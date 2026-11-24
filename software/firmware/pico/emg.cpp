#include "emg.h"
#include <math.h>

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void EmgProcessor::begin(const EmgConfig& cfg, const EmgCalibration& cal, CalDoneFn cb) {
  cal_ = cal;
  cb_  = cb;
  setConfig(cfg);
}

void EmgProcessor::setConfig(const EmgConfig& cfg) {
  cfg_.window_ms = constrain(cfg.window_ms, centad::MIN_WINDOW_MS, centad::MAX_WINDOW_MS);
  cfg_.gain      = clampf(cfg.gain, 0.1f, 10.0f);
  cfg_.deadband  = clampf(cfg.deadband, 0.0f, 0.5f);
  resetWindow();
}

void EmgProcessor::resetWindow() {
  win_n_ = cfg_.window_ms * FS / 1000;
  if (win_n_ < 1) win_n_ = 1;
  if (win_n_ > MAX_WIN) win_n_ = MAX_WIN;
  win_i_ = win_fill_ = 0;
  sq_sum_ = 0;
  memset(sq_, 0, sizeof(sq_));
}

EmgStatus EmgProcessor::status() const {
  if (phase_ != CalPhase::None) return EmgStatus::Calibrating;
  if (fault_) return EmgStatus::SensorFault;
  if (!cal_.has_baseline || !cal_.has_mvc) return EmgStatus::Uncalibrated;
  return EmgStatus::Ok;
}

uint32_t EmgProcessor::calRemainingMs() const {
  if (phase_ == CalPhase::None || cal_n_ >= cal_total_) return 0;
  return (cal_total_ - cal_n_) * 1000 / FS;
}

bool EmgProcessor::startBaseline(String& why) {
  if (phase_ != CalPhase::None) { why = "calibration already running"; return false; }
  phase_     = CalPhase::Baseline;
  cal_total_ = centad::BASELINE_CAL_MS * FS / 1000;
  cal_n_ = cal_env_n_ = 0;
  cal_sum_ = 0;
  why = fault_ ? "started, but sensor looks faulty: check electrodes" : "started";
  return true;
}

bool EmgProcessor::startMvc(String& why) {
  if (phase_ != CalPhase::None) { why = "calibration already running"; return false; }
  if (!cal_.has_baseline) { why = "calibrate rest (baseline) first"; return false; }
  phase_     = CalPhase::Mvc;
  cal_total_ = centad::MVC_CAL_MS * FS / 1000;
  cal_n_ = 0;
  cal_peak_ = 0;
  why = "started";
  return true;
}

void EmgProcessor::push(uint16_t x) {
  updateFault(x);
  updateEnvelope(x);
  updateCalibration(x);
  updateActivation();
}

// 3. moving RMS of (x - baseline). Before a baseline exists we use the running
//    mid-scale guess (2048) so the chart still shows something sensible.
void EmgProcessor::updateEnvelope(uint16_t x) {
  const int32_t b = cal_.has_baseline ? (int32_t)lroundf(cal_.baseline) : 2048;
  const int32_t r = (int32_t)x - b;
  const int32_t s = r * r;                  // <= 4095^2 fits int32
  sq_sum_ += s - sq_[win_i_];
  sq_[win_i_] = s;
  win_i_ = (win_i_ + 1) % win_n_;
  if (win_fill_ < win_n_) win_fill_++;
  env_ = sqrtf((float)sq_sum_ / (float)win_fill_);
}

void EmgProcessor::updateFault(uint16_t x) {
  if (x < seg_min_) seg_min_ = x;
  if (x > seg_max_) seg_max_ = x;
  if (x <= SAT_LO || x >= SAT_HI) seg_sat_++;
  if (++seg_n_ < FAULT_N) return;
  const bool flat      = (seg_max_ - seg_min_) < FLAT_SPAN;
  const bool saturated = seg_sat_ * 10 > seg_n_ * 9;
  fault_ = flat || saturated;
  seg_n_ = seg_sat_ = 0;
  seg_min_ = 4095;
  seg_max_ = 0;
}

// Baseline cal: first 60 % of the window -> mean (baseline); remaining 40 % ->
// mean envelope around that baseline (rest_env, i.e. the noise floor).
void EmgProcessor::updateCalibration(uint16_t x) {
  if (phase_ == CalPhase::None) return;
  cal_n_++;

  if (phase_ == CalPhase::Baseline) {
    const uint32_t split = cal_total_ * 6 / 10;
    if (cal_n_ <= split) {
      cal_sum_ += x;
      if (cal_n_ == split) {
        cal_.baseline = (float)(cal_sum_ / split);
        cal_.has_baseline = true;
        resetWindow();          // envelope must restart around the new baseline
        cal_sum_ = 0;
      }
    } else if (win_fill_ >= win_n_) {   // only once the RMS window is full again
      cal_sum_ += env_;
      cal_env_n_++;
    }
    if (cal_n_ >= cal_total_) {
      cal_.rest_env = cal_env_n_ ? (float)(cal_sum_ / cal_env_n_) : env_;
      phase_ = CalPhase::None;
      char msg[96];
      if (cal_.has_mvc && cal_.mvc <= cal_.rest_env * 1.5f) {
        snprintf(msg, sizeof msg, "baseline %.0f, rest %.1f; MVC now too close to rest, redo max effort",
                 cal_.baseline, cal_.rest_env);
        cal_.has_mvc = false;
      } else {
        snprintf(msg, sizeof msg, "baseline %.0f counts, rest envelope %.1f", cal_.baseline, cal_.rest_env);
      }
      if (cb_) cb_(CalPhase::Baseline, true, msg);
    }
    return;
  }

  // MVC: peak envelope over the window
  if (env_ > cal_peak_) cal_peak_ = env_;
  if (cal_n_ >= cal_total_) {
    phase_ = CalPhase::None;
    const float min_ok = fmaxf(cal_.rest_env * 2.0f, cal_.rest_env + 5.0f);
    char msg[96];
    if (cal_peak_ < min_ok) {
      snprintf(msg, sizeof msg, "rejected: peak %.1f barely above rest %.1f, no contraction seen",
               cal_peak_, cal_.rest_env);
      if (cb_) cb_(CalPhase::Mvc, false, msg);
    } else {
      cal_.mvc = cal_peak_;
      cal_.has_mvc = true;
      snprintf(msg, sizeof msg, "MVC envelope %.1f (rest %.1f)", cal_.mvc, cal_.rest_env);
      if (cb_) cb_(CalPhase::Mvc, true, msg);
    }
  }
}

void EmgProcessor::updateActivation() {
  if (status() != EmgStatus::Ok) { act_ = 0; return; }
  const float span = cal_.mvc - cal_.rest_env;
  float a = span > 0 ? cfg_.gain * (env_ - cal_.rest_env) / span : 0;
  a = clampf(a, 0, 1);
  const float db = cfg_.deadband;
  a = (a <= db) ? 0 : (a - db) / (1.0f - db);
  act_ = clampf(a, 0, 1);
}
