#pragma once
#include <Arduino.h>
#include <Adafruit_BNO08x.h>
#include <ArduinoEigenDense.h>
#include "PacketSerial.h"

extern PacketSerial bluetoothPacketSerial;
#define BluetoothSerial Serial1


#define LED 13
// --- IMU SPI PINS ---
#define IMU_CS   10
#define IMU_RST  14
#define IMU_INT  15

enum class StabilityState : uint8_t {
    Unknown = 0, OnTable, Stationary, Stable, Motion
};

struct IMUData {
    // game rotation vector → euler angles (degrees)
    double yaw   = NAN;
    double pitch = NAN;
    double roll  = NAN;

    // raw quaternion (corrected)
    double qw = NAN, qx = NAN, qy = NAN, qz = NAN;

    // linear acceleration m/s² (gravity removed)
    double linAccX = NAN, linAccY = NAN, linAccZ = NAN;

    // calibrated gyroscope rad/s
    double gyroX = NAN, gyroY = NAN, gyroZ = NAN;

    // gravity vector m/s²
    double gravX = NAN, gravY = NAN, gravZ = NAN;

    // stability
    StabilityState stability = StabilityState::Unknown;
};

// global instance
extern IMUData imuData;

void setupIMU();
double readIMUYaw();
double readIMURoll();
double readIMUPitch();
void printIMUData();
void updateIMU();
void sendIMUData();