/*
  ==========================================================
   MIRAJ's Controller - ESP32 BLE Xbox-style Gamepad
  ==========================================================
  Bluetooth LE HID Gamepad using ESP32.
  BT Device Name: "MIRAJ's Controller"

  LIBRARY REQUIRED:
    ESP32-BLE-Gamepad by lemmingDev
    Install via Arduino IDE -> Library Manager -> search
    "ESP32 BLE Gamepad" -> Install
    (GitHub: https://github.com/lemmingDev/ESP32-BLE-Gamepad)

  BOARD:
    Any ESP32 Dev board (ESP32-WROOM-32 etc.)

  ==========================================================
  BUTTON MAP (Xbox-style)
  ==========================================================
    A       -> GPIO 16
    B       -> GPIO 17
    X       -> GPIO 18
    Y       -> GPIO 19
    LB      -> GPIO 21
    RB      -> GPIO 22
    Back    -> GPIO 23
    Start   -> GPIO 25
    L3      -> GPIO 26   (left stick click)
    R3      -> GPIO 27   (right stick click)

  D-PAD (digital, sent as regular buttons 13-16 - more reliably
  recognized than a BLE hat/POV switch by most OSes and testers)
    Up      -> GPIO 13
    Down    -> GPIO 14
    Left    -> GPIO 4
    Right   -> GPIO 5

  ANALOG STICKS (ADC1 pins only - safe with BLE)
    Left Stick  X -> GPIO 36 (VP)
    Left Stick  Y -> GPIO 39 (VN)
    Right Stick X -> GPIO 34
    Right Stick Y -> GPIO 35

  LT / RT (digital switches, same as other buttons)
    LT -> GPIO 32
    RT -> GPIO 33

  All buttons wired: GPIO -> Button -> GND
  (using internal pull-ups, so button press = LOW)

  Analog sticks: potentiometer/joystick module wiper -> GPIO,
  outer legs -> 3.3V and GND.

  ==========================================================
  DRIFT FIX CHANGELOG (vs original sketch)
  ==========================================================
  1. Per-stick auto-calibration at boot: each stick's resting
     center is measured (200 samples) instead of assuming a
     fixed 2048 midpoint. Cheap pots rarely rest exactly at
     2048, and that offset alone causes slow one-directional
     drift.
  2. ADC oversampling: every stick reading is now an average
     of 8 samples instead of a single analogRead(). The ESP32's
     ADC1 is inherently noisy (+/-20-40 raw counts at rest);
     averaging removes most of that jitter.
  3. Deadzone increased from 80 -> 180 (out of 4095) now that
     readings are averaged and centered correctly, so small
     leftover noise doesn't register as movement.
  4. mapStick() now maps the positive and negative sides of
     each stick separately around its OWN calibrated center,
     instead of assuming a symmetric -2048..2047 range around
     a fixed point.
  5. Hold sticks centered and still during the first ~0.5s
     after power-on / reconnect - that's when calibration runs.
     A serial message announces when it's done.
  ==========================================================
*/

#include <BleGamepad.h>

// ---------------- Pin Definitions ----------------
// Face buttons
#define PIN_A      16
#define PIN_B      17
#define PIN_X      18
#define PIN_Y      19

// Shoulder / system buttons
#define PIN_LB     21
#define PIN_RB     22
#define PIN_BACK   23
#define PIN_START  25
#define PIN_L3     26
#define PIN_R3     27

// D-Pad
#define PIN_DPAD_UP     13
#define PIN_DPAD_DOWN   14
#define PIN_DPAD_LEFT   4
#define PIN_DPAD_RIGHT  5

// Analog sticks & triggers (ADC1 channels only)
#define PIN_LSTICK_X  36
#define PIN_LSTICK_Y  39
#define PIN_RSTICK_X  34
#define PIN_RSTICK_Y  35
#define PIN_LT        32
#define PIN_RT        33

// ---------------- Button -> BLE Button Map ----------------
// Matches the "standard gamepad" position order that Windows/
// Chrome/Android remap onto: A,B,X,Y,LB,RB,LT,RT,Back,Start,L3,R3
// LT/RT get a DIGITAL press (in addition to their analog slider
// value) so they land in the correct button slot (6,7). Without
// this, everything after LB/RB shifts up by two slots, which is
// why Back/Start showed up as LT/RT and L3/R3 showed up as
// Back/Start (the "3-line" menu icons) before.
#define BTN_A     BUTTON_1
#define BTN_B     BUTTON_2
#define BTN_X     BUTTON_3
#define BTN_Y     BUTTON_4
#define BTN_LB    BUTTON_5
#define BTN_RB    BUTTON_6
#define BTN_LT    BUTTON_7
#define BTN_RT    BUTTON_8
#define BTN_BACK  BUTTON_9
#define BTN_START BUTTON_10
#define BTN_L3    BUTTON_11
#define BTN_R3    BUTTON_12
#define BTN_DPAD_UP     BUTTON_13
#define BTN_DPAD_DOWN   BUTTON_14
#define BTN_DPAD_LEFT   BUTTON_15
#define BTN_DPAD_RIGHT  BUTTON_16

