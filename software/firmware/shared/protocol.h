#pragma once
#include <stdint.h>
// =============================================================================
//  CENTAD shared constants. Keep in sync with panel/src/lib/protocol.ts and the
//  display bridge (display-patch/).
// =============================================================================
//
//  Pico -> clients (JSON text frame, ~20 Hz):
//    {"t":ms, "raw":[...counts...], "env":float, "act":0..1,
//     "baseline":int, "mvc":float,
//     "status":"ok|calibrating|sensor_fault|uncalibrated",
//     "cal":{"phase":"baseline|mvc","remaining_ms":int},   // only while calibrating
//     "cfg":{"window_ms":int,"gain":float,"deadband":float},
//     "raw_hz":int}                                        // sample rate of raw[]
//
//  Clients -> Pico (JSON text frame):
//    {"cmd":"calibrate_baseline"}
//    {"cmd":"calibrate_mvc"}
//    {"cmd":"set","window_ms":150,"gain":1.0,"deadband":0.05}   // any subset
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

}  // namespace centad
