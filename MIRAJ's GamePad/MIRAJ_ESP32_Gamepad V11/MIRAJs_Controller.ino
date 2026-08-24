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
     after power-on / reconnect - that's when CENTER calibration
     runs. A serial message announces when it's done.
  6. RANGE calibration (fixes high "Circle Error" on gamepad
     testers): right after center calibration, you get a 4-second
     window where you must rotate BOTH sticks around their full
     outer edge (like tracing a circle). This measures each axis's
     real min/max travel instead of assuming the theoretical
     0-4095 range.
  7. Circularization: each stick's X/Y pair is normalized then
     clamped so the combined vector never exceeds a unit circle
     before being scaled to output range. A 2-pot stick's square/
     diamond mechanical travel otherwise lets X and Y both hit
     +-100% near the corners (combined magnitude ~141%), which is
     exactly what produces "spikes past the circle" / high circle
     error on a tester.
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

// Calibrated per-axis extremes, measured in calibrateRange().
// Center calibration alone (0.0% Center Error on the tester)
// doesn't fix circle error - that comes from mapStick() assuming
// a full symmetric 0-4095 theoretical range that the pots never
// actually reach, plus the square/diamond mechanical travel of a
// 2-pot stick producing overshoot at some angles. These extremes
// let us map from what the stick REALLY does, and circularizeAndScale()
// below clamps the combined X/Y vector so no angle overshoots any
// other - that's what directly reduces circle error.
int minLX = 200,  maxLX = 3900;
int minLY = 200,  maxLY = 3900;
int minRX = 200,  maxRX = 3900;
int minRY = 200,  maxRY = 3900;

// How long (ms) to collect min/max while the user rotates each
// stick around its full outer edge during boot calibration.
const unsigned long RANGE_CAL_MS = 4000;

// Last-sent axis values, used to only send a BLE report when
// something actually changed. This avoids flooding the BLE
// notify queue faster than the connection interval can drain
// it - overflowing that queue causes dropped/out-of-order
// packets, which is what produces the "starburst" pattern in
// gamepad testers (sparse, jumpy points connected by straight
// lines instead of a smooth circle).
int16_t lastLX = 0, lastLY = 0, lastRX = 0, lastRY = 0;
int16_t lastLT = 0, lastRT = 0;

// Minimum change (in mapped -32767..32767 units) required before
// we bother sending a new report for the sticks. Filters out
// residual jitter while still feeling responsive.
const int16_t AXIS_CHANGE_THRESHOLD = 300;

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

// Measure each stick's true min/max travel by having the user
// rotate BOTH sticks around their full outer edge (tracing the
// rim, like the gamepad tester's circle) for RANGE_CAL_MS ms.
// This was the missing piece - center-only calibration gets you
// 0% Center Error but says nothing about how far each direction
// actually travels, which is what circle error measures.
void calibrateRange() {
  minLX = minLY = minRX = minRY = 4095;
  maxLX = maxLY = maxRX = maxRY = 0;

  Serial.println("Range calibration: rotate BOTH sticks around");
  Serial.println("their full outer edge now (like tracing a circle)...");

  unsigned long start = millis();
  while (millis() - start < RANGE_CAL_MS) {
    int lx = analogRead(PIN_LSTICK_X);
    int ly = analogRead(PIN_LSTICK_Y);
    int rx = analogRead(PIN_RSTICK_X);
    int ry = analogRead(PIN_RSTICK_Y);

    minLX = min(minLX, lx); maxLX = max(maxLX, lx);
    minLY = min(minLY, ly); maxLY = max(maxLY, ly);
    minRX = min(minRX, rx); maxRX = max(maxRX, rx);
    minRY = min(minRY, ry); maxRY = max(maxRY, ry);

    delay(5);
  }

  Serial.println("Range calibration complete:");
  Serial.print("  LX: "); Serial.print(minLX); Serial.print(" - "); Serial.println(maxLX);
  Serial.print("  LY: "); Serial.print(minLY); Serial.print(" - "); Serial.println(maxLY);
  Serial.print("  RX: "); Serial.print(minRX); Serial.print(" - "); Serial.println(maxRX);
  Serial.print("  RY: "); Serial.print(minRY); Serial.print(" - "); Serial.println(maxRY);
}

// Convert raw ADC to a normalized -1.0..+1.0 float using this
// axis's OWN calibrated center AND its OWN calibrated min/max,
// instead of assuming a fixed 0-4095 range that the pot never
// actually reaches.
float normalizeAxis(int raw, int center, int minVal, int maxVal) {
  int diff = raw - center;

  if (abs(diff) < DEADZONE) {
    return 0.0f;
  }

  float norm;
  if (diff >= 0) {
    int posRange = maxVal - center;
    if (posRange < 1) posRange = 1;
    norm = (float)diff / (float)posRange;
  } else {
    int negRange = center - minVal;
    if (negRange < 1) negRange = 1;
    norm = (float)diff / (float)negRange;
  }

  return constrain(norm, -1.0f, 1.0f);
}