// ---------------- Globals ----------------
BleGamepad bleGamepad("MIRAJ's Controller", "ESP32", 100);

// Struct to track button state for simple debounce
struct BtnState {
  uint8_t pin;
  uint8_t bleButton;
  bool lastState;
};

BtnState buttons[] = {
  {PIN_A,     BTN_A,     false},
  {PIN_B,     BTN_B,     false},
  {PIN_X,     BTN_X,     false},
  {PIN_Y,     BTN_Y,     false},
  {PIN_LB,    BTN_LB,    false},
  {PIN_RB,    BTN_RB,    false},
  {PIN_BACK,  BTN_BACK,  false},
  {PIN_START, BTN_START, false},
  {PIN_L3,    BTN_L3,    false},
  {PIN_R3,    BTN_R3,    false},
  {PIN_DPAD_UP,    BTN_DPAD_UP,    false},
  {PIN_DPAD_DOWN,  BTN_DPAD_DOWN,  false},
  {PIN_DPAD_LEFT,  BTN_DPAD_LEFT,  false},
  {PIN_DPAD_RIGHT, BTN_DPAD_RIGHT, false},
  {PIN_LT,    BTN_LT,    false},
  {PIN_RT,    BTN_RT,    false},
};

const int numButtons = sizeof(buttons) / sizeof(buttons[0]);

// Total BLE button count = every entry in the array above
// (LT/RT are plain switches too, so they're included here now).
const int totalBleButtons = numButtons;

// Analog deadzone (raw ADC 0-4095), used only by the analog
// sticks (left/right thumbsticks), not LT/RT. Raised from 80 to
// 180 now that readings are oversampled + individually centered.
const int DEADZONE = 180;

// Number of ADC samples averaged per stick reading per loop.
// Higher = smoother but slightly slower loop. 8 is a good balance
// at ~100Hz report rate.
const int ADC_OVERSAMPLE = 8;

// Calibrated centers, measured at boot in calibrateSticks().
int centerLX = 2048;
int centerLY = 2048;
int centerRX = 2048;
int centerRY = 2048;

// ---------------- Helpers ----------------

// Average several ADC reads to smooth out ESP32 ADC1 noise.
int readAveraged(int pin, int samples = ADC_OVERSAMPLE) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
  }
  return sum / samples;
}

// Measure each stick's true resting center. Call once at boot
// (and optionally on reconnect) while sticks are untouched.
void calibrateSticks() {
  const int samples = 200;
  long sumLX = 0, sumLY = 0, sumRX = 0, sumRY = 0;

  for (int i = 0; i < samples; i++) {
    sumLX += analogRead(PIN_LSTICK_X);
    sumLY += analogRead(PIN_LSTICK_Y);
    sumRX += analogRead(PIN_RSTICK_X);
    sumRY += analogRead(PIN_RSTICK_Y);
    delay(2);
  }

  centerLX = sumLX / samples;
  centerLY = sumLY / samples;
  centerRX = sumRX / samples;
  centerRY = sumRY / samples;

  Serial.println("Stick calibration complete:");
  Serial.print("  centerLX="); Serial.println(centerLX);
  Serial.print("  centerLY="); Serial.println(centerLY);
  Serial.print("  centerRX="); Serial.println(centerRX);
  Serial.print("  centerRY="); Serial.println(centerRY);
}

// Convert raw ADC (0-4095) to signed axis range (-32767 to 32767)
// with a deadzone around the stick's OWN calibrated center. Maps
// the positive and negative sides separately since the center
// won't sit exactly in the middle of the 0-4095 range.
int16_t mapStick(int raw, int center) {
  int diff = raw - center;

  if (abs(diff) < DEADZONE) {
    diff = 0;
  }

  long mapped;
  if (diff >= 0) {
    int posRange = (4095 - center);
    if (posRange < 1) posRange = 1;
    mapped = map(diff, 0, posRange, 0, 32767);
  } else {
    int negRange = center;
    if (negRange < 1) negRange = 1;
    mapped = map(diff, -negRange, 0, -32767, 0);
  }

  return (int16_t)constrain(mapped, -32767, 32767);
}

