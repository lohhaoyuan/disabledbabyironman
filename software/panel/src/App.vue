<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { AlertTriangle, CircleCheck, CircleDashed, Download, Loader2, Play, Plug, Square, Trash2, Unplug, WifiOff } from '@lucide/vue'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Slider } from '@/components/ui/slider'
import { Switch } from '@/components/ui/switch'
import LiveCharts from '@/components/LiveCharts.vue'
import { useEmgSocket } from '@/composables/useEmgSocket'
import { useRecorder } from '@/composables/useRecorder'
import { BASELINE_CAL_MS, LIMITS, MVC_CAL_MS, defaultWsUrl, type EmgSettings } from '@/lib/protocol'

const sock = useEmgSocket()
const rec = useRecorder()
const address = ref(defaultWsUrl())
const windowSeconds = ref(10)
const showRaw = ref(false)
const label = ref('')

const f = computed(() => sock.frame.value)

// ---------------------------------------------------------------- status badge
type Tone = 'good' | 'warn' | 'bad' | 'idle'
const badge = computed<{ text: string; tone: Tone; icon: unknown; spin?: boolean }>(() => {
  const l = sock.link.value
  if (l === 'idle') return { text: 'Not connected', tone: 'idle', icon: Unplug }
  if (l === 'blocked') return { text: 'Blocked (HTTPS page)', tone: 'bad', icon: WifiOff }
  if (l === 'connecting') return { text: 'Connecting…', tone: 'warn', icon: Loader2, spin: true }
  if (l === 'reconnecting') return { text: 'Reconnecting…', tone: 'warn', icon: Loader2, spin: true }
  if (sock.stale.value || !f.value) return { text: 'Connected, no data', tone: 'warn', icon: AlertTriangle }
  switch (f.value.status) {
    case 'ok': return { text: 'Connected', tone: 'good', icon: CircleCheck }
    case 'calibrating': return { text: 'Calibrating', tone: 'warn', icon: Loader2, spin: true }
    case 'sensor_fault': return { text: 'Sensor fault', tone: 'bad', icon: AlertTriangle }
    case 'uncalibrated': return { text: 'Needs calibration', tone: 'warn', icon: CircleDashed }
  }
  return { text: '?', tone: 'idle', icon: CircleDashed }
})
const toneClass: Record<Tone, string> = {
  good: 'bg-emerald-50 text-emerald-800 border-emerald-200',
  warn: 'bg-amber-50 text-amber-900 border-amber-200',
  bad: 'bg-red-50 text-red-800 border-red-200',
  idle: 'bg-muted text-muted-foreground border-border',
}

const live = computed(() => sock.link.value === 'open' && !sock.stale.value && !!f.value)
const actPct = computed(() => (live.value ? Math.round(f.value!.act * 100) : null))
const drive = computed(() => (live.value ? f.value!.act.toFixed(2) : '–'))

// ---------------------------------------------------------------- calibration
const prep = reactive({ phase: '' as '' | 'baseline' | 'mvc', left: 0 })
let prepTimer: ReturnType<typeof setInterval> | undefined

function calibrate(phase: 'baseline' | 'mvc') {
  clearInterval(prepTimer)
  prep.phase = phase
  prep.left = 3
  prepTimer = setInterval(() => {
    prep.left--
    if (prep.left <= 0) {
      clearInterval(prepTimer)
      prep.phase = ''
      sock.send({ cmd: phase === 'baseline' ? 'calibrate_baseline' : 'calibrate_mvc' })
    }
  }, 1000)
}
function cancelPrep() {
  clearInterval(prepTimer)
  prep.phase = ''
}
const calRunning = computed(() => f.value?.cal ?? null)
const calProgress = computed(() => {
  const c = calRunning.value
  if (!c) return 0
  const total = c.phase === 'baseline' ? BASELINE_CAL_MS : MVC_CAL_MS
  return Math.min(100, Math.max(0, 100 - (c.remaining_ms / total) * 100))
})
const lastCalEvent = computed(() => sock.events.value.find((e) => e.event.startsWith('calibrate')))

