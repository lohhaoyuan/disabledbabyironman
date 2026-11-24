#include "settings.h"
#include <EEPROM.h>

namespace {
constexpr uint32_t MAGIC   = 0xCE7AD001;
constexpr uint16_t VERSION = 2;   // v2: auto max settings (v1 blobs fall back to defaults)

struct __attribute__((packed)) Blob {
  uint32_t magic;
  uint16_t version;
  uint16_t window_ms;
  float    gain;
  float    deadband;
  uint8_t  has_baseline;
  uint8_t  has_mvc;
  float    baseline;
  float    rest_env;
  float    mvc;
  uint8_t  auto_mvc;
  uint16_t auto_window_s;
  float    auto_floor_x;
  uint32_t crc;
};

uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1));
  }
  return ~c;
}

bool started = false;
void ensure() {
  if (!started) { EEPROM.begin(256); started = true; }
}
}  // namespace

namespace settings {

bool load(EmgConfig& cfg, EmgCalibration& cal) {
  ensure();
  Blob b;
  EEPROM.get(0, b);
  if (b.magic != MAGIC || b.version != VERSION ||
      b.crc != crc32((const uint8_t*)&b, offsetof(Blob, crc))) {
    cfg = EmgConfig{};
    cal = EmgCalibration{};
    return false;
  }
  cfg.window_ms    = b.window_ms;
  cfg.gain         = b.gain;
  cfg.deadband     = b.deadband;
  cfg.auto_mvc      = b.auto_mvc;
  cfg.auto_window_s = b.auto_window_s;
  cfg.auto_floor_x  = b.auto_floor_x;
  cal.has_baseline = b.has_baseline;
  cal.has_mvc      = b.has_mvc;
  cal.baseline     = b.baseline;
  cal.rest_env     = b.rest_env;
  cal.mvc          = b.mvc;
  return true;
}

void save(const EmgConfig& cfg, const EmgCalibration& cal) {
  ensure();
  Blob b{};
  b.magic        = MAGIC;
  b.version      = VERSION;
  b.window_ms    = cfg.window_ms;
  b.gain         = cfg.gain;
  b.deadband     = cfg.deadband;
  b.auto_mvc      = cfg.auto_mvc;
  b.auto_window_s = cfg.auto_window_s;
  b.auto_floor_x  = cfg.auto_floor_x;
  b.has_baseline = cal.has_baseline;
  b.has_mvc      = cal.has_mvc;
  b.baseline     = cal.baseline;
  b.rest_env     = cal.rest_env;
  b.mvc          = cal.mvc;
  b.crc          = crc32((const uint8_t*)&b, offsetof(Blob, crc));
  EEPROM.put(0, b);
  EEPROM.commit();   // flash erase+write: core 1 is paused for a few ms (sampler resyncs)
}

void erase() {
  ensure();
  for (int i = 0; i < 4; i++) EEPROM.write(i, 0xFF);
  EEPROM.commit();
}

}  // namespace settings
