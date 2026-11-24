#pragma once
#include <stdint.h>
// =============================================================================
//  CENTAD shared constants. Keep in sync with panel/src/lib/protocol.ts and the
//  display bridge (display-patch/).
// =============================================================================
//
//  Pico -> clients (JSON text frame, ~20 Hz):
//    {"t":ms, "raw":[...counts...], "raw_hz":int, "env":float, "act":0..1,
//     "baseline":int, "rest":float,
//     "mvc":float,            // the "100 %" reference in use (auto or manual)
//     "mvc_mode":"auto|manual", "mvc_manual":float, "auto_hist_s":int,
//     "has_baseline":bool, "has_mvc":bool,
//     "status":"ok|calibrating|sensor_fault|uncalibrated",
//     "cal":{"phase":"baseline|mvc","remaining_ms":int},   // only while calibrating
//     "cfg":{"window_ms":int,"gain":float,"deadband":float,
//            "auto_mvc":bool,"auto_window_s":int,"auto_floor_x":float},
//     "lost":int}
//
//  Clients -> Pico (JSON text frame):
//    {"cmd":"calibrate_baseline"}
//    {"cmd":"calibrate_mvc"}                  // only used when auto_mvc is off
//    {"cmd":"set","window_ms":150,"gain":1.0,"deadband":0.05,
//     "auto_mvc":true,"auto_window_s":60,"auto_floor_x":5}     // any subset
//    {"cmd":"reset_auto"}                     // forget the learned max
//    {"cmd":"reset_settings"}
// =============================================================================

namespace centad {

constexpr const char* MDNS_HOST       = "centad-emg";      // -> centad-emg.local
constexpr const char* AP_SSID         = "CENTAD-EMG";
constexpr const char* AP_PASSWORD     = "centad-rehab";    // >= 8 chars
constexpr uint16_t    HTTP_PORT       = 80;
constexpr uint16_t    WS_PORT         = 81;

constexpr uint32_t    SAMPLE_RATE_HZ  = 1000;              // EMG ADC rate
constexpr uint32_t    BROADCAST_HZ    = 20;                // JSON frames / s
constexpr uint32_t    RAW_DECIMATE    = 5;                 // raw[] = every 5th sample (200 Hz)

constexpr uint32_t    BASELINE_CAL_MS = 5000;              // rest calibration
constexpr uint32_t    MVC_CAL_MS      = 4000;              // max-effort calibration
constexpr uint32_t    FAULT_MS        = 500;               // flat/saturated => sensor_fault

// Runtime-adjustable defaults (saved to flash)
constexpr uint16_t    DEF_WINDOW_MS   = 150;
constexpr float       DEF_GAIN        = 1.0f;
constexpr float       DEF_DEADBAND    = 0.05f;
constexpr uint16_t    MIN_WINDOW_MS   = 50;
constexpr uint16_t    MAX_WINDOW_MS   = 500;

// Auto max (no max-effort calibration needed): "100 %" = AUTO_PERCENTILE of the
// per-second envelope peaks over the last auto_window_s seconds, never below
// rest_env * auto_floor_x (and never below rest_env + AUTO_FLOOR_MIN_COUNTS).
constexpr bool        DEF_AUTO_MVC          = true;
constexpr uint16_t    DEF_AUTO_WINDOW_S     = 60;
constexpr uint16_t    MIN_AUTO_WINDOW_S     = 10;
constexpr uint16_t    MAX_AUTO_WINDOW_S     = 300;
constexpr float       DEF_AUTO_FLOOR_X      = 5.0f;
constexpr float       MIN_AUTO_FLOOR_X      = 2.0f;
constexpr float       MAX_AUTO_FLOOR_X      = 20.0f;
constexpr float       AUTO_PERCENTILE       = 0.90f;
constexpr float       AUTO_FLOOR_MIN_COUNTS = 10.0f;
constexpr uint16_t    AUTO_MIN_HISTORY_S    = 5;     // use the floor until this much history exists

}  // namespace centad
