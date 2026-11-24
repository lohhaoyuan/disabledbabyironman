import { computed, onBeforeUnmount, ref, shallowRef } from 'vue'
import { isEvent, isFrame, type Command, type EmgEvent, type EmgFrame } from '@/lib/protocol'

export type LinkState = 'idle' | 'connecting' | 'open' | 'reconnecting' | 'blocked'

const STALE_MS = 1500
const RETRY_MIN_MS = 500
const RETRY_MAX_MS = 5000

/**
 * One WebSocket to the EMG node, auto-reconnecting. Frames go to listeners
 * (non-reactive, for the chart/recorder at full rate); the latest frame is also
 * exposed reactively but only refreshed ~10x/s so the UI never thrashes.
 */
export function useEmgSocket() {
  const url = ref('')
  const link = ref<LinkState>('idle')
  const linkNote = ref('')
  const frame = shallowRef<EmgFrame | null>(null)
  const lastFrameAt = ref(0)
  const now = ref(performance.now())
  const events = ref<(EmgEvent & { at: Date })[]>([])
  const rateHz = ref(0)

  const listeners = new Set<(f: EmgFrame) => void>()
  let ws: WebSocket | null = null
  let wanted = false
  let retryMs = RETRY_MIN_MS
  let retryTimer: ReturnType<typeof setTimeout> | undefined
  let pending: EmgFrame | null = null
  let count = 0
  let rateStart = performance.now()

  const tick = setInterval(() => {
    now.value = performance.now()
    if (pending) {
      frame.value = pending
      pending = null
    }
    if (now.value - rateStart >= 1000) {
      rateHz.value = (count * 1000) / (now.value - rateStart)
      count = 0
      rateStart = now.value
    }
  }, 100)

  const stale = computed(() => link.value === 'open' && now.value - lastFrameAt.value > STALE_MS)

  function open() {
    clearTimeout(retryTimer)
    if (!wanted) return
    link.value = link.value === 'idle' ? 'connecting' : 'reconnecting'
    try {
      ws = new WebSocket(url.value)
    } catch (e) {
      link.value = 'blocked'
      linkNote.value =
        location.protocol === 'https:' && url.value.startsWith('ws:')
          ? 'This page is HTTPS, so the browser blocks ws://. Open the panel over http:// (e.g. http://centad-emg.local/panel/).'
          : String((e as Error).message ?? e)
      return
    }
    ws.onopen = () => {
      link.value = 'open'
      linkNote.value = ''
      retryMs = RETRY_MIN_MS
    }
    ws.onmessage = (ev) => {
      let m: unknown
      try {
        m = JSON.parse(ev.data as string)
      } catch {
        return
      }
      if (isFrame(m)) {
        lastFrameAt.value = performance.now()
        count++
        pending = m
        for (const l of listeners) l(m)
      } else if (isEvent(m)) {
        events.value = [{ ...m, at: new Date() }, ...events.value].slice(0, 8)
      }
    }
    ws.onclose = () => {
      ws = null
      if (!wanted) {
        link.value = 'idle'
        return
      }
      link.value = 'reconnecting'
      retryTimer = setTimeout(open, retryMs)
      retryMs = Math.min(retryMs * 2, RETRY_MAX_MS)
    }
  }

  function connect(u: string) {
    disconnect()
    url.value = u.trim()
    localStorage.setItem('centad.wsUrl', url.value)
    wanted = true
    link.value = 'idle'
    open()
  }

  function disconnect() {
    wanted = false
    clearTimeout(retryTimer)
    ws?.close()
    ws = null
    link.value = 'idle'
    frame.value = null
  }

  function send(c: Command): boolean {
    if (!ws || ws.readyState !== WebSocket.OPEN) return false
    ws.send(JSON.stringify(c))
    return true
  }

  function onFrame(fn: (f: EmgFrame) => void) {
    listeners.add(fn)
    return () => listeners.delete(fn)
  }

  // iPad: reconnect immediately when the tab comes back.
  const onVisible = () => {
    if (document.visibilityState === 'visible' && wanted && !ws) {
      retryMs = RETRY_MIN_MS
      open()
    }
  }
  document.addEventListener('visibilitychange', onVisible)

  onBeforeUnmount(() => {
    clearInterval(tick)
    document.removeEventListener('visibilitychange', onVisible)
    disconnect()
  })

  return { url, link, linkNote, frame, stale, events, rateHz, connect, disconnect, send, onFrame }
}