void setup() {
  Serial.begin(115200);

  // Button pins (input with internal pull-up, active LOW)
  for (int i = 0; i < numButtons; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
  }

  // ADC resolution
  analogReadResolution(12); // 0-4095

  // ---------------- BLE Gamepad configuration ----------------
  BleGamepadConfiguration bleGamepadConfig;
  bleGamepadConfig.setControllerType(CONTROLLER_TYPE_GAMEPAD); // Xbox-style gamepad
  bleGamepadConfig.setButtonCount(totalBleButtons);
  bleGamepadConfig.setHatSwitchCount(0);       // D-pad is sent as buttons 13-16 instead
  bleGamepadConfig.setAutoReport(false);       // we call sendReport() manually
  bleGamepadConfig.setIncludeXAxis(true);
  bleGamepadConfig.setIncludeYAxis(true);
  bleGamepadConfig.setIncludeZAxis(true);   // right stick X (Windows convention)
  bleGamepadConfig.setIncludeRzAxis(true);  // right stick Y (Windows convention)
  // NOTE: RX/RY were previously disabled here, leaving a gap in the axis
  // sequence (X, Y, Z, --, --, RZ). That gap appears to break the HID
  // descriptor's field for RZ specifically - Windows/DirectInput showed
  // "Z Rotation" stuck at 0% even though the ESP32 was sending the value
  // correctly. Keeping RX/RY enabled (even though unused/always 0) keeps
  // the X,Y,Z,RX,RY,RZ block contiguous and fixes RZ.
  bleGamepadConfig.setIncludeRxAxis(true);   // must stay enabled alongside RZ - see note below
  bleGamepadConfig.setIncludeRyAxis(true);   // (unused, always 0, but keeps the axis block contiguous)
  bleGamepadConfig.setIncludeSlider1(true);    // LT
  bleGamepadConfig.setIncludeSlider2(true);    // RT

  // IMPORTANT: since library v5, the default axis range is 0..32767
  // (not -32767..32767) for compatibility with testers that dislike
  // negative values. Our stick mapping is centered (-32767..32767),
  // so without this, negative values wrap/overflow on the host side
  // - this is what caused the "hits max then jumps negative" bug.
  bleGamepadConfig.setAxesMin(-32767);
  bleGamepadConfig.setAxesMax(32767);

  bleGamepad.begin(&bleGamepadConfig);

  Serial.println("MIRAJ's Controller - BLE Gamepad started.");
  Serial.println("Calibrating sticks - leave them centered...");
  calibrateSticks();
  Serial.println("Advertising...");
}

void loop() {
  if (bleGamepad.isConnected()) {

    // ---------- Buttons ----------
    for (int i = 0; i < numButtons; i++) {
      bool pressed = (digitalRead(buttons[i].pin) == LOW); // active LOW
      if (pressed != buttons[i].lastState) {
        if (pressed) {
          bleGamepad.press(buttons[i].bleButton);
        } else {
          bleGamepad.release(buttons[i].bleButton);
        }
        buttons[i].lastState = pressed;
      }
    }

    // (D-pad is now handled above as regular buttons 13-16)

    // ---------- Analog Sticks ----------
    // Oversampled reads to smooth ADC noise, then mapped around
    // each stick's own calibrated center.
    int16_t lx = mapStick(readAveraged(PIN_LSTICK_X), centerLX);
    int16_t ly = mapStick(readAveraged(PIN_LSTICK_Y), centerLY);
    int16_t rx = mapStick(readAveraged(PIN_RSTICK_X), centerRX);
    int16_t ry = mapStick(readAveraged(PIN_RSTICK_Y), centerRY);

    // ---------- Triggers (LT/RT are plain switches, not analog) ----------
    // Digital press/release already handled in the buttons loop above
    // (they're in the buttons[] array). Here we just derive a simple
    // "full/none" slider value from that same digital state, purely
    // for games/testers that expect an analog trigger axis.
    int16_t lt = (digitalRead(PIN_LT) == LOW) ? 32767 : 0;
    int16_t rt = (digitalRead(PIN_RT) == LOW) ? 32767 : 0;

    // Verified setAxes signature: (x, y, z, rX, rY, rZ, slider1, slider2).
    // Confirmed working placement (matches tested reference sketch and
    // the browser tester reading HID report index 4 = rX): ry goes in
    // the rX slot (4th argument), not rZ.
    bleGamepad.setAxes(lx, ly, rx, ry, 0, 0, lt, rt);

    // ---------- Push report over BLE ----------
    bleGamepad.sendReport();
  }

  delay(10); // ~100Hz polling rate
}
