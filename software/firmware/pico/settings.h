#pragma once
// Persistent settings + calibration.
//
// Stored with the arduino-pico EEPROM emulation, which lives in its own flash
// sector *outside* the LittleFS partition. That way `pio run -t uploadfs`
// (re-flashing the web files) never wipes a patient's calibration.
// The task brief said LittleFS; this is the same idea, just a safer sector.

#include "emg.h"

namespace settings {
bool load(EmgConfig& cfg, EmgCalibration& cal);   // false => defaults used
void save(const EmgConfig& cfg, const EmgCalibration& cal);
void erase();
}
