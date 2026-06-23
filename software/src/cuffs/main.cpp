#include <Arduino.h>
#include "main.h"
IMUData imuData;
PacketSerial bluetoothPacketSerial;

void setup() {
    delay(1000);
    Serial.begin(115200);
    BluetoothSerial.begin(115200);
    bluetoothPacketSerial.setStream(&BluetoothSerial);

    Serial.println("setup serial");
    setupIMU();
    Serial.println("live");
}

void loop() {
    printIMUData();
    updateIMU();
    sendIMUData();
    Serial1.println("ping");
    // Serial.println("Sent");
}
