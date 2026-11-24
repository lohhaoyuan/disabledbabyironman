# CENTAD: EMG reward display + BLDC bench rig

EMG-driven reward display and BLDC bench rig for the CENTAD infant crawling-rehab device.

```
 Grove EMG ──GP27──► Pico 2 W ──Wi-Fi──┬──► iPad: reward display   http://centad-emg.local/
 (on the child)      1 kHz ADC         │        setDrive(act) at 20 Hz
                     envelope → act    └──► laptop/iPad: clinician panel  http://centad-emg.local/panel/
                     WebSocket :81                live chart · calibration · settings · CSV log

 MKS ESP32 FOC Mega ──► GM4108H-120T   (separate, USB-serial bench test, not linked to EMG yet)
```

## Layout

```
software/
├─ platformio.ini            one PlatformIO project: envs foc_mega, pico
├─ firmware/
│  ├─ foc_mega/  pins.h main.cpp           ESP32 FOC Mega open-loop bench test
│  ├─ pico/      pins.h main.cpp emg.* net.* settings.* secrets.example.h
│  └─ shared/    protocol.h                message format + constants (mirrored in panel/src/lib/protocol.ts)
├─ panel/                    Vue 3 + Vite + TS + shadcn-vue clinician panel (+ scripts/mock-pico.mjs)
├─ display-patch/            patch for lm05-gameplay (WebSocket bridge)
├─ scripts/build_pico_data.sh  builds data/ = display + panel for the Pico's flash
└─ data/                     generated, gitignored
```

Each env compiles only its own folder (`build_src_filter`). **All pin assignments live in `firmware/<board>/pins.h`.**

---

## 1. Wiring

### Pico 2 W ← Seeed Grove EMG Detector (`firmware/pico/pins.h`)

| Grove wire | EMG pin | Pico 2 W pin |
|---|---|---|
| Yellow | SIG | **GP27 / ADC1 / A1** (physical pin 32) |
| White | NC | not connected |
| Red | VCC | **3V3(OUT)** (physical pin 36) |
| Black | GND | **AGND** (physical pin 33) |

* **Power the EMG board from 3V3**, as planned. Don't move it to VBUS/VSYS (5 V): its output swings
  toward its own supply and the Pico ADC must never see more than 3.3 V.
* Use AGND for lower ADC noise. Power the Pico from USB (a battery bank is fine; a laptop on
  mains is a noise source, so prefer battery when recording).
* Electrodes: two on the muscle belly in line with the fibres, reference electrode on bone.

Onboard LED: **solid** = on your Wi-Fi · **1 Hz blink** = running its own AP · **2 Hz** =
calibrating · **4 Hz** = sensor fault.

### MKS ESP32 FOC Mega → GM4108H-120T (`firmware/foc_mega/pins.h`)

| Board | Connect to |
|---|---|
| Power in + / − | 16 V bench supply (start with ~1 A current limit) |
| Motor **M0** A / B / C | the 3 motor phase wires (any order; swap two to reverse) |
| USB | computer (flash + serial @115200) |

Driver pins (verified): PWM 32 / 33 / 25, enable 12. No encoder or current sense is used yet;
their pins are `-1` placeholders in `pins.h` for the closed-loop TODO.

---

## 2. Build & flash

Install PlatformIO (VS Code extension or `pip install platformio`). From `software/`:

### FOC Mega

```bash
pio run -e foc_mega -t upload
```

```bash
pio device monitor -e foc_mega
```

Serial commands (type, then Enter): `T1` target 1 rad/s (±5 max) · `V3` voltage limit 3 V (0–6) ·
`S` stop + disable · `E` enable · `?` help. A status line prints every 500 ms.
**Bench only:** no encoder, no torque cap, no resistance detection. Open loop heats the motor;
keep `V` low and check the can temperature.

### Pico 2 W

1. Wi-Fi credentials:
   ```bash
   cp firmware/pico/secrets.example.h firmware/pico/secrets.h
   ```
   then edit `WIFI_SSID` / `WIFI_PASSWORD`. Without it the Pico simply runs its own AP.
2. Firmware (first time: hold **BOOTSEL** while plugging in USB):
   ```bash
   pio run -e pico -t upload
   ```
3. Web files (display + panel) into the Pico's flash:
   ```bash
   ./scripts/build_pico_data.sh
   ```
   ```bash
   pio run -e pico -t uploadfs
   ```
   The display comes from your local clone `~/Developer/lm05-gameplay` (override with
   `GAMEPLAY_DIR=...`): its `centad-ws-bridge` branch if present, otherwise its current files with
   `display-patch/` applied. With no local clone it uses GitHub `main` + the patch. Needs Node 22+
   for the panel build.
