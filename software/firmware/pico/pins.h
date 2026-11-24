#pragma once
// =============================================================================
//  Raspberry Pi Pico 2 W — pin map.  EDIT PINS HERE ONLY.
// =============================================================================
//
//  Wiring (Seeed Grove EMG Detector v1.1, Grove 4-pin cable):
//
//    Grove wire   Grove EMG pin  -> Pico 2 W pin
//    -----------------------------------------------------------------------
//    Yellow       SIG (output)   -> GP27 / ADC1 / A1    (physical pin 32)
//    White        NC             -> not connected
//    Red          VCC            -> 3V3(OUT)            (physical pin 36)
//    Black        GND            -> AGND                (physical pin 33)
//
//  POWER THE EMG BOARD FROM 3V3, NOT VBUS/VSYS. Its output swings toward its
//  own supply rail and the Pico ADC must never see more than 3.3 V. If you
//  ever run it from 5 V, add a divider on SIG (e.g. 10k top / 20k bottom
//  = x0.667) and set EMG_INPUT_SCALE to 1/0.667.
//
//  Use AGND (pin 33) rather than any other GND for lower ADC noise.
// =============================================================================

// ---- EMG input --------------------------------------------------------------
// Must be an ADC pin: GP26 (A0), GP27 (A1), GP28 (A2).
#define PIN_EMG            27
#define EMG_ADC_MAX        4095     // 12-bit ADC
#define EMG_INPUT_SCALE    1.0f     // informational; firmware works in raw counts

// ---- Status LED -------------------------------------------------------------
// LED_BUILTIN on the Pico 2 W is driven through the CYW43 Wi-Fi chip.
// Replace with a GPIO number to use an external LED instead.
#define PIN_STATUS_LED     LED_BUILTIN
