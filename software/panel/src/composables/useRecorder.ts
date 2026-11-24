import { ref } from 'vue'
import type { EmgFrame, SensorStatus } from '@/lib/protocol'

/**
 * Research log. One row per raw sample (raw_hz, typically 500 Hz); envelope /
 * activation / calibration are the values of the frame the sample arrived in
 * (they update at 20 Hz). Stored in typed-array chunks: ~30 bytes/row, so an
 * hour at 500 Hz is ~55 MB. Fine on a laptop; on an iPad keep sessions shorter.
 */
const CHUNK = 1 << 16
const STATUS: SensorStatus[] = ['ok', 'calibrating', 'sensor_fault', 'uncalibrated']

interface Chunk {
  t: Float64Array
  raw: Uint16Array
  env: Float32Array
  act: Float32Array
  base: Float32Array
  mvc: Float32Array
  st: Uint8Array
  n: number
}

const newChunk = (): Chunk => ({
  t: new Float64Array(CHUNK),
  raw: new Uint16Array(CHUNK),
  env: new Float32Array(CHUNK),
  act: new Float32Array(CHUNK),
  base: new Float32Array(CHUNK),
  mvc: new Float32Array(CHUNK),
  st: new Uint8Array(CHUNK),
  n: 0,
})

export function useRecorder() {
  const recording = ref(false)
  const rows = ref(0)
  const startedAt = ref<Date | null>(null)
  const stoppedAt = ref<Date | null>(null)
  let chunks: Chunk[] = []
  let cur: Chunk | null = null
  let lastT = -Infinity

  function start() {
    if (recording.value) return
    if (!startedAt.value) startedAt.value = new Date()
    stoppedAt.value = null
    recording.value = true
  }
  function stop() {
    recording.value = false
    stoppedAt.value = new Date()
  }
  function clear() {
    recording.value = false
    chunks = []
    cur = null
    rows.value = 0
    lastT = -Infinity
    startedAt.value = stoppedAt.value = null
  }

  function add(f: EmgFrame) {
    if (!recording.value || !f.raw.length) return
    const dt = 1000 / f.raw_hz
    const st = Math.max(0, STATUS.indexOf(f.status))
    for (let i = 0; i < f.raw.length; i++) {
      const t = f.t - (f.raw.length - 1 - i) * dt
      if (t <= lastT) continue // duplicate frame (e.g. resent on connect)
      lastT = t
      if (!cur || cur.n === CHUNK) {
        cur = newChunk()
        chunks.push(cur)
      }
      const j = cur.n++
      cur.t[j] = t
      cur.raw[j] = f.raw[i]
      cur.env[j] = f.env
      cur.act[j] = f.act
      cur.base[j] = f.baseline
      cur.mvc[j] = f.mvc
      cur.st[j] = st
    }
    rows.value = chunks.reduce((s, c) => s + c.n, 0)
  }

  function exportCsv(label = '') {
    const parts: string[] = [
      '# CENTAD EMG recording\n',
      `# started ${startedAt.value?.toISOString() ?? ''}\n`,
      `# label ${label.replace(/[\r\n]/g, ' ')}\n`,
      '# timestamp_ms = Pico clock; raw = ADC counts (12-bit); envelope = RMS counts; activation = 0..1 (display drive)\n',
      'timestamp_ms,raw,envelope,activation,baseline,mvc,status\n',
    ]
    for (const c of chunks) {
      let s = ''
      for (let j = 0; j < c.n; j++) {
        s += `${c.t[j].toFixed(1)},${c.raw[j]},${c.env[j].toFixed(2)},${c.act[j].toFixed(4)},${c.base[j].toFixed(0)},${c.mvc[j].toFixed(2)},${STATUS[c.st[j]]}\n`
      }
      parts.push(s)
    }
    const blob = new Blob(parts, { type: 'text/csv' })
    const stamp = (startedAt.value ?? new Date()).toISOString().replace(/[:.]/g, '-').slice(0, 19)
    const safe = label.trim().replace(/[^\w-]+/g, '_')
    const a = document.createElement('a')
    a.href = URL.createObjectURL(blob)
    a.download = `centad-emg_${safe ? safe + '_' : ''}${stamp}.csv`
    document.body.appendChild(a)
    a.click()
    a.remove()
    setTimeout(() => URL.revokeObjectURL(a.href), 10_000)
  }

  return { recording, rows, startedAt, stoppedAt, start, stop, clear, add, exportCsv }
}