4. Watch it boot:
   ```bash
   pio device monitor -e pico
   ```
   It prints the IP, the URLs, and a 1 Hz status line. Serial keys: `b` calibrate rest, `m` calibrate
   max effort, `p` print settings, `r` reset settings, `x` erase settings + calibration.

Settings and calibration are stored in the Pico's EEPROM-emulation flash sector, **separate from
LittleFS**, so re-running `uploadfs` (or re-flashing firmware) does not wipe calibration.

---

## 3. Wi-Fi

1. **Station mode:** joins the network in `secrets.h` (15 s timeout) and keeps retrying if dropped.
2. **Fallback AP:** if that fails, it creates **`CENTAD-EMG`**, password **`centad-rehab`**, at
   **192.168.4.1**. Join it from the iPad/laptop.
3. **mDNS:** `centad-emg.local` is advertised in both modes. iPadOS and macOS resolve `.local` natively.
   Windows needs Bonjour, and Android browsers often don't resolve it, so use the IP printed on serial
   (`http://<ip>/`) there.

Note: in fallback-AP mode the iPad has no internet; everything it needs is served by the Pico.

---

## 4. Opening the display and the panel together

Both are WebSocket clients of the Pico (port 81) at the same time.

| What | Where | URL |
|---|---|---|
| Reward display | iPad, full screen (add to home screen) | `http://centad-emg.local/` (redirects to `/?ws=auto`) |
| Display + debug overlay | anywhere | `http://centad-emg.local/?ws=auto&debug=1` |
| Clinician panel | laptop or second iPad | `http://centad-emg.local/panel/` |
| Panel, dev server | laptop | `cd panel && npm run dev` → `http://localhost:5173` (address field defaults to `ws://centad-emg.local:81`) |

The panel's address field is editable and remembered; you can also pass `?ws=ws://192.168.4.1:81`.

### Why the display is served by the Pico (mixed content)

The Vercel deployment is **HTTPS**. Browsers, including iPad Safari, refuse to open a plain
`ws://` socket from an `https://` page ("mixed content") and there is no user override on iOS.
The alternatives were:

* **`wss://` on the Pico:** needs a TLS certificate the iPad trusts. A self-signed cert has to be
  installed and trusted manually on every iPad (Settings → General → About → Certificate Trust),
  breaks when the IP changes, and TLS on the Pico costs RAM and latency. Fragile.
* **A laptop relay / local web server:** works, but adds a device that has to be running and on
  the same network.
* **Pico serves the display itself over HTTP (chosen):** page and socket are both plain HTTP on the
  LAN, so nothing is blocked; it works identically on home Wi-Fi and in fallback-AP mode, needs no
  internet, no certificates, and no extra device. The display is ~120 KB, the panel ~85 KB gzipped,
  together ~250 KB of the 1 MB LittleFS partition.

The Vercel site is unchanged without `?ws=`, so it stays the sensor-less demo. If someone opens it
with `?ws=`, the debug overlay says `ws blocked` instead of failing silently.

One side effect: service workers only run on HTTPS (or localhost), so the Pico-served display has
no offline cache. That doesn't matter, because it is loaded from the Pico on the same network anyway.

---

## 5. Calibration procedure

Do this with the electrodes on and the child settled. Use the panel (or serial `b` / `m`).

1. **Check the signal.** Status badge should be *Needs calibration* (not *Sensor fault*). Turn on
   *Show raw*: there should be visible noise; a dead-flat line means electrode/cable problem.
2. **Calibrate rest** (5 s, after a 3-2-1 countdown). Keep the limb relaxed and still. The first 3 s
   give the **baseline** (mean ADC level), the last 2 s the **rest envelope** (noise floor).
3. **Calibrate max effort** (4 s). Elicit the strongest contraction you can. The peak envelope
   becomes the **MVC**. If the peak is less than 2× the rest level it is rejected and the old
   value is kept.
4. **Tune** in *Signal settings* and press *Apply* (saved on the Pico):
   * **Smoothing window** (50–500 ms, default 150): longer = steadier but laggier.
   * **Gain** (default 1): >1 means less effort is needed for full drive (useful when the MVC
     captured was an over-estimate).
   * **Deadband** (default 5 %): activation below this is 0, so resting noise never moves the dog.

