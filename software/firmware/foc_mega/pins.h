#pragma once
// =============================================================================
//  MKS ESP32 FOC Mega — pin map + motor constants.  EDIT PINS HERE ONLY.
// =============================================================================
//
//  Wiring for the open-loop bench test (motor channel M0):
//
//    Board terminal        -> Connect to
//    ------------------------------------------------------------------
//    Power in (+/-)        -> 16 V bench supply (start with ~1 A current limit)
//    M0 phase A / B / C    -> the 3 GM4108H-120T phase wires (any order;
//                             swapping any two reverses direction)
//    USB                   -> computer (flash + serial monitor @ 115200)
//
//  Nothing else is needed for open loop: no encoder, no current sense.
//
//  PWM A/B/C + enable are VERIFIED on your board. Everything marked
//  UNVERIFIED is a placeholder for the closed-loop TODO in main.cpp and is
//  NOT used by the current firmware.
// =============================================================================

// ---- Motor M0 driver (VERIFIED) --------------------------------------------
#define PIN_M0_PWM_A   32
#define PIN_M0_PWM_B   33
#define PIN_M0_PWM_C   25
#define PIN_M0_ENABLE  12

// ---- Supply / motor constants (VERIFIED) -----------------------------------
#define SUPPLY_VOLTAGE        16.0f   // V, bench supply -> driver.voltage_power_supply
#define DRIVER_VOLTAGE_LIMIT  16.0f   // V, driver.voltage_limit
#define MOTOR_POLE_PAIRS      11      // GM4108H-120T
#define MOTOR_PHASE_RES_OHM   11.1f   // phase-to-phase (not used in open loop)

// ---- Encoder for closed loop (UNVERIFIED — not used yet) -------------------
// PWM-output magnetic encoder read with MagneticSensorPWM + interrupt.
// Set to the GPIO the encoder's PWM/OUT wire lands on. -1 = not connected.
// Must be an interrupt-capable GPIO; avoid strapping pins 0, 2, 5, 12, 15.
#define PIN_M0_ENCODER_PWM    -1
#define ENCODER_PWM_MIN_US    4       // MagneticSensorPWM(pin, min_us, max_us):
#define ENCODER_PWM_MAX_US    904     //   take these from the encoder datasheet / a test sketch

// ---- Inline current sense (UNVERIFIED — not used yet) ----------------------
// Needed later for the torque cap / resistance detection. Confirm against the
// MKS schematic before using. -1 = not used.
#define PIN_M0_CURRENT_A      -1
#define PIN_M0_CURRENT_B      -1