// ---------------------------------------------------------------- settings
const draft = reactive<EmgSettings>({ window_ms: 150, gain: 1, deadband: 0.05 })
const draftDirty = ref(false)
watch(
  () => f.value?.cfg,
  (cfg) => {
    if (cfg && !draftDirty.value) Object.assign(draft, cfg)
  },
)
function setDraft<K extends keyof EmgSettings>(k: K, v: number[] | undefined) {
  if (!v) return
  draft[k] = v[0] as EmgSettings[K]
  draftDirty.value = true
}
function applySettings() {
  if (sock.send({ cmd: 'set', ...draft })) draftDirty.value = false
}
function revertSettings() {
  draftDirty.value = false
  if (f.value) Object.assign(draft, f.value.cfg)
}

// ---------------------------------------------------------------- recording
const unsubRec = sock.onFrame(rec.add)
const elapsed = ref('0:00')
const clock = setInterval(() => {
  if (!rec.startedAt.value) return (elapsed.value = '0:00')
  const end = rec.recording.value ? Date.now() : (rec.stoppedAt.value ?? new Date()).getTime()
  const s = Math.floor((end - rec.startedAt.value.getTime()) / 1000)
  elapsed.value = `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`
}, 500)

function connectToggle() {
  if (sock.link.value === 'idle' || sock.link.value === 'blocked') sock.connect(address.value)
  else sock.disconnect()
}

onMounted(() => sock.connect(address.value))
onBeforeUnmount(() => {
  clearInterval(clock)
  clearInterval(prepTimer)
  unsubRec()
})

const fmt = (v: number | undefined, d = 1) => (v == null || !Number.isFinite(v) ? '–' : v.toFixed(d))
</script>

