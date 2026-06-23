#include "main.h"
#include <Wire.h>

Adafruit_BNO08x bno;  // create the BNO08x object with the reset pin);
Eigen::Quaterniond initialRotationOffset = Eigen::Quaterniond::Identity();
Eigen::Quaterniond initialYawOffset = Eigen::Quaterniond::Identity();
Eigen::Quaterniond initialRollOffset = Eigen::Quaterniond::Identity();
Eigen::Quaterniond initialPitchOffset = Eigen::Quaterniond::Identity();

void setupIMU() {
    if (!bno.begin_I2C(0x4A, &Wire)) {
        Serial.println("Failed to find BNO08x chip");
        while (1) {
            digitalWrite(LED, HIGH);
            delay(100);
            digitalWrite(LED, LOW);
            delay(100);
        }
    }
    Serial.println("BNO found");


    // game rotation vector — quaternion, no magnetometer fusion
    if (!bno.enableReport(SH2_GAME_ROTATION_VECTOR, 10000)) {
        Serial.println("[IMU] ERR: game rotation vector");
        while (1) { delay(10); }
    }
    Serial.println("game rotation vector report enabled");
    // linear acceleration — gravity removed
    if (!bno.enableReport(SH2_LINEAR_ACCELERATION, 10000)) {
        Serial.println("[IMU] ERR: linear acceleration");
        while (1) { delay(10); }
    }
    Serial.println("linear acceleration report enabled");

    // calibrated gyroscope
    if (!bno.enableReport(SH2_GYROSCOPE_CALIBRATED, 10000)) {
        Serial.println("[IMU] ERR: gyroscope");
        while (1) { delay(10); }
    }
    Serial.println("gyro report enabled");
    // gravity vector — limb orientation relative to vertical
    if (!bno.enableReport(SH2_GRAVITY, 20000)) {
        Serial.println("[IMU] ERR: gravity");
        while (1) { delay(10); }
    }


    Serial.println("gravity report enabled");
    // stability classifier — on-sensor motion state
    if (!bno.enableReport(SH2_STABILITY_CLASSIFIER, 50000)) {
        Serial.println("[IMU] ERR: stability classifier");
        while (1) { delay(10); }
    }
    Serial.println("stability classifier enabled");
}


double readIMUYaw() {
    sh2_SensorValue_t bnoValue;
    if (bno.getSensorEvent(&bnoValue)) {
        switch (bnoValue.sensorId) {
        case SH2_GAME_ROTATION_VECTOR: {
            const Eigen::Quaterniond rotation = {
                bnoValue.un.gameRotationVector.real,
                bnoValue.un.gameRotationVector.i,
                bnoValue.un.gameRotationVector.j,
                bnoValue.un.gameRotationVector.k,
            };

            // Set the initial offset if it hasn't been set
            if (initialYawOffset == Eigen::Quaterniond::Identity())
                initialYawOffset = rotation.inverse();

            // Compute the robot angle
            const auto correctedRotation = initialYawOffset * rotation;
            const auto rotationMatrix = correctedRotation.toRotationMatrix();
            const auto yaw = -atan2(rotationMatrix(1, 0), rotationMatrix(0, 0));
            return degrees(yaw);

            break;
        }
        default:
            break;
        }
    }

    return NAN;
}

// ── Roll / Pitch (same offset correction as heading) ──────────────────────

double readIMURoll() {
    sh2_SensorValue_t bnoValue;
    if (bno.getSensorEvent(&bnoValue)) {
        switch (bnoValue.sensorId) {
        case SH2_GAME_ROTATION_VECTOR: {
            const Eigen::Quaterniond rotation = {
                bnoValue.un.gameRotationVector.real,
                bnoValue.un.gameRotationVector.i,
                bnoValue.un.gameRotationVector.j,
                bnoValue.un.gameRotationVector.k,
            };
            if (initialRollOffset == Eigen::Quaterniond::Identity())
                initialRollOffset = rotation.inverse();
            const auto R = (initialRollOffset * rotation).toRotationMatrix();
            return degrees(atan2(R(2, 1), R(2, 2)));  // ZYX convention
        }
        default: break;
        }
    }
    return NAN;
}

double readIMUPitch() {
    sh2_SensorValue_t bnoValue;
    if (bno.getSensorEvent(&bnoValue)) {
        switch (bnoValue.sensorId) {
        case SH2_GAME_ROTATION_VECTOR: {
            const Eigen::Quaterniond rotation = {
                bnoValue.un.gameRotationVector.real,
                bnoValue.un.gameRotationVector.i,
                bnoValue.un.gameRotationVector.j,
                bnoValue.un.gameRotationVector.k,
            };
            if (initialPitchOffset == Eigen::Quaterniond::Identity())
                initialPitchOffset = rotation.inverse();
            const auto R = (initialPitchOffset * rotation).toRotationMatrix();
            return degrees(atan2(-R(2, 0), sqrt(R(2, 1) * R(2, 1) + R(2, 2) * R(2, 2))));
        }
        default: break;
        }
    }
    return NAN;
}

// ── Serial print all reports ───────────────────────────────────────────────

void printIMUData() {
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
        const auto R = (initialRotationOffset * rotation).toRotationMatrix();
        Serial.printf("[ROT] yaw=%.2f  pitch=%.2f  roll=%.2f\n",
            (double)-degrees(atan2(R(1, 0), R(0, 0))),
            (double) degrees(atan2(-R(2, 0), sqrt(R(2,1)*R(2,1) + R(2,2)*R(2,2)))),
            (double) degrees(atan2(R(2, 1), R(2, 2))));
        break;
    }
    case SH2_LINEAR_ACCELERATION:
        Serial.printf("[LINACC] x=%.3f  y=%.3f  z=%.3f m/s²\n",
            v.un.linearAcceleration.x,
            v.un.linearAcceleration.y,
            v.un.linearAcceleration.z);
        break;
    case SH2_GYROSCOPE_CALIBRATED:
        Serial.printf("[GYRO]   x=%.3f  y=%.3f  z=%.3f rad/s\n",
            v.un.gyroscope.x,
            v.un.gyroscope.y,
            v.un.gyroscope.z);
        break;
    case SH2_GRAVITY:
        Serial.printf("[GRAV]   x=%.3f  y=%.3f  z=%.3f m/s²\n",
            v.un.gravity.x,
            v.un.gravity.y,
            v.un.gravity.z);
        break;
    case SH2_STABILITY_CLASSIFIER: {
        const char* states[] = {"Unknown", "OnTable", "Stationary", "Stable", "Motion"};
        uint8_t cls = v.un.stabilityClassifier.classification;
        Serial.printf("[STAB]   %s\n", cls < 5 ? states[cls] : "?");
        break;
    }
    default: break;
    }
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

void sendIMUData() {
    bluetoothPacketSerial.send(
        reinterpret_cast<const uint8_t*>(&imuData),
        sizeof(IMUData)
    );
}