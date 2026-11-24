<script setup lang="ts">
/**
 * Two stacked uPlot charts on one synced time axis (never a dual y-axis):
 *   1. Activation 0–100 % (what the display receives as drive)
 *   2. Envelope in ADC counts, with optional raw (centred on baseline) behind it,
 *      and dashed rest / 100 % (auto or manual max) reference lines.
 * Data lives in a fixed-size ring; redraws are rAF-throttled.
 */
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import uPlot from 'uplot'
import 'uplot/dist/uPlot.min.css'
import { SeriesRing } from '@/lib/ring'
import type { EmgFrame } from '@/lib/protocol'

const props = defineProps<{
  onFrame: (fn: (f: EmgFrame) => void) => () => void
  windowSeconds: number
  showRaw: boolean
}>()

const C = {
  act: '#2a78d6',      // categorical slot 1 (validated)
  env: '#2a78d6',
  raw: '#b8b8b4',      // recessive context trace
  ref: '#6b6b66',
  grid: '#ecebe8',
  axis: '#6b6b66',
}

const MAX_RAW_HZ = 1000
const MAX_WINDOW_S = 60
const ring = new SeriesRing(['t', 'raw', 'env', 'act'] as const, MAX_RAW_HZ * MAX_WINDOW_S)
let baseline = NaN
let rest = NaN
let mvc = NaN

const actEl = ref<HTMLDivElement>()
const envEl = ref<HTMLDivElement>()
let uAct: uPlot | null = null
let uEnv: uPlot | null = null
let dirty = false
let raf = 0
let unsub: (() => void) | null = null
let ro: ResizeObserver | null = null

function ingest(f: EmgFrame) {
  baseline = f.has_baseline ? f.baseline : NaN
  rest = f.has_baseline ? f.rest : NaN
  mvc = f.has_baseline && (f.mvc_mode === 'auto' || f.has_mvc) ? f.mvc : NaN
  const n = f.raw.length
  if (!n) {
    ring.push({ t: f.t, raw: NaN, env: f.env, act: f.act })
  } else {
    const dt = 1000 / f.raw_hz
    for (let i = 0; i < n; i++) {
      const t = f.t - (n - 1 - i) * dt
      if (t <= ring.last('t')) continue
      ring.push({ t, raw: f.raw[i], env: f.env, act: f.act })
    }
  }
  dirty = true
}

function visibleSlice() {
  const t = ring.ordered('t')
  const tEnd = t.length ? t[t.length - 1] : 0
  const tStart = tEnd - props.windowSeconds * 1000
  let i0 = 0
  while (i0 < t.length && t[i0] < tStart) i0++
  const xs = new Float64Array(t.length - i0)
  for (let i = i0; i < t.length; i++) xs[i - i0] = (t[i] - tEnd) / 1000
  const env = ring.ordered('env').subarray(i0)
  const act = ring.ordered('act').subarray(i0)
  const rawAbs = ring.ordered('raw').subarray(i0)
  const raw = new Float64Array(rawAbs.length)
  const b = Number.isFinite(baseline) ? baseline : 2048
  for (let i = 0; i < raw.length; i++) raw[i] = rawAbs[i] - b
  const actPct = new Float64Array(act.length)
  for (let i = 0; i < act.length; i++) actPct[i] = act[i] * 100
  return { xs, env, actPct, raw }
}

function draw() {
  raf = requestAnimationFrame(draw)
  if (!dirty || !uAct || !uEnv) return
  dirty = false
  const { xs, env, actPct, raw } = visibleSlice()
  const x = Array.from(xs)
  uAct.setData([x, Array.from(actPct)], false)
  uAct.setScale('x', { min: -props.windowSeconds, max: 0 })
  uEnv.setData([x, props.showRaw ? Array.from(raw) : x.map(() => null), Array.from(env)], false)
  uEnv.setScale('x', { min: -props.windowSeconds, max: 0 })
}

