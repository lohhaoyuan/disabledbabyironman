// =============================================================================
//  CENTAD EMG node: Raspberry Pi Pico 2 W + Seeed Grove EMG Detector
// =============================================================================
//
//  Core 1: samples the EMG on GP27 at exactly SAMPLE_RATE_HZ (deadline-paced on
//          the hardware microsecond timer, never blocked by Wi-Fi) and pushes
//          samples into a lock-free single-producer/single-consumer ring.
//  Core 0: drains the ring through the signal chain (emg.h), serves HTTP +
//          WebSocket, and broadcasts a JSON frame every 1/BROADCAST_HZ s.
//
//  Serial (115200) debug commands: b = calibrate rest, m = calibrate max effort
//  (manual mode), a = forget learned auto max, p = print settings,
//  r = reset settings to defaults, x = erase everything.
//  Pins: see pins.h.  Message format: see ../shared/protocol.h.
// =============================================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <atomic>
#include <algorithm>
#include "hardware/adc.h"

#include "pins.h"
#include "../shared/protocol.h"
#include "emg.h"
#include "settings.h"
#include "net.h"

using namespace centad;

// ---------------------------------------------------------------------------
// Core 1 -> core 0 sample ring (SPSC, power-of-two size, ~2 s of headroom)
// ---------------------------------------------------------------------------
namespace ring {
constexpr uint32_t SIZE = 2048;
uint16_t               buf[SIZE];
std::atomic<uint32_t>  head{0};     // written by core 1 only
std::atomic<uint32_t>  tail{0};     // written by core 0 only
std::atomic<uint32_t>  overflow{0}; // samples lost because core 0 fell >2 s behind
std::atomic<uint32_t>  resync{0};   // sampler stalls (e.g. flash write) that skipped samples
std::atomic<uint32_t>  last_ms{0};  // millis() of the newest sample
}

// ---------------------------------------------------------------------------
// Core 0 state
// ---------------------------------------------------------------------------
EmgProcessor emg;
constexpr uint32_t RAW_HZ  = SAMPLE_RATE_HZ / RAW_DECIMATE;
constexpr uint32_t RAW_CAP = 128;
uint16_t raw_buf[RAW_CAP];
uint32_t raw_n = 0, raw_decim = 0;
uint32_t raw_dropped = 0;

const char* statusStr(EmgStatus s) {
  switch (s) {
    case EmgStatus::Ok:           return "ok";
    case EmgStatus::Calibrating:  return "calibrating";
    case EmgStatus::SensorFault:  return "sensor_fault";
    case EmgStatus::Uncalibrated: return "uncalibrated";
  }
  return "?";
}
const char* phaseStr(CalPhase p) {
  return p == CalPhase::Baseline ? "baseline" : p == CalPhase::Mvc ? "mvc" : "none";
}

// ---------------------------------------------------------------------------
// Outgoing messages
// ---------------------------------------------------------------------------
static char frame[1792];

// Telemetry frame (see protocol.h). Built by hand: fast, no heap churn.
void sendFrame() {
  const auto& c = emg.calibration();
  const auto& g = emg.config();
  int n = snprintf(frame, sizeof frame, "{\"t\":%lu,\"raw\":[", (unsigned long)ring::last_ms.load());
  for (uint32_t i = 0; i < raw_n && n < (int)sizeof frame - 400; i++)
    n += snprintf(frame + n, sizeof frame - n, i ? ",%u" : "%u", raw_buf[i]);
  n += snprintf(frame + n, sizeof frame - n,
                "],\"raw_hz\":%lu,\"env\":%.2f,\"act\":%.4f,\"baseline\":%ld,\"rest\":%.2f,\"mvc\":%.2f,"
                "\"mvc_mode\":\"%s\",\"mvc_manual\":%.2f,\"auto_hist_s\":%lu,"
                "\"has_baseline\":%s,\"has_mvc\":%s,\"status\":\"%s\","
                "\"cfg\":{\"window_ms\":%u,\"gain\":%.3f,\"deadband\":%.3f,"
                "\"auto_mvc\":%s,\"auto_window_s\":%u,\"auto_floor_x\":%.2f}",
                (unsigned long)RAW_HZ, emg.envelope(), emg.activation(), lroundf(c.baseline), c.rest_env,
                emg.reference(), g.auto_mvc ? "auto" : "manual", c.mvc,
                (unsigned long)std::min<uint32_t>(emg.autoHistorySeconds(), g.auto_window_s),
                c.has_baseline ? "true" : "false", c.has_mvc ? "true" : "false",
                statusStr(emg.status()), g.window_ms, g.gain, g.deadband,
                g.auto_mvc ? "true" : "false", g.auto_window_s, g.auto_floor_x);
  if (emg.calPhase() != CalPhase::None)
    n += snprintf(frame + n, sizeof frame - n, ",\"cal\":{\"phase\":\"%s\",\"remaining_ms\":%lu}",
                  phaseStr(emg.calPhase()), (unsigned long)emg.calRemainingMs());
  n += snprintf(frame + n, sizeof frame - n, ",\"lost\":%lu}",
                (unsigned long)(ring::overflow.load() + ring::resync.load() + raw_dropped));
  net::broadcast(frame, n);
  raw_n = 0;
}

