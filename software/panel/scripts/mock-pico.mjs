#!/usr/bin/env node
// Mock CENTAD EMG node: speaks the same WebSocket protocol as the Pico
// (firmware/shared/protocol.h) with synthetic EMG, so the panel and the
// reward display can be developed without hardware.
//
//   npm run mock                 -> ws://localhost:8181
//   PORT=81 npm run mock         (port 81 may need sudo)
//
// Keys while running: c = toggle continuous contraction, f = toggle sensor
// fault (flat signal), q = quit.

import { WebSocketServer } from 'ws'

const PORT = Number(process.env.PORT ?? 8181)
const FS = 1000, RAW_DECIMATE = 2, BROADCAST_HZ = 20
const BASELINE_MS = 5000, MVC_MS = 4000

const DEFAULTS = { window_ms: 150, gain: 1, deadband: 0.05, auto_mvc: true, auto_window_s: 60, auto_floor_x: 5 }
const cfg = { ...DEFAULTS }
const cal = { has_baseline: false, has_mvc: false, baseline: 0, rest: 0, mvc: 0 }
let phase = null, calN = 0, calTotal = 0, calSum = 0, calEnvN = 0, calPeak = 0
let forceContract = false, fault = false
// auto max: per-second envelope peaks (same rule as firmware/pico/emg.cpp)
let peaks = [], secN = 0, secMax = 0, autoRef = 0
const autoFloor = () => Math.max(cal.rest * cfg.auto_floor_x, cal.rest + 10)
function updateAuto() {
  const h = peaks.slice(-cfg.auto_window_s)
  if (h.length < 5) { autoRef = autoFloor(); return }
  const sorted = [...h].sort((a, b) => a - b)
  const idx = Math.min(Math.floor(0.9 * (h.length - 1)), h.length - 2)
  autoRef = Math.max(sorted[idx], autoFloor())
}
function resetAuto() { peaks = []; secN = 0; secMax = 0; updateAuto() }
const reference = () => cfg.auto_mvc ? autoRef : cal.mvc

// ---- synthetic signal: rest noise + periodic bursts (like a crawling kick)
let n = 0
function sample() {
  n++
  if (fault) return 2047
  const t = n / FS
  const burst = forceContract || (phase === 'mvc') ? 1 : Math.max(0, Math.sin(t * 2 * Math.PI / 4)) ** 3
  const g = () => { let s = 0; for (let i = 0; i < 6; i++) s += Math.random(); return (s - 3) / 0.707 }
  const v = 2010 + g() * (4 + 90 * burst)
  return Math.max(0, Math.min(4095, Math.round(v)))
}

// ---- same chain as firmware/pico/emg.cpp
let sq = [], sqSum = 0, env = 0, act = 0
function resetWin() { sq = []; sqSum = 0 }
function push(x) {
  const b = cal.has_baseline ? Math.round(cal.baseline) : 2048
  const s = (x - b) ** 2
  sq.push(s); sqSum += s
  const w = Math.round(cfg.window_ms * FS / 1000)
  while (sq.length > w) sqSum -= sq.shift()
  env = Math.sqrt(sqSum / sq.length)

  if (phase === 'baseline') {
    calN++
    const split = Math.floor(calTotal * 0.6)
    if (calN <= split) { calSum += x; if (calN === split) { cal.baseline = calSum / split; cal.has_baseline = true; resetWin(); calSum = 0 } }
    else if (sq.length >= w) { calSum += env; calEnvN++ }
    if (calN >= calTotal) { cal.rest = calEnvN ? calSum / calEnvN : env; phase = null; resetAuto(); event('calibrate_baseline', true, `baseline ${cal.baseline.toFixed(0)} counts, rest envelope ${cal.rest.toFixed(1)}`) }
  } else if (phase === 'mvc') {
    calN++; calPeak = Math.max(calPeak, env)
    if (calN >= calTotal) {
      phase = null
      if (calPeak < Math.max(cal.rest * 2, cal.rest + 5)) event('calibrate_mvc', false, 'rejected: no contraction seen')
      else { cal.mvc = calPeak; cal.has_mvc = true; event('calibrate_mvc', true, `MVC envelope ${cal.mvc.toFixed(1)} (rest ${cal.rest.toFixed(1)})`) }
    }
  }
  const st = status()
  if (st === 'ok') {
    secMax = Math.max(secMax, env)
    if (++secN >= FS) { peaks.push(secMax); if (peaks.length > 300) peaks.shift(); secN = 0; secMax = 0; updateAuto() }
  }
  if (st !== 'ok') { act = 0; return }
  let a = cfg.gain * (env - cal.rest) / (reference() - cal.rest)
  a = Math.min(1, Math.max(0, a))
  act = a <= cfg.deadband ? 0 : (a - cfg.deadband) / (1 - cfg.deadband)
}
const status = () => phase ? 'calibrating' : fault ? 'sensor_fault' : (cal.has_baseline && (cfg.auto_mvc || cal.has_mvc)) ? 'ok' : 'uncalibrated'