// Dashed horizontal reference lines (rest / MVC) with a small text label.
function refLines(u: uPlot) {
  const ctx = u.ctx
  const lines: [number, string][] = []
  if (Number.isFinite(rest)) lines.push([rest, 'rest'])
  if (Number.isFinite(mvc)) lines.push([mvc, '100 %'])
  ctx.save()
  ctx.setLineDash([6, 4])
  ctx.lineWidth = 1.5 * devicePixelRatio
  ctx.strokeStyle = C.ref
  ctx.fillStyle = C.ref
  ctx.font = `${11 * devicePixelRatio}px -apple-system, system-ui, sans-serif`
  for (const [v, label] of lines) {
    const y = u.valToPos(v, 'y', true)
    if (y < u.bbox.top || y > u.bbox.top + u.bbox.height) continue
    ctx.beginPath()
    ctx.moveTo(u.bbox.left, y)
    ctx.lineTo(u.bbox.left + u.bbox.width, y)
    ctx.stroke()
    ctx.textAlign = 'right'
    ctx.fillText(label, u.bbox.left + u.bbox.width - 6 * devicePixelRatio, y - 4 * devicePixelRatio)
  }
  ctx.restore()
}

const axis = (label: string): uPlot.Axis => ({
  label,
  stroke: C.axis,
  grid: { stroke: C.grid, width: 1 },
  ticks: { show: false },
  font: '12px -apple-system, system-ui, sans-serif',
  labelFont: '12px -apple-system, system-ui, sans-serif',
})
const xAxis = (): uPlot.Axis => {
  const { label: _omit, labelFont: _f, ...rest } = axis('')
  return { ...rest, size: 28, values: (_u, v) => v.map((s) => (s === 0 ? 'now' : `${s}s`)) }
}

function make() {
  const width = actEl.value!.clientWidth
  const sync = uPlot.sync('emg')
  const cursor: uPlot.Cursor = { sync: { key: sync.key }, points: { size: 8 }, drag: { x: false, y: false } }

  uAct = new uPlot(
    {
      width,
      height: 170,
      cursor,
      legend: { show: true },
      scales: { x: { time: false }, y: { range: [0, 100] } },
      axes: [xAxis(), { ...axis('%'), values: (_u, v) => v.map((n) => `${n}`) }],
      series: [
        { label: 's', value: (_u, v) => (v == null ? '–' : `${v.toFixed(2)} s`) },
        { label: 'Activation', stroke: C.act, width: 2, value: (_u, v) => (v == null ? '–' : `${v.toFixed(0)} %`) },
      ],
    },
    [[], []],
    actEl.value!,
  )

  uEnv = new uPlot(
    {
      width,
      height: 200,
      cursor,
      legend: { show: true },
      scales: {
        x: { time: false },
        y: {
          range: (_u, lo, hi) => {
            const top = Math.max(hi, Number.isFinite(mvc) ? mvc * 1.15 : 0, 20)
            return [Math.min(0, lo), top]
          },
        },
      },
      axes: [xAxis(), axis('counts')],
      series: [
        { label: 's', value: (_u, v) => (v == null ? '–' : `${v.toFixed(2)} s`) },
        // raw first so it draws underneath the envelope
        { label: 'Raw − baseline', stroke: C.raw, width: 1, value: (_u, v) => (v == null ? '–' : v.toFixed(0)) },
        { label: 'Envelope', stroke: C.env, width: 2, value: (_u, v) => (v == null ? '–' : v.toFixed(1)) },
      ],
      hooks: { draw: [refLines] },
    },
    [[], [], []],
    envEl.value!,
  )
}

onMounted(() => {
  make()
  unsub = props.onFrame(ingest)
  raf = requestAnimationFrame(draw)
  ro = new ResizeObserver(() => {
    const w = actEl.value?.clientWidth ?? 0
    if (w > 0) {
      uAct?.setSize({ width: w, height: 170 })
      uEnv?.setSize({ width: w, height: 200 })
    }
  })
  ro.observe(actEl.value!)
})

watch(() => [props.windowSeconds, props.showRaw], () => (dirty = true))

onBeforeUnmount(() => {
  cancelAnimationFrame(raf)
  unsub?.()
  ro?.disconnect()
  uAct?.destroy()
  uEnv?.destroy()
})

defineExpose({ clear: () => { ring.clear(); dirty = true } })
</script>

<template>
  <div class="space-y-4">
    <div>
      <div class="text-sm font-medium text-muted-foreground mb-1">Activation (drive sent to display)</div>
      <div ref="actEl" class="w-full" />
    </div>
    <div>
      <div class="text-sm font-medium text-muted-foreground mb-1">EMG envelope (ADC counts)</div>
      <div ref="envEl" class="w-full" />
    </div>
  </div>
</template>

<style>
.uplot .u-legend { font-size: 12px; text-align: left; }
.uplot .u-legend .u-marker { border-radius: 2px; }
</style>