<template>
  <div class="min-h-screen bg-neutral-50 text-foreground">
    <!-- Header -->
    <header class="sticky top-0 z-10 border-b bg-white/90 backdrop-blur">
      <div class="mx-auto flex max-w-6xl flex-wrap items-center gap-3 px-4 py-3">
        <div class="mr-auto">
          <div class="text-base font-semibold tracking-tight">CENTAD · EMG</div>
          <div class="text-xs text-muted-foreground">Clinician debug panel</div>
        </div>
        <form class="flex items-center gap-2" @submit.prevent="connectToggle">
          <Label for="addr" class="sr-only">WebSocket address</Label>
          <Input id="addr" v-model="address" class="h-9 w-64 font-mono text-sm" spellcheck="false" autocapitalize="off" />
          <Button type="submit" size="sm" :variant="sock.link.value === 'idle' ? 'default' : 'outline'">
            <component :is="sock.link.value === 'idle' || sock.link.value === 'blocked' ? Plug : Unplug" class="size-4" />
            {{ sock.link.value === 'idle' || sock.link.value === 'blocked' ? 'Connect' : 'Disconnect' }}
          </Button>
        </form>
        <span
          class="inline-flex h-9 items-center gap-2 rounded-md border px-3 text-sm font-medium"
          :class="toneClass[badge.tone]"
          role="status"
        >
          <component :is="badge.icon" class="size-4" :class="badge.spin && 'animate-spin'" />
          {{ badge.text }}
          <span v-if="live" class="font-normal opacity-70">· {{ sock.rateHz.value.toFixed(0) }} Hz</span>
        </span>
      </div>
      <div v-if="sock.linkNote.value" class="mx-auto max-w-6xl px-4 pb-3 text-sm text-red-700">
        {{ sock.linkNote.value }}
      </div>
    </header>

    <main class="mx-auto grid max-w-6xl gap-4 px-4 py-4">
      <!-- Big numbers -->
      <section class="grid gap-4 sm:grid-cols-3">
        <Card class="sm:col-span-1">
          <CardHeader class="pb-0"><CardDescription>Muscle activation</CardDescription></CardHeader>
          <CardContent>
            <div class="text-7xl font-semibold tabular-nums tracking-tight">
              {{ actPct ?? '–' }}<span class="text-3xl text-muted-foreground">%</span>
            </div>
            <div class="mt-3 h-2 w-full overflow-hidden rounded-full bg-muted">
              <div class="h-full rounded-full bg-[#2a78d6] transition-[width] duration-100" :style="{ width: `${actPct ?? 0}%` }" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardHeader class="pb-0"><CardDescription>Drive sent to display</CardDescription></CardHeader>
          <CardContent>
            <div class="text-7xl font-semibold tabular-nums tracking-tight">{{ drive }}</div>
            <div class="mt-2 text-xs text-muted-foreground">
              0–1 value the reward display receives (it smooths it further). Forced to 0 while calibrating or on sensor fault.
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardHeader class="pb-0"><CardDescription>Envelope</CardDescription></CardHeader>
          <CardContent>
            <div class="text-5xl font-semibold tabular-nums tracking-tight">
              {{ live ? fmt(f?.env) : '–' }}<span class="ml-1 text-lg text-muted-foreground">counts</span>
            </div>
            <dl class="mt-3 grid grid-cols-3 gap-2 text-sm">
              <div><dt class="text-muted-foreground">Baseline</dt><dd class="font-medium tabular-nums">{{ f?.has_baseline ? f.baseline : '–' }}</dd></div>
              <div><dt class="text-muted-foreground">Rest</dt><dd class="font-medium tabular-nums">{{ f?.has_baseline ? fmt(f.rest) : '–' }}</dd></div>
              <div><dt class="text-muted-foreground">MVC</dt><dd class="font-medium tabular-nums">{{ f?.has_mvc ? fmt(f.mvc) : '–' }}</dd></div>
            </dl>
            <div v-if="f && f.lost > 0" class="mt-2 text-xs text-amber-800">{{ f.lost }} samples dropped on Pico</div>
          </CardContent>
        </Card>
      </section>

      <!-- Chart -->
      <Card>
        <CardHeader class="flex flex-row flex-wrap items-center gap-4">
          <CardTitle class="mr-auto">Live signal</CardTitle>
          <div class="flex items-center gap-2 text-sm">
            <span class="text-muted-foreground">Window</span>
            <div class="inline-flex rounded-md border p-0.5">
              <button
                v-for="s in [5, 10, 30, 60]"
                :key="s"
                type="button"
                class="rounded px-2.5 py-1 text-sm"
                :class="windowSeconds === s ? 'bg-foreground text-background' : 'text-muted-foreground hover:bg-muted'"
                @click="windowSeconds = s"
              >{{ s }} s</button>
            </div>
          </div>
          <label class="flex items-center gap-2 text-sm">
            <Switch v-model="showRaw" />
            <span>Show raw</span>
          </label>
        </CardHeader>
        <CardContent>
          <LiveCharts :on-frame="sock.onFrame" :window-seconds="windowSeconds" :show-raw="showRaw" />
        </CardContent>
      </Card>

      <section class="grid gap-4 md:grid-cols-2">
        <!-- Calibration -->
        <Card>
          <CardHeader>
            <CardTitle>Calibration</CardTitle>
            <CardDescription>Rest first, then max effort. Values are saved on the Pico.</CardDescription>
          </CardHeader>
          <CardContent class="space-y-4">
            <div class="grid grid-cols-2 gap-3">
              <Button size="lg" variant="outline" class="h-14 text-base" :disabled="!live || !!calRunning || !!prep.phase" @click="calibrate('baseline')">
                Calibrate rest
              </Button>
              <Button size="lg" variant="outline" class="h-14 text-base" :disabled="!live || !!calRunning || !!prep.phase || !f?.has_baseline" @click="calibrate('mvc')">
                Calibrate max effort
              </Button>
            </div>

            <div v-if="prep.phase" class="flex items-center justify-between rounded-md border bg-amber-50 px-4 py-3">
              <div>
                <div class="text-sm text-amber-900">{{ prep.phase === 'baseline' ? 'Keep the muscle relaxed' : 'Get ready for maximum effort' }}</div>
                <div class="text-3xl font-semibold tabular-nums">Starting in {{ prep.left }}</div>
              </div>
              <Button size="sm" variant="ghost" @click="cancelPrep">Cancel</Button>
            </div>

            <div v-else-if="calRunning" class="rounded-md border px-4 py-3">
              <div class="flex items-baseline justify-between">
                <div class="text-sm font-medium">
                  {{ calRunning.phase === 'baseline' ? 'Rest: stay relaxed' : 'Max effort: contract as hard as possible' }}
                </div>
                <div class="text-3xl font-semibold tabular-nums">{{ (calRunning.remaining_ms / 1000).toFixed(1) }} s</div>
              </div>
              <div class="mt-2 h-2 overflow-hidden rounded-full bg-muted">
                <div class="h-full bg-foreground transition-[width] duration-100" :style="{ width: `${calProgress}%` }" />
              </div>
            </div>

            <div v-if="lastCalEvent" class="text-sm" :class="lastCalEvent.ok ? 'text-muted-foreground' : 'text-red-700'">
              <span class="font-medium">{{ lastCalEvent.event === 'calibrate_baseline' ? 'Rest' : 'Max effort' }}:</span>
              {{ lastCalEvent.msg }}
            </div>
          </CardContent>
        </Card>

        <!-- Settings -->
        <Card>
          <CardHeader>
            <CardTitle>Signal settings</CardTitle>
            <CardDescription>Applied on the Pico and saved to flash.</CardDescription>
          </CardHeader>
          <CardContent class="space-y-5">
            <div class="space-y-2">
              <div class="flex justify-between text-sm"><Label>Smoothing window</Label><span class="tabular-nums">{{ draft.window_ms }} ms</span></div>
              <Slider :model-value="[draft.window_ms]" :min="LIMITS.window_ms.min" :max="LIMITS.window_ms.max" :step="LIMITS.window_ms.step" @update:model-value="(v) => setDraft('window_ms', v)" />
            </div>
            <div class="space-y-2">
              <div class="flex justify-between text-sm"><Label>Gain</Label><span class="tabular-nums">× {{ draft.gain.toFixed(2) }}</span></div>
              <Slider :model-value="[draft.gain]" :min="LIMITS.gain.min" :max="LIMITS.gain.max" :step="LIMITS.gain.step" @update:model-value="(v) => setDraft('gain', v)" />
            </div>
            <div class="space-y-2">
              <div class="flex justify-between text-sm"><Label>Deadband</Label><span class="tabular-nums">{{ (draft.deadband * 100).toFixed(0) }} %</span></div>
              <Slider :model-value="[draft.deadband]" :min="LIMITS.deadband.min" :max="LIMITS.deadband.max" :step="LIMITS.deadband.step" @update:model-value="(v) => setDraft('deadband', v)" />
            </div>
            <div class="flex gap-2">
              <Button :disabled="!live || !draftDirty" @click="applySettings">Apply</Button>
              <Button variant="ghost" :disabled="!draftDirty" @click="revertSettings">Revert</Button>
              <Button variant="ghost" class="ml-auto" :disabled="!live" @click="sock.send({ cmd: 'reset_settings' })">Defaults</Button>
            </div>
          </CardContent>
        </Card>
      </section>

      <!-- Recording -->
      <Card>
        <CardHeader class="flex flex-row flex-wrap items-center gap-3">
          <div class="mr-auto">
            <CardTitle>Research log</CardTitle>
            <CardDescription>CSV: timestamp, raw, envelope, activation (+ baseline, MVC, status), one row per raw sample.</CardDescription>
          </div>
          <Badge v-if="rec.recording.value" variant="destructive" class="h-6 gap-1.5 px-2.5 text-sm">
            <span class="size-2 animate-pulse rounded-full bg-red-600" /> REC
          </Badge>
        </CardHeader>
        <CardContent class="flex flex-wrap items-center gap-3">
          <Input v-model="label" placeholder="Session label (e.g. P03-left-quad)" class="h-10 w-64" />
          <Button v-if="!rec.recording.value" size="lg" :disabled="!live" @click="rec.start()">
            <Play class="size-4" /> {{ rec.rows.value ? 'Resume' : 'Record' }}
          </Button>
          <Button v-else size="lg" variant="destructive" @click="rec.stop()"><Square class="size-4" /> Stop</Button>
          <Button size="lg" variant="outline" :disabled="!rec.rows.value || rec.recording.value" @click="rec.exportCsv(label)">
            <Download class="size-4" /> Export CSV
          </Button>
          <Button size="lg" variant="ghost" :disabled="!rec.rows.value || rec.recording.value" @click="rec.clear()">
            <Trash2 class="size-4" /> Clear
          </Button>
          <div class="ml-auto text-sm tabular-nums text-muted-foreground">
            {{ elapsed }} · {{ rec.rows.value.toLocaleString() }} rows
          </div>
        </CardContent>
      </Card>

      <!-- Events -->
      <Card v-if="sock.events.value.length">
        <CardHeader><CardTitle class="text-sm">Pico messages</CardTitle></CardHeader>
        <CardContent>
          <ul class="space-y-1 text-sm">
            <li v-for="(e, i) in sock.events.value" :key="i" class="flex gap-3">
              <span class="tabular-nums text-muted-foreground">{{ e.at.toLocaleTimeString() }}</span>
              <span :class="e.ok ? '' : 'text-red-700'">{{ e.event }}: {{ e.msg }}</span>
            </li>
          </ul>
        </CardContent>
      </Card>
    </main>
  </div>
</template>
