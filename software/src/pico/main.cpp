#include <Arduino.h>
#include <PacketSerial.h>

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

PacketSerial btSerial;

void onPacket(const uint8_t* buf, size_t size) {
    if (size != sizeof(IMUData)) {
        Serial.printf("[ERR] bad packet size: %d (expected %d)\n", size, sizeof(IMUData));
        return;
    }

    IMUData data;
    memcpy(&data, buf, sizeof(IMUData));

    Serial.printf("[ROT]    yaw=%.2f  pitch=%.2f  roll=%.2f\n",
        data.yaw, data.pitch, data.roll);
    Serial.printf("[LINACC] x=%.3f  y=%.3f  z=%.3f\n",
        data.linAccX, data.linAccY, data.linAccZ);
    Serial.printf("[GYRO]   x=%.3f  y=%.3f  z=%.3f\n",
        data.gyroX, data.gyroY, data.gyroZ);
    Serial.printf("[GRAV]   x=%.3f  y=%.3f  z=%.3f\n",
        data.gravX, data.gravY, data.gravZ);
}

void setup() {
    Serial.begin(115200);   // USB debug
    Serial1.begin(115200);  // UART0 — GP0 TX, GP1 RX
    btSerial.setStream(&Serial1);
    btSerial.setPacketHandler(&onPacket);
    Serial.println("Pico receiver ready");
}

void loop() {
    btSerial.update();
}