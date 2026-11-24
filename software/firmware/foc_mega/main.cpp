// =============================================================================
//  CENTAD — MKS ESP32 FOC Mega + GM4108H-120T  ·  OPEN-LOOP BENCH TEST ONLY
// =============================================================================
//
//  !!! BENCH TEST ONLY — NOT FOR USE WITH A CHILD ON THE RIG !!!
//    * No encoder: the controller does not know where the shaft is.
//    * No current sense: there is NO torque cap and NO resistance detection.
//      If the shaft is blocked the motor keeps pushing at voltage_limit.
//    * Open loop drives full voltage_limit through the windings all the time,
//      so the motor heats up quickly even when it is barely moving. Keep
//      voltage_limit low (default 3 V, hard cap 6 V) and touch-check the can.
//
//  Serial commands (115200, newline-terminated; SimpleFOC Commander):
//    T<rad/s>   target velocity, clamped to +/-5      e.g.  T1   T-0.5
//    V<volts>   open-loop voltage limit, clamped 0..6 e.g.  V3
//    S          stop: target -> 0 and disable driver (motor goes limp)
//    E          enable driver again (target stays as is)
//    ?          list commands
//
//  Status line every 500 ms:
//    target=<rad/s> angle=<rad> Uq=<V> vlim=<V> en=<0|1>
//
//  NOTE on this SimpleFOC version (hardware-verified): open loop only produces
//  a non-zero Uq if motor.init() AND motor.initFOC() are both called, the
//  voltage limit is set again after init, and loop() calls motor.loopFOC()
//  BEFORE motor.move(). Without that Uq stays 0 and the motor just holds.
//
//  TODO(closed loop): add the PWM encoder with
//      MagneticSensorPWM sensor(PIN_M0_ENCODER_PWM, ENCODER_PWM_MIN_US, ENCODER_PWM_MAX_US);
//      void doPWM(){ sensor.handlePWM(); }
//      sensor.init(); sensor.enableInterrupt(doPWM); motor.linkSensor(&sensor);
//    then switch to MotionControlType::velocity and tune motor.PID_velocity.
//  TODO(torque cap): add InlineCurrentSense on PIN_M0_CURRENT_A/B and use
//    TorqueControlType::foc_current with motor.current_limit.
//  TODO(resistance detection): compare commanded vs measured velocity and/or
//    q-current; if the infant pushes back past a threshold, back off / stop.
// =============================================================================

#include <Arduino.h>
#include <SimpleFOC.h>
#include "pins.h"

// ---- Limits -----------------------------------------------------------------
static constexpr float DEFAULT_TARGET_RAD_S = 1.0f;
static constexpr float MAX_TARGET_RAD_S     = 5.0f;
static constexpr float DEFAULT_VOLTAGE_V    = 3.0f;
static constexpr float MAX_VOLTAGE_V        = 6.0f;   // hard cap: open loop heats fast
static constexpr uint32_t STATUS_PERIOD_MS  = 500;

BLDCMotor      motor(MOTOR_POLE_PAIRS);
BLDCDriver3PWM driver(PIN_M0_PWM_A, PIN_M0_PWM_B, PIN_M0_PWM_C, PIN_M0_ENABLE);
Commander      command(Serial);

float target_velocity = DEFAULT_TARGET_RAD_S;
bool  driver_enabled  = true;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- Commander callbacks ----------------------------------------------------
void onTarget(char* cmd) {
  float v = 0;
  command.scalar(&v, cmd);
  target_velocity = clampf(v, -MAX_TARGET_RAD_S, MAX_TARGET_RAD_S);
  Serial.printf("target = %.3f rad/s\n", target_velocity);
}

void onVoltage(char* cmd) {
  float v = 0;
  command.scalar(&v, cmd);
  motor.voltage_limit = clampf(v, 0.0f, MAX_VOLTAGE_V);
  Serial.printf("voltage_limit = %.2f V\n", motor.voltage_limit);
}

void onStop(char*) {
  target_velocity = 0;
  motor.disable();
  driver_enabled = false;
  Serial.println("STOPPED: target=0, driver disabled (send E to re-enable)");
}

void onEnable(char*) {
  motor.enable();
  driver_enabled = true;
  Serial.printf("driver enabled, target = %.3f rad/s\n", target_velocity);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== CENTAD FOC Mega: OPEN-LOOP BENCH TEST (no encoder, no torque cap) ===");

  SimpleFOCDebug::enable(&Serial);

  driver.voltage_power_supply = SUPPLY_VOLTAGE;
  driver.voltage_limit        = DRIVER_VOLTAGE_LIMIT;
  if (!driver.init()) {
    Serial.println("[ERR] driver.init() failed — check power and pins.h");
  }
  motor.linkDriver(&driver);

  motor.controller    = MotionControlType::velocity_openloop;
  motor.voltage_limit = DEFAULT_VOLTAGE_V;
  motor.velocity_limit = MAX_TARGET_RAD_S;

  motor.init();
  motor.initFOC();                         // required for open loop on this SimpleFOC version
  motor.voltage_limit = DEFAULT_VOLTAGE_V; // must be set again after init

  command.add('T', onTarget,  "target velocity rad/s (+/-5)");
  command.add('V', onVoltage, "voltage limit V (0..6)");
  command.add('S', onStop,    "stop + disable");
  command.add('E', onEnable,  "enable");

  Serial.printf("Ready. target=%.2f rad/s, voltage_limit=%.2f V. Commands: T V S E ?\n",
                target_velocity, motor.voltage_limit);
}

void loop() {
  motor.loopFOC();                // must run BEFORE move() (see header note)
  motor.move(target_velocity);
  command.run();

  static uint32_t last = 0;
  const uint32_t now = millis();
  if (now - last >= STATUS_PERIOD_MS) {
    last = now;
    Serial.printf("target=%.3f angle=%.3f Uq=%.3f vlim=%.2f en=%d\n",
                  target_velocity, motor.shaft_angle, motor.voltage.q,
                  motor.voltage_limit, driver_enabled ? 1 : 0);
  }
}