// Event message: {"event":"<name>","ok":bool,"msg":"..."}. Clients that only
// care about telemetry (the display) ignore anything without "act".
void sendEvent(const char* name, bool ok, const char* msg, int client = -1) {
  JsonDocument d;
  d["event"] = name;
  d["ok"] = ok;
  d["msg"] = msg;
  char out[256];
  size_t n = serializeJson(d, out, sizeof out);
  if (client >= 0) net::sendTo((uint8_t)client, out, n);
  else net::broadcast(out, n);
  Serial.printf("[event] %s ok=%d %s\n", name, ok, msg);
}

void onCalibrationDone(CalPhase phase, bool ok, const char* msg) {
  if (ok) settings::save(emg.config(), emg.calibration());
  sendEvent(phase == CalPhase::Baseline ? "calibrate_baseline" : "calibrate_mvc", ok, msg);
}

// ---------------------------------------------------------------------------
// Incoming commands
// ---------------------------------------------------------------------------
void startCal(bool baseline, int client) {
  String why;
  const bool ok = baseline ? emg.startBaseline(why) : emg.startMvc(why);
  sendEvent(baseline ? "calibrate_baseline" : "calibrate_mvc", ok, why.c_str(), ok ? -1 : client);
}

void handleCommand(int client, const char* text, size_t len) {
  JsonDocument d;
  if (deserializeJson(d, text, len)) { sendEvent("error", false, "bad JSON", client); return; }
  const char* cmd = d["cmd"] | "";

  if (!strcmp(cmd, "calibrate_baseline")) startCal(true, client);
  else if (!strcmp(cmd, "calibrate_mvc")) startCal(false, client);
  else if (!strcmp(cmd, "set")) {
    EmgConfig cfg = emg.config();
    if (d["window_ms"].is<int>())   cfg.window_ms = d["window_ms"].as<int>();
    if (d["gain"].is<float>())      cfg.gain      = d["gain"].as<float>();
    if (d["deadband"].is<float>())  cfg.deadband  = d["deadband"].as<float>();
    if (d["auto_mvc"].is<bool>())   cfg.auto_mvc  = d["auto_mvc"].as<bool>();
    if (d["auto_window_s"].is<int>()) cfg.auto_window_s = d["auto_window_s"].as<int>();
    if (d["auto_floor_x"].is<float>()) cfg.auto_floor_x = d["auto_floor_x"].as<float>();
    emg.setConfig(cfg);
    settings::save(emg.config(), emg.calibration());
    const auto& g = emg.config();
    char msg[160];
    snprintf(msg, sizeof msg, "window %u ms, gain %.2f, deadband %.2f, max %s (%u s, floor %.1fx rest)",
             g.window_ms, g.gain, g.deadband, g.auto_mvc ? "auto" : "manual", g.auto_window_s, g.auto_floor_x);
    sendEvent("set", true, msg);
  } else if (!strcmp(cmd, "reset_auto")) {
    emg.resetAuto();
    sendEvent("reset_auto", true, "learned max cleared; relearning from now");
  } else if (!strcmp(cmd, "reset_settings")) {
    emg.setConfig(EmgConfig{});
    settings::save(emg.config(), emg.calibration());
    sendEvent("set", true, "settings reset to defaults (calibration kept)");
  } else {
    sendEvent("error", false, "unknown cmd", client);
  }
}

void onWsText(uint8_t client, const char* text, size_t len) { handleCommand(client, text, len); }
void onWsConnect(uint8_t) { sendFrame(); }   // give new clients state immediately

void handleSerial() {
  if (!Serial.available()) return;
  switch (Serial.read()) {
    case 'b': startCal(true, -1); break;
    case 'm': startCal(false, -1); break;
    case 'r': handleCommand(-1, "{\"cmd\":\"reset_settings\"}", 24); break;
    case 'a': handleCommand(-1, "{\"cmd\":\"reset_auto\"}", 19); break;
    case 'x':
      settings::erase();
      emg.begin(EmgConfig{}, EmgCalibration{}, onCalibrationDone);
      Serial.println("settings + calibration erased");
      break;
    case 'p': {
      const auto& c = emg.calibration();
      const auto& g = emg.config();
      Serial.printf("cfg: window=%u ms gain=%.2f deadband=%.2f max=%s(%u s, floor %.1fx) | cal: baseline=%.1f(%d) "
                    "rest=%.2f manual_mvc=%.2f(%d) | ref in use=%.2f (%lu s history)\n",
                    g.window_ms, g.gain, g.deadband, g.auto_mvc ? "auto" : "manual", g.auto_window_s,
                    g.auto_floor_x, c.baseline, c.has_baseline, c.rest_env, c.mvc, c.has_mvc, emg.reference(),
                    (unsigned long)emg.autoHistorySeconds());
      break;
    }
    default: break;
  }
}

