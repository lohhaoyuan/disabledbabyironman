# CENTAD EMG clinician panel

Vue 3 + Vite + TypeScript + Tailwind v4 + shadcn-vue, charts with uPlot.

```bash
npm install
```
```bash
npm run dev      # http://localhost:5173  (connects to ws://centad-emg.local:81 by default)
```
```bash
npm run mock     # fake Pico on ws://localhost:8181; open http://localhost:5173/?ws=ws://localhost:8181
```
```bash
npm run build    # dist/, which ../scripts/build_pico_data.sh copies to the Pico at /panel/
```

* `src/lib/protocol.ts`: message types (mirror of `firmware/shared/protocol.h`)
* `src/composables/useEmgSocket.ts`: WebSocket with auto-reconnect
* `src/composables/useRecorder.ts`: research log + CSV export
* `src/components/LiveCharts.vue`: activation + envelope charts, fixed-size ring buffer

Note: `src/components/ui/switch/Switch.vue` was patched to use `data-[state=checked]` because the
generated `data-checked:` variants don't match what reka-ui 2.11 renders.