// Take a normalized X/Y pair (-1..1 each) and scale to
// -32767..32767, clamping the COMBINED vector length to a unit
// circle. Without this, a stick with square/diamond mechanical
// travel lets X and Y both hit +-1 at once near the corners
// (combined magnitude sqrt(2) =~ 1.41), which is exactly the
// "spikes past the circle" pattern the tester showed as 23%
// circle error. Clamping the vector here forces every angle to
// reach, at most, the same output radius.
void circularizeAndScale(float nx, float ny, int16_t &outX, int16_t &outY) {
  float mag = sqrtf(nx * nx + ny * ny);
  if (mag > 1.0f) {
    nx /= mag;
    ny /= mag;
  }
  outX = (int16_t)constrain(nx * 32767.0f, -32767.0f, 32767.0f);
  outY = (int16_t)constrain(ny * 32767.0f, -32767.0f, 32767.0f);
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

  // NEW: full range calibration. Sticks need to be rotated around
  // their outer edge during this window (4s) for it to capture
  // real min/max travel - this is what fixes circle error.
  calibrateRange();

  Serial.println("Advertising...");
}

void loop() {
  if (bleGamepad.isConnected()) {

    // ---------- Buttons ----------
    bool buttonChanged = false;
    for (int i = 0; i < numButtons; i++) {
      bool pressed = (digitalRead(buttons[i].pin) == LOW); // active LOW
      if (pressed != buttons[i].lastState) {
        if (pressed) {
          bleGamepad.press(buttons[i].bleButton);
        } else {
          bleGamepad.release(buttons[i].bleButton);
        }
        buttons[i].lastState = pressed;
        buttonChanged = true;
      }
    }

    // (D-pad is now handled above as regular buttons 13-16)

    // ---------- Analog Sticks ----------
    // Oversampled reads, normalized around each axis's own
    // calibrated center + real min/max, then circularized per
    // stick so the L and R pairs each get clamped to a unit
    // circle before scaling to output range. This is the fix for
    // the square-shaped trace / high circle error on the tester.
    float nlx = normalizeAxis(readAveraged(PIN_LSTICK_X), centerLX, minLX, maxLX);
    float nly = normalizeAxis(readAveraged(PIN_LSTICK_Y), centerLY, minLY, maxLY);
    float nrx = normalizeAxis(readAveraged(PIN_RSTICK_X), centerRX, minRX, maxRX);
    float nry = normalizeAxis(readAveraged(PIN_RSTICK_Y), centerRY, minRY, maxRY);

    int16_t lx, ly, rx, ry;
    circularizeAndScale(nlx, nly, lx, ly);
    circularizeAndScale(nrx, nry, rx, ry);

    // ---------- Triggers (LT/RT are plain switches, not analog) ----------
    // Digital press/release already handled in the buttons loop above
    // (they're in the buttons[] array). Here we just derive a simple
    // "full/none" slider value from that same digital state, purely
    // for games/testers that expect an analog trigger axis.
    int16_t lt = (digitalRead(PIN_LT) == LOW) ? 32767 : 0;
    int16_t rt = (digitalRead(PIN_RT) == LOW) ? 32767 : 0;

    // ---------- Only send when something actually changed ----------
    // Sending unconditionally every 10ms outruns the BLE connection
    // interval on many hosts, overflowing the notify queue and
    // silently dropping packets - producing sparse, jumpy reports
    // instead of a smooth stream (visible as a "starburst" pattern
    // in circle-test tools, plus a Report Rate that reads ~0Hz).
    bool changed =
      buttonChanged ||
      (abs(lx - lastLX) > AXIS_CHANGE_THRESHOLD) ||
      (abs(ly - lastLY) > AXIS_CHANGE_THRESHOLD) ||
      (abs(rx - lastRX) > AXIS_CHANGE_THRESHOLD) ||
      (abs(ry - lastRY) > AXIS_CHANGE_THRESHOLD) ||
      (lt != lastLT) ||
      (rt != lastRT);

    if (changed) {
      // Verified setAxes signature: (x, y, z, rX, rY, rZ, slider1, slider2).
      // Confirmed working placement (matches tested reference sketch and
      // the browser tester reading HID report index 4 = rX): ry goes in
      // the rX slot (4th argument), not rZ.
      bleGamepad.setAxes(lx, ly, rx, ry, 0, 0, lt, rt);
      bleGamepad.sendReport();

      lastLX = lx; lastLY = ly; lastRX = rx; lastRY = ry;
      lastLT = lt; lastRT = rt;
    }
  }

  delay(4); // poll fast, but we only *send* on real change (see above)
}
