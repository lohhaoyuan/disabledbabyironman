#include "emg.h"
#include <math.h>
#include <algorithm>

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void EmgProcessor::begin(const EmgConfig& cfg, const EmgCalibration& cal, CalDoneFn cb) {
  cal_ = cal;
  cb_  = cb;
  setConfig(cfg);
}

void EmgProcessor::setConfig(const EmgConfig& cfg) {
  const uint16_t old_window = cfg_.window_ms;
  cfg_.window_ms     = constrain(cfg.window_ms, centad::MIN_WINDOW_MS, centad::MAX_WINDOW_MS);
  cfg_.gain          = clampf(cfg.gain, 0.1f, 10.0f);
  cfg_.deadband      = clampf(cfg.deadband, 0.0f, 0.5f);
  cfg_.auto_mvc      = cfg.auto_mvc;
  cfg_.auto_window_s = constrain(cfg.auto_window_s, centad::MIN_AUTO_WINDOW_S, centad::MAX_AUTO_WINDOW_S);
  cfg_.auto_floor_x  = clampf(cfg.auto_floor_x, centad::MIN_AUTO_FLOOR_X, centad::MAX_AUTO_FLOOR_X);
  if (cfg_.window_ms != old_window || win_n_ == 0) resetWindow();
  updateAuto();   // window / floor may have changed: recompute the reference now
}

void EmgProcessor::resetAuto() {
  auto_i_ = auto_n_ = 0;
  sec_samples_ = 0;
  sec_max_ = 0;
  updateAuto();
}

float EmgProcessor::autoFloor() const {
  return fmaxf(cal_.rest_env * cfg_.auto_floor_x, cal_.rest_env + centad::AUTO_FLOOR_MIN_COUNTS);
}

float EmgProcessor::reference() const {
  return cfg_.auto_mvc ? auto_ref_ : cal_.mvc;
}

// Recompute the auto reference from the stored per-second peaks: the
// AUTO_PERCENTILE of the newest auto_window_s entries, never below the floor.
// 90th percentile of per-second peaks means one stray spike (1 s) cannot set
// it, while ~6 s of real effort per minute (at 60 s) can.
void EmgProcessor::updateAuto() {
  const uint32_t n = std::min<uint32_t>(auto_n_, cfg_.auto_window_s);
  const float floor = autoFloor();
  if (n < centad::AUTO_MIN_HISTORY_S) { auto_ref_ = floor; return; }
  static float tmp[AUTO_CAP];
  for (uint32_t k = 0; k < n; k++) tmp[k] = auto_peaks_[(auto_i_ + AUTO_CAP - 1 - k) % AUTO_CAP];
  // Always skip at least the single highest second, so one spike can never set
  // the reference even while history is still short.
  const uint32_t idx = std::min<uint32_t>((uint32_t)(centad::AUTO_PERCENTILE * (float)(n - 1)), n - 2);
  std::nth_element(tmp, tmp + idx, tmp + n);
  auto_ref_ = fmaxf(tmp[idx], floor);
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
  if (!cal_.has_baseline || (!cfg_.auto_mvc && !cal_.has_mvc)) return EmgStatus::Uncalibrated;
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

  // auto reference: collect one envelope peak per second, only from good data
  if (status() == EmgStatus::Ok) {
    if (env_ > sec_max_) sec_max_ = env_;
    if (++sec_samples_ >= FS) {
      auto_peaks_[auto_i_] = sec_max_;
      auto_i_ = (auto_i_ + 1) % AUTO_CAP;
      if (auto_n_ < AUTO_CAP) auto_n_++;
      sec_samples_ = 0;
      sec_max_ = 0;
      updateAuto();
    }
  }
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
      resetAuto();              // new rest level / electrode position: relearn the max
      char msg[96];
      if (cal_.has_mvc && cal_.mvc <= cal_.rest_env * 1.5f) {
        snprintf(msg, sizeof msg, "baseline %.0f, rest %.1f; MVC now too close to rest, redo max effort",
                 cal_.baseline, cal_.rest_env);
        cal_.has_mvc = false;
      } else {
        snprintf(msg, sizeof msg, "baseline %.0f counts, rest envelope %.1f%s", cal_.baseline, cal_.rest_env,
                 cfg_.auto_mvc ? "; auto max learning from now" : "");
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
  const float span = reference() - cal_.rest_env;
  float a = span > 0 ? cfg_.gain * (env_ - cal_.rest_env) / span : 0;
  a = clampf(a, 0, 1);
  const float db = cfg_.deadband;
  a = (a <= db) ? 0 : (a - db) / (1.0f - db);
  act_ = clampf(a, 0, 1);
}
