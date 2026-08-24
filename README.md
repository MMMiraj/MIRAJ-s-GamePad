# MIRAJ's Controller — ESP32 BLE Gamepad

A DIY Bluetooth LE gamepad built on an ESP32, designed to work as a standard
Xbox-style controller on Windows/PC — built primarily for use with the
**Create: Tweaked Controllers** Minecraft mod, but works with any game or
gamepad tester that reads the standard HID gamepad layout.

## Features

- Native **Bluetooth LE** — no dongle, no drivers, pairs directly with Windows
- Two analog thumbsticks with **auto-calibration**:
  - Each stick's resting center is measured automatically at boot
  - Each stick's real min/max travel range self-expands during normal play — no manual calibration step
  - ADC oversampling (8 samples averaged) to smooth out ESP32 ADC noise
  - Output is **circularized** so diagonal movement doesn't exceed 100% magnitude (fixes "circle error" on gamepad testers)
- Standard **W3C Standard Gamepad** button layout (A/B/X/Y, LB/RB, LT/RT, Back/Start, L3/R3, D-pad) — labels show up correctly on gamepad testers and in-game
- Live **axis-invert toggle** (no reflashing): hold **Back**, then tap a D-pad direction to invert Left stick X/Y; hold **Back + R3** and tap a direction to invert the Right stick
- **Stick-range reset hotkey**: hold **Back + Start** for 2 seconds to reset the auto-calibrated range back to safe defaults
- Only sends a BLE report when something actually changes, to avoid overflowing the BLE notify queue (prevents dropped/jumpy input)

## Hardware

- 1x ESP32 Dev Board (ESP32-WROOM-32 or similar)
- 2x analog joystick modules (with click switch)
- 10x tactile push buttons (A, B, X, Y, LB, RB, Back, Start, LT, RT)
- 4x tactile push buttons (D-pad Up/Down/Left/Right)
- Breadboard / perfboard, jumper wires

## Wiring

All buttons: **GPIO → button → GND** (internal pull-ups used, so a press reads LOW).
Joysticks: wiper → GPIO, outer legs → 3.3V and GND.

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
| D-pad Up | 13 |
| D-pad Down | 14 |
| D-pad Left | 4 |
| D-pad Right | 5 |
| LT (button) | 32 |
| RT (button) | 33 |
| Left Stick X | 36 (VP) |
| Left Stick Y | 39 (VN) |
| Right Stick X | 34 |
| Right Stick Y | 35 |

> Left/Right stick axes use ADC1-only pins so they keep working once Bluetooth is active (ADC2 conflicts with the radio on ESP32).

## Setup

1. Install the **ESP32 board package** in Arduino IDE (Boards Manager → search "esp32").
2. Install the **ESP32 BLE Gamepad** library by lemmingDev:
   Library Manager → search "ESP32 BLE Gamepad" → Install
   (or [GitHub](https://github.com/lemmingDev/ESP32-BLE-Gamepad))
3. Wire the hardware per the table above.
4. Open `esp32_ble_gamepad.ino`, select your ESP32 board + port, and upload.
5. Open Serial Monitor at `115200` baud — on boot it calibrates stick centers (leave sticks untouched for about half a second) and prints the calibration values.
6. Pair **"MIRAJ's Controller"** via Windows Bluetooth settings.
7. Test on a gamepad tester site (e.g. `hardwaretester.com/gamepad`) to confirm all axes/buttons register correctly.

## Usage notes

- **First few minutes of play**: move each stick fully to its corners a couple of times so the auto-range calibration learns the real travel limits.
- **Reset stick range**: hold Back + Start for 2 seconds if a bad reading ever throws off the auto-calibrated range.
- **Invert an axis**: hold Back and tap a D-pad direction (Left stick), or hold Back + R3 and tap a direction (Right stick). Toggles live, resets on power-cycle.

## Used with

Built and tested for **[Create: Tweaked Controllers](https://www.curseforge.com/minecraft/mc-mods/create-tweaked-controllers)**, a Create mod addon that lets you bind gamepad buttons/axes to in-game contraptions. Button-to-function assignment happens inside the mod's own config UI — this firmware just needs to expose a clean, correctly labeled gamepad.

## License

MIT — do whatever you want with it.