// ---------------------------------------------------------------------------
// Status LED: solid = on home Wi-Fi, 1 Hz = own AP, 4 Hz = sensor fault,
// 2 Hz = calibrating.
// ---------------------------------------------------------------------------
void updateLed() {
  static bool on = false;
  uint32_t period;
  switch (emg.status()) {
    case EmgStatus::SensorFault: period = 250; break;
    case EmgStatus::Calibrating: period = 500; break;
    default: period = net::mode() == net::Mode::Station ? 0 : 1000;
  }
  const bool want = period == 0 ? true : (millis() % period) < period / 2;
  if (want != on) { on = want; digitalWrite(PIN_STATUS_LED, on); }
}

// ===========================================================================
// Core 0
// ===========================================================================
void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  Serial.println("\n=== CENTAD EMG node (Pico 2 W) ===");
  pinMode(PIN_STATUS_LED, OUTPUT);

  EmgConfig cfg;
  EmgCalibration cal;
  const bool loaded = settings::load(cfg, cal);
  Serial.printf("[settings] %s\n", loaded ? "loaded from flash" : "none saved, using defaults");
  emg.begin(cfg, cal, onCalibrationDone);

  net::begin(onWsText, onWsConnect);
}

void loop() {
  // 1. drain samples from core 1 through the signal chain
  uint32_t t = ring::tail.load(std::memory_order_relaxed);
  const uint32_t h = ring::head.load(std::memory_order_acquire);
  while (t != h) {
    const uint16_t x = ring::buf[t & (ring::SIZE - 1)];
    t++;
    emg.push(x);
    if (++raw_decim >= RAW_DECIMATE) {
      raw_decim = 0;
      if (raw_n < RAW_CAP) raw_buf[raw_n++] = x;
      else raw_dropped++;
    }
  }
  ring::tail.store(t, std::memory_order_release);

  // 2. telemetry at BROADCAST_HZ
  static uint32_t last_tx = 0;
  const uint32_t now = millis();
  if (now - last_tx >= 1000 / BROADCAST_HZ) {
    last_tx = now;
    sendFrame();
  }

  // 3. networking, serial, LED
  net::loop();
  handleSerial();
  updateLed();

  static uint32_t last_print = 0;
  if (now - last_print >= 1000) {
    last_print = now;
    Serial.printf("[emg] status=%s env=%.1f ref=%.1f act=%.3f clients=%u ip=%s lost=%lu\n",
                  statusStr(emg.status()), emg.envelope(), emg.reference(), emg.activation(), net::clientCount(),
                  net::ip().toString().c_str(),
                  (unsigned long)(ring::overflow.load() + ring::resync.load() + raw_dropped));
  }
}

// ===========================================================================
// Core 1: fixed-rate sampler. Nothing else runs on this core.
// adc_gpio_init() disables the pad's digital input and pull resistors, which
// is also the workaround for the RP2350-E9 ADC-pin leakage erratum.
// ===========================================================================
static uint64_t next_us;
static constexpr uint32_t PERIOD_US = 1000000 / SAMPLE_RATE_HZ;

void setup1() {
  adc_init();
  adc_gpio_init(PIN_EMG);
  adc_select_input(PIN_EMG - 26);
  next_us = time_us_64();
}

void loop1() {
  next_us += PERIOD_US;
  const int64_t late = (int64_t)(time_us_64() - next_us);
  if (late > (int64_t)PERIOD_US * 5) {
    // We were stalled (core 0 pauses this core during flash writes). Skip the
    // missed slots instead of bursting them out back-to-back.
    ring::resync.fetch_add((uint32_t)(late / PERIOD_US));
    next_us = time_us_64();
  } else {
    while ((int64_t)(next_us - time_us_64()) > 0) tight_loop_contents();
  }

  const uint16_t v = adc_read();
  const uint32_t h = ring::head.load(std::memory_order_relaxed);
  if (h - ring::tail.load(std::memory_order_acquire) >= ring::SIZE) {
    ring::overflow.fetch_add(1);
    return;
  }
  ring::buf[h & (ring::SIZE - 1)] = v;
  ring::last_ms.store(millis(), std::memory_order_relaxed);
  ring::head.store(h + 1, std::memory_order_release);
}
