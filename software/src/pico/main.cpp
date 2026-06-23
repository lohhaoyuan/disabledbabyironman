#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <ArduinoEigenDense.h>
#include "main.h"

Adafruit_BNO08x bno;
IMUData imuData;
Eigen::Quaterniond initialRotationOffset = Eigen::Quaterniond::Identity();

void setupIMU() {
    // I2C0 on Pico: SDA=GP4, SCL=GP5
    Wire.setSDA(4);
    Wire.setSCL(5);
    Wire.begin();

    if (!bno.begin_I2C(0x4A, &Wire)) {
        Serial.println("[ERR] BNO08x not found");
        while (1) { delay(10); }
    }
    Serial.println("BNO found");

    bno.enableReport(SH2_GAME_ROTATION_VECTOR, 10000);
    bno.enableReport(SH2_LINEAR_ACCELERATION,  10000);
    bno.enableReport(SH2_GYROSCOPE_CALIBRATED, 10000);
    bno.enableReport(SH2_GRAVITY,              20000);
    bno.enableReport(SH2_STABILITY_CLASSIFIER, 50000);
    Serial.println("Reports enabled");
}

void updateIMU() {
    sh2_SensorValue_t v;
    if (!bno.getSensorEvent(&v)) return;

    switch (v.sensorId) {
    case SH2_GAME_ROTATION_VECTOR: {
        const Eigen::Quaterniond rotation = {
            v.un.gameRotationVector.real,
            v.un.gameRotationVector.i,
            v.un.gameRotationVector.j,
            v.un.gameRotationVector.k,
        };
        if (initialRotationOffset == Eigen::Quaterniond::Identity())
            initialRotationOffset = rotation.inverse();

        const auto corrected = initialRotationOffset * rotation;
        const auto R = corrected.toRotationMatrix();

        imuData.yaw   = -degrees(atan2(R(1, 0), R(0, 0)));
        imuData.pitch =  degrees(atan2(-R(2, 0), sqrt(R(2,1)*R(2,1) + R(2,2)*R(2,2))));
        imuData.roll  =  degrees(atan2(R(2, 1), R(2, 2)));

        imuData.qw = corrected.w();
        imuData.qx = corrected.x();
        imuData.qy = corrected.y();
        imuData.qz = corrected.z();
        break;
    }
    case SH2_LINEAR_ACCELERATION:
        imuData.linAccX = v.un.linearAcceleration.x;
        imuData.linAccY = v.un.linearAcceleration.y;
        imuData.linAccZ = v.un.linearAcceleration.z;
        break;
    case SH2_GYROSCOPE_CALIBRATED:
        imuData.gyroX = v.un.gyroscope.x;
        imuData.gyroY = v.un.gyroscope.y;
        imuData.gyroZ = v.un.gyroscope.z;
        break;
    case SH2_GRAVITY:
        imuData.gravX = v.un.gravity.x;
        imuData.gravY = v.un.gravity.y;
        imuData.gravZ = v.un.gravity.z;
        break;
    case SH2_STABILITY_CLASSIFIER:
        imuData.stability = static_cast<StabilityState>(
            v.un.stabilityClassifier.classification);
        break;
    default: break;
    }
}

void printIMUData() {
    Serial.printf("[ROT]    yaw=%.2f  pitch=%.2f  roll=%.2f\n",
        imuData.yaw, imuData.pitch, imuData.roll);
    Serial.printf("[LINACC] x=%.3f  y=%.3f  z=%.3f\n",
        imuData.linAccX, imuData.linAccY, imuData.linAccZ);
    Serial.printf("[GYRO]   x=%.3f  y=%.3f  z=%.3f\n",
        imuData.gyroX, imuData.gyroY, imuData.gyroZ);
    Serial.printf("[GRAV]   x=%.3f  y=%.3f  z=%.3f\n",
        imuData.gravX, imuData.gravY, imuData.gravZ);
}

void setup() {
    Serial.begin(115200);
    setupIMU();
}

void loop() {
    updateIMU();
    printIMUData();
}