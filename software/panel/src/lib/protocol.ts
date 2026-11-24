// Mirrors firmware/shared/protocol.h. Keep the two in sync.

export type SensorStatus = 'ok' | 'calibrating' | 'sensor_fault' | 'uncalibrated'

export interface EmgFrame {
  t: number              // Pico millis() of the newest raw sample
  raw: number[]          // raw ADC counts since the previous frame
  raw_hz: number         // sample rate of raw[]
  env: number            // envelope, ADC counts (moving RMS around baseline)
  act: number            // 0..1, what the reward display uses as drive
  baseline: number       // ADC counts
  rest: number           // envelope at rest
  mvc: number            // envelope at max voluntary contraction
  has_baseline: boolean
  has_mvc: boolean
  status: SensorStatus
  cfg: EmgSettings
  cal?: { phase: 'baseline' | 'mvc'; remaining_ms: number }
  lost: number           // samples lost on the Pico (should stay 0)
}

export interface EmgSettings {
  window_ms: number
  gain: number
  deadband: number
}

export interface EmgEvent {
  event: string          // calibrate_baseline | calibrate_mvc | set | error
  ok: boolean
  msg: string
}

export type Command =
  | { cmd: 'calibrate_baseline' }
  | { cmd: 'calibrate_mvc' }
  | ({ cmd: 'set' } & Partial<EmgSettings>)
  | { cmd: 'reset_settings' }

export const WS_PORT = 81
export const DEFAULT_WS_URL = `ws://centad-emg.local:${WS_PORT}`
export const BASELINE_CAL_MS = 5000
export const MVC_CAL_MS = 4000
export const LIMITS = {
  window_ms: { min: 50, max: 500, step: 10 },
  gain: { min: 0.1, max: 5, step: 0.05 },
  deadband: { min: 0, max: 0.5, step: 0.01 },
} as const

export function isFrame(m: unknown): m is EmgFrame {
  return typeof m === 'object' && m !== null && typeof (m as EmgFrame).act === 'number'
}
export function isEvent(m: unknown): m is EmgEvent {
  return typeof m === 'object' && m !== null && typeof (m as EmgEvent).event === 'string'
}

/** Best default: if the panel is served by the Pico itself, talk to that host. */
export function defaultWsUrl(): string {
  const q = new URLSearchParams(location.search).get('ws')
  if (q) return q
  const saved = localStorage.getItem('centad.wsUrl')
  if (saved) return saved
  const h = location.hostname
  const isDev = h === 'localhost' || h === '127.0.0.1' || h === '' || location.port !== ''
  if (location.protocol === 'http:' && !isDev) return `ws://${h}:${WS_PORT}`
  return DEFAULT_WS_URL
}
