#pragma once
// =============================================================================
//  EMG signal chain (runs on core 0, fed 1 sample at a time at SAMPLE_RATE_HZ)
// =============================================================================
//
//   raw ADC counts x[n]  (0..4095, sampled at 1 kHz on core 1)
//     │
//     ├─ 1. baseline   b      = mean(x) with the muscle at rest      (calibrate_baseline)
//     ├─ 2. rectify    r[n]   = x[n] − b
//     ├─ 3. envelope   env[n] = sqrt( mean( r² ) over window_ms )    (moving RMS)
//     ├─ 4. normalise  a      = gain · (env − rest_env) / (mvc − rest_env)
//     │                  rest_env = mean envelope at rest (measured during baseline cal)
//     │                  mvc      = peak envelope during max effort  (calibrate_mvc)
//     ├─ 5. deadband   a < deadband → 0, otherwise rescaled so the output is still 0..1
//     └─ 6. clamp      act = clamp(a, 0, 1)
//
//   Sensor fault: every FAULT_MS the min/max/saturation of the raw signal is
//   checked. Peak-to-peak < FLAT_SPAN counts (cable off / module dead) or
//   > 90 % of samples at the rails (saturated) => sensor_fault, act forced to 0.
//
//   act is also forced to 0 while calibrating or until both calibrations exist.
// =============================================================================

#include <Arduino.h>
#include "../shared/protocol.h"

struct EmgConfig {
  uint16_t window_ms = centad::DEF_WINDOW_MS;
  float    gain      = centad::DEF_GAIN;
  float    deadband  = centad::DEF_DEADBAND;
};

struct EmgCalibration {
  bool  has_baseline = false;
  bool  has_mvc      = false;
  float baseline     = 0;   // ADC counts
  float rest_env     = 0;   // envelope counts at rest
  float mvc          = 0;   // envelope counts at max voluntary contraction
};

enum class EmgStatus : uint8_t { Ok, Calibrating, SensorFault, Uncalibrated };
enum class CalPhase  : uint8_t { None, Baseline, Mvc };

class EmgProcessor {
 public:
  // Called when a calibration finishes. ok=false => calibration rejected, old values kept.
  using CalDoneFn = void (*)(CalPhase phase, bool ok, const char* msg);

  void begin(const EmgConfig& cfg, const EmgCalibration& cal, CalDoneFn cb);
  void push(uint16_t x);                    // one raw sample

  void setConfig(const EmgConfig& cfg);
  bool startBaseline(String& why);
  bool startMvc(String& why);

  const EmgConfig&      config() const { return cfg_; }
  const EmgCalibration& calibration() const { return cal_; }
  float     envelope()   const { return env_; }
  float     activation() const { return act_; }
  EmgStatus status()     const;
  CalPhase  calPhase()   const { return phase_; }
  uint32_t  calRemainingMs() const;
  bool      sensorFault() const { return fault_; }

 private:
  static constexpr uint32_t FS        = centad::SAMPLE_RATE_HZ;
  static constexpr uint32_t MAX_WIN   = centad::MAX_WINDOW_MS * FS / 1000;
  static constexpr uint32_t FAULT_N   = centad::FAULT_MS * FS / 1000;
  static constexpr int      FLAT_SPAN = 6;     // counts p-p => "flat"
  static constexpr int      SAT_LO    = 8;
  static constexpr int      SAT_HI    = 4087;

  void resetWindow();
  void updateEnvelope(uint16_t x);
  void updateFault(uint16_t x);
  void updateCalibration(uint16_t x);
  void updateActivation();

  EmgConfig      cfg_;
  EmgCalibration cal_;
  CalDoneFn      cb_ = nullptr;

  // moving RMS (exact integer running sum, no float drift)
  int32_t  sq_[MAX_WIN] = {0};
  uint32_t win_n_ = 0, win_i_ = 0, win_fill_ = 0;
  int64_t  sq_sum_ = 0;
  float    env_ = 0, act_ = 0;

  // sensor fault detection
  uint32_t seg_n_ = 0, seg_sat_ = 0;
  int      seg_min_ = 4095, seg_max_ = 0;
  bool     fault_ = false;

  // calibration
  CalPhase phase_ = CalPhase::None;
  uint32_t cal_n_ = 0, cal_total_ = 0;
  double   cal_sum_ = 0;
  uint32_t cal_env_n_ = 0;
  float    cal_peak_ = 0;
};
