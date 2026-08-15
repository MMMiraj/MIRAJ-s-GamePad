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

// Analog deadzone (raw ADC 0-4095, center ~2048), used only by
// the analog sticks (left/right thumbsticks), not LT/RT.
const int DEADZONE = 80;

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
  bleGamepadConfig.setIncludeRxAxis(false);
  bleGamepadConfig.setIncludeRyAxis(false);
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

  Serial.println("MIRAJ's Controller - BLE Gamepad started. Advertising...");
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
    int16_t lx = mapStick(analogRead(PIN_LSTICK_X));
    int16_t ly = mapStick(analogRead(PIN_LSTICK_Y));
    int16_t rx = mapStick(analogRead(PIN_RSTICK_X));
    int16_t ry = mapStick(analogRead(PIN_RSTICK_Y));

    // TEMP DEBUG: confirm the computed value leaving the ESP32, independent
    // of whatever the host/tester shows. Remove once RY is confirmed working.
    Serial.print("ry raw="); Serial.print(analogRead(PIN_RSTICK_Y));
    Serial.print(" mapped="); Serial.println(ry);

    // ---------- Triggers (LT/RT are plain switches, not analog) ----------
    // Digital press/release already handled in the buttons loop above
    // (they're in the buttons[] array). Here we just derive a simple
    // "full/none" slider value from that same digital state, purely
    // for games/testers that expect an analog trigger axis.
    int16_t lt = (digitalRead(PIN_LT) == LOW) ? 32767 : 0;
    int16_t rt = (digitalRead(PIN_RT) == LOW) ? 32767 : 0;

    // Verified setAxes signature: (x, y, z, rX, rY, rZ, slider1, slider2).
    // Right stick Y is enabled via setIncludeRzAxis(true), so ry must
    // go in the 6th argument (rZ) - rX/rY (4th/5th) are disabled above.
    bleGamepad.setAxes(lx, ly, rx, 0, 0, ry, lt, rt);

    // ---------- Push report over BLE ----------
    bleGamepad.sendReport();
  }

  delay(10); // ~100Hz polling rate
}

// Convert raw ADC (0-4095) to signed axis range (-32767 to 32767)
// with a small deadzone around center.
int16_t mapStick(int raw) {
  int center = 2048;
  int diff = raw - center;

  if (abs(diff) < DEADZONE) {
    diff = 0;
  }

  long mapped = map(diff, -2048, 2047, -32767, 32767);
  return (int16_t)constrain(mapped, -32767, 32767);
}
