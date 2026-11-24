# lm05-gameplay: WebSocket sensor bridge

`0001-Add-opt-in-WebSocket-sensor-bridge-ws-for-CENTAD-EMG.patch` applies to
[lohhaoyuan/lm05-gameplay](https://github.com/lohhaoyuan/lm05-gameplay) `main` (438bcf6).
The same commit is on the local branch **`centad-ws-bridge`** in your clone at
`~/Developer/lm05-gameplay` (not pushed).

Review / apply:

```bash
git -C ~/Developer/lm05-gameplay log -p main..centad-ws-bridge
```

```bash
git -C ~/Developer/lm05-gameplay am /path/to/software/display-patch/0001-*.patch
```

## What changes

* `index.html`: new section **9b. SENSOR BRIDGE**. Inert unless the URL has `?ws=`.
  * `?ws=ws://centad-emg.local:81` or `?ws=auto` (= `ws://<page host>:81`).
  * Every message with a numeric `act` → `window.setDrive(act)`; other messages are ignored.
  * No data for more than 1 s → drive ramps to 0 over 0.5 s (the existing smoothing then eases the dog to a stop).
  * Auto-reconnect with 0.5 → 5 s back-off, and immediately when the tab becomes visible again.
  * `?debug=1` overlay gains `ws` state + rate, URL, last `act` and the Pico's sensor status.
  * On an HTTPS page `new WebSocket('ws://…')` throws; the bridge catches it and shows `ws blocked`.
  * `Bridge.update(dt)` is called once per frame, between debug input and the drive filter.
* `sw.js`: cache name `centad-v1` → `centad-v2`, so existing Vercel installs pick up the new `index.html`.
* `README.md`: documents the bridge and the mixed-content limitation.

Without `?ws=`, nothing runs and behaviour is unchanged (checked).