// ---- server
const wss = new WebSocketServer({ port: PORT })
const all = (m) => { const s = JSON.stringify(m); for (const c of wss.clients) if (c.readyState === 1) c.send(s) }
function event(name, ok, msg) { all({ event: name, ok, msg }); console.log(`[event] ${name} ok=${ok} ${msg}`) }

wss.on('connection', (ws, req) => {
  console.log(`[ws] client ${req.socket.remoteAddress} connected (${wss.clients.size})`)
  ws.on('message', (data) => {
    let m; try { m = JSON.parse(data) } catch { return ws.send(JSON.stringify({ event: 'error', ok: false, msg: 'bad JSON' })) }
    if (m.cmd === 'calibrate_baseline' || m.cmd === 'calibrate_mvc') {
      if (phase) return ws.send(JSON.stringify({ event: m.cmd, ok: false, msg: 'calibration already running' }))
      if (m.cmd === 'calibrate_mvc' && !cal.has_baseline) return ws.send(JSON.stringify({ event: m.cmd, ok: false, msg: 'calibrate rest (baseline) first' }))
      phase = m.cmd === 'calibrate_baseline' ? 'baseline' : 'mvc'
      calTotal = (phase === 'baseline' ? BASELINE_MS : MVC_MS) * FS / 1000
      calN = calSum = calEnvN = calPeak = 0
      event(m.cmd, true, 'started')
    } else if (m.cmd === 'set') {
      if (typeof m.window_ms === 'number') cfg.window_ms = Math.min(500, Math.max(50, Math.round(m.window_ms)))
      if (typeof m.gain === 'number') cfg.gain = Math.min(10, Math.max(0.1, m.gain))
      if (typeof m.deadband === 'number') cfg.deadband = Math.min(0.5, Math.max(0, m.deadband))
      if (typeof m.auto_mvc === 'boolean') cfg.auto_mvc = m.auto_mvc
      if (typeof m.auto_window_s === 'number') cfg.auto_window_s = Math.min(300, Math.max(10, Math.round(m.auto_window_s)))
      if (typeof m.auto_floor_x === 'number') cfg.auto_floor_x = Math.min(20, Math.max(2, m.auto_floor_x))
      resetWin(); updateAuto()
      event('set', true, `window ${cfg.window_ms} ms, gain ${cfg.gain.toFixed(2)}, deadband ${cfg.deadband.toFixed(2)}, max ${cfg.auto_mvc ? 'auto' : 'manual'} (${cfg.auto_window_s} s, floor ${cfg.auto_floor_x.toFixed(1)}x rest)`)
    } else if (m.cmd === 'reset_auto') {
      resetAuto(); event('reset_auto', true, 'learned max cleared; relearning from now')
    } else if (m.cmd === 'reset_settings') {
      Object.assign(cfg, DEFAULTS); resetWin(); updateAuto()
      event('set', true, 'settings reset to defaults (calibration kept)')
    } else ws.send(JSON.stringify({ event: 'error', ok: false, msg: 'unknown cmd' }))
  })
})

const t0 = Date.now()
let raw = [], decim = 0, produced = 0
setInterval(() => {
  const due = Math.floor((Date.now() - t0) * FS / 1000)
  while (produced < due) {
    const x = sample(); push(x); produced++
    if (++decim >= RAW_DECIMATE) { decim = 0; raw.push(x) }
  }
  const frame = {
    t: produced, raw, raw_hz: FS / RAW_DECIMATE, env: +env.toFixed(2), act: +act.toFixed(4),
    baseline: Math.round(cal.baseline), rest: +cal.rest.toFixed(2), mvc: +reference().toFixed(2),
    mvc_mode: cfg.auto_mvc ? 'auto' : 'manual', mvc_manual: +cal.mvc.toFixed(2),
    auto_hist_s: Math.min(peaks.length, cfg.auto_window_s),
    has_baseline: cal.has_baseline, has_mvc: cal.has_mvc, status: status(),
    cfg: { ...cfg }, lost: 0,
  }
  if (phase) frame.cal = { phase, remaining_ms: Math.round((calTotal - calN) * 1000 / FS) }
  all(frame)
  raw = []
}, 1000 / BROADCAST_HZ)

console.log(`mock CENTAD EMG node on ws://localhost:${PORT}  (keys: c contract, f fault, q quit)`)
if (process.stdin.isTTY) {
  process.stdin.setRawMode(true)
  process.stdin.on('data', (k) => {
    const c = k.toString()
    if (c === 'c') console.log('contract', (forceContract = !forceContract))
    if (c === 'f') console.log('fault', (fault = !fault))
    if (c === 'q' || c === '\u0003') process.exit(0)
  })
}