Activation = `clamp(gain · (envelope − rest) / (MVC − rest), 0, 1)`, then the deadband. It is forced
to 0 while calibrating, before both calibrations exist, and during a sensor fault (signal flat or
pinned at a rail for more than 500 ms).

Re-calibrating rest invalidates the MVC if it ends up too close to the new rest level (you'll be
told to redo max effort).

## 6. Research log

Panel → *Research log*: Record / Stop / Export CSV. One row per raw sample (500 Hz):
`timestamp_ms` (Pico clock), `raw` (ADC counts), `envelope`, `activation`, `baseline`, `mvc`, `status`.
Envelope/activation update at 20 Hz, so they repeat across the 25 raw rows of each frame. Data is
kept in the browser tab until exported; closing or reloading the tab loses an unexported session.

## 7. Developing without hardware

```bash
cd panel && npm install
```
```bash
npm run mock
```
```bash
npm run dev
```

Open `http://localhost:5173/?ws=ws://localhost:8181`. The mock speaks the same protocol with
synthetic EMG bursts. For the display, serve the patched lm05-gameplay folder and open
`/?ws=ws://localhost:8181&debug=1`.

## 8. WebSocket protocol

See `firmware/shared/protocol.h`. Pico → clients at 20 Hz:

```json
{"t":123456,"raw":[2010,2013,...],"raw_hz":500,"env":12.3,"act":0.42,"baseline":2010,"rest":3.1,
 "mvc":110.5,"has_baseline":true,"has_mvc":true,"status":"ok",
 "cfg":{"window_ms":150,"gain":1.0,"deadband":0.05},"lost":0}
```

plus `"cal":{"phase":"mvc","remaining_ms":2300}` while calibrating, and one-off events
`{"event":"calibrate_mvc","ok":true,"msg":"..."}`. Clients send
`{"cmd":"calibrate_baseline"}`, `{"cmd":"calibrate_mvc"}`,
`{"cmd":"set","window_ms":150,"gain":1,"deadband":0.05}` (any subset), `{"cmd":"reset_settings"}`.
`status` adds `uncalibrated` to the requested `ok|calibrating|sensor_fault`.

---

## 9. Assumptions & what is not verified on hardware

Verified here: `foc_mega` and `pico` build; `npm run build` builds the panel; the EMG
signal chain was run on the host against synthetic data (rest → 0, scaling, flat/saturation fault,
rejected MVC); the panel and the patched display were tested together against the mock (live chart,
both calibrations with countdown, settings, CSV export, display follows `act`, display ramps to 0
within 1 s when the link dies, both reconnect automatically, display unchanged without `?ws=`).

**Not verified (needs the hardware):**

* Anything on a real Pico 2 W: Wi-Fi join/AP fallback, mDNS, HTTP serving from LittleFS, the
  WebSockets library on RP2350, actual sample timing/jitter, and that flash writes only cause a
  short sampling gap (counted in `lost`).
* The Grove EMG's real output range/noise. Thresholds that may need tuning: fault flat span
  (6 counts p-p), saturation rails (8 / 4087), MVC rejection (2× rest), deadband default.
* The new FOC firmware on the Mega. It keeps your verified config and init order, but the
  Commander wiring and status line haven't run on the board.
* iPad Safari specifically (home-screen launch, reconnect after sleep).

**Assumptions / deviations from the brief:**

* `software/` is a fresh PlatformIO project (`src_dir = firmware`) that replaces the old one
  (Teensy/STM32 cuff + Pico IMU sketch); the old code is still in git history (commit 846276c).
* The EMG firmware is written from scratch (no earlier EMG code to start from).
* The Grove EMG is powered from 3V3 (confirmed).
* Sampling is 1 kHz on core 1 (deadline-paced on the µs timer); `raw[]` is decimated to 500 Hz to
  keep frames small. Change `RAW_DECIMATE` in `protocol.h` for full rate.
* Settings use EEPROM emulation rather than a LittleFS file, so `uploadfs` can't wipe calibration.
* The panel's "drive sent to display" is `act` as broadcast by the Pico; the display smooths it
  further internally (τ = 90 ms) and does not report back.
* The bridge never calls `setContingency()`; contingency stays under manual/debug control.
* MVC in infants: a max *voluntary* contraction on command isn't realistic for a baby. Treat "max
  effort" as the strongest contraction you can elicit during the session, and use gain to adjust.
  How to standardise this is a protocol question for the KKH team.
