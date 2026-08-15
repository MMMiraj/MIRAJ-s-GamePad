# MIRAJ's Controller — ESP32 BLE Xbox-Style Gamepad

A DIY Bluetooth LE gamepad built on the ESP32, presenting itself to Windows/Android/Linux as an Xbox-style controller with dual analog sticks, a 12-button layout, a 4-way D-pad, and analog-feeling triggers — no receiver dongle required.

## Features

- **Bluetooth LE HID gamepad** — pairs directly, no drivers or receiver needed
- **2 analog thumbsticks** (X/Y each) on ADC1 pins, safe to read while BLE radio is active
- **12 buttons**: A, B, X, Y, LB, RB, LT, RT, Back, Start, L3, R3
- **D-pad** sent as 4 discrete buttons rather than a HID hat switch, for more reliable recognition across OSes and browser-based testers
- **Debounced digital button reads** with active-low wiring (internal pull-ups)
- Manual report batching (`setAutoReport(false)`) for a clean, single BLE packet per polling cycle

## Hardware

**Board:** Any standard ESP32 Dev board (ESP32-WROOM-32 or similar)

**Library:** [ESP32-BLE-Gamepad by lemmingDev](https://github.com/lemmingDev/ESP32-BLE-Gamepad)
Install via Arduino IDE → Library Manager → search "ESP32 BLE Gamepad"

### Pinout

| Function | GPIO |
|---|---|
| A | 16 |
| B | 17 |
| X | 18 |
| Y | 19 |
| LB | 21 |
| RB | 22 |
| Back | 23 |
| Start | 25 |
| L3 (left stick click) | 26 |
| R3 (right stick click) | 27 |
| D-Pad Up | 13 |
| D-Pad Down | 14 |
| D-Pad Left | 4 |
| D-Pad Right | 5 |
| Left Stick X | 36 (VP) |
| Left Stick Y | 39 (VN) |
| Right Stick X | 34 |
| Right Stick Y | 35 |
| LT | 32 |
| RT | 33 |

All buttons: GPIO → button → GND (internal pull-ups, active LOW).
Analog sticks: potentiometer/joystick module wiper → GPIO, outer legs → 3.3V and GND. Stick pins are all ADC1 channels, which stay accurate with BLE running (ADC2 does not).

## Setup

1. Install the **ESP32-BLE-Gamepad** library (link above).
2. Open this sketch in the Arduino IDE, select your ESP32 board.
3. Flash it.
4. On your host device, open Bluetooth settings and pair **"MIRAJ's Controller."**
5. Test axes/buttons — on Windows, `Set up USB game controllers` (joy.cpl) gives the most reliable raw readout; browser-based gamepad testers can be inconsistent with non-standard BLE devices (see note below).

## Known gotcha: re-pairing after any config change

If you change the button count, axis configuration, or anything else that alters the BLE HID descriptor, **the host caches the old descriptor** and won't just pick up the new one on reconnect. After flashing a config change:

1. Fully **forget/unpair** the device in the host's Bluetooth settings (not just disconnect)
2. Reboot the host
3. Re-pair from scratch

Skipping this step is the most common cause of "I changed the code but nothing changed on the host."

## Notes on axis mapping

The BLE HID report's axis order is `x, y, z, rZ, rX, rY, slider1, slider2`. Different hosts/testers read different axes for "right stick Y" depending on how they interpret a non-standard gamepad — this sketch sends the right stick's Y value on the **rX** slot, which is what Windows/DirectInput and most browser-based testers pick up in practice. If you're seeing one stick axis not register on your setup, checking the raw axis values in `joy.cpl` (rather than a browser tester) is the most reliable way to confirm what the host is actually receiving before touching the code.
