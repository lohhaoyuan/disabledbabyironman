#pragma once
#include <Arduino.h>
#include <cmath>

enum class StabilityState : uint8_t {
    Unknown = 0, OnTable, Stationary, Stable, Motion
};

struct IMUData {
    double yaw   = NAN;
    double pitch = NAN;
    double roll  = NAN;

    double qw = NAN, qx = NAN, qy = NAN, qz = NAN;

    double linAccX = NAN, linAccY = NAN, linAccZ = NAN;
    double gyroX = NAN, gyroY = NAN, gyroZ = NAN;
    double gravX = NAN, gravY = NAN, gravZ = NAN;

    StabilityState stability = StabilityState::Unknown;
};