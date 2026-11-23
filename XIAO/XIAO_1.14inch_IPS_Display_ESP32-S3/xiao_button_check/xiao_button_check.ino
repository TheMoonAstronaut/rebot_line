// Stage 1: display and physical buttons only. Never generates STEP pulses.
// Keep the motor driver's 24 V supply OFF throughout this diagnostic.
#include <Arduino.h>
#include <Seeed_GFX.h>
#include "board/boards/XIAO_LCD_Board.h"
#include "driver/tft/Driver_ST7789.h"
#include "panel/Panel_TFT.h"

static_assert(D6 == 43 && D7 == 44 && D19 == 11,
              "Select XIAO_ESP32S3_PLUS, not the regular XIAO board");
static_assert(D11 == 38 && D12 == 39 && D13 == 40,
              "Motor pins must use the Plus variant mapping");

constexpr uint32_t DEBOUNCE_MS = 20;
constexpr uint32_t HEARTBEAT_MS = 2000;
constexpr uint16_t BACKGROUND = 0x0841;
constexpr uint16_t CARD = 0x18E3;

struct Button {
  uint8_t pin;
  bool rawPressed = false;
  bool pressed = false;
  uint32_t changedAt = 0;
  uint32_t pressCount = 0;

  void begin() {
    pinMode(pin, INPUT_PULLUP);
    rawPressed = pressed = digitalRead(pin) == LOW;
    changedAt = millis();
  }

  bool update(uint32_t now) {
    const bool sample = digitalRead(pin) == LOW;
    if (sample != rawPressed) {
      rawPressed = sample;
      changedAt = now;
    }
    if (pressed != rawPressed && now - changedAt >= DEBOUNCE_MS) {
      pressed = rawPressed;
      if (pressed) ++pressCount;
      return true;
    }
    return false;
  }
};

enum class Preview { RELEASE_KEYS, IDLE, FORWARD, REVERSE, LOCKED };

Seeed_GFX display;
Button usr1{D6};
Button usr2{D7};
Button usr3{D19};
Preview preview = Preview::RELEASE_KEYS;
bool displayReady = false;
bool releasedSinceBoot = false;
bool stopLatched = false;
uint32_t lastHeartbeat = 0;

const char* previewName(Preview value) {
  switch (value) {
    case Preview::RELEASE_KEYS: return "RELEASE KEYS";
    case Preview::IDLE: return "IDLE";
    case Preview::FORWARD: return "FORWARD";
    case Preview::REVERSE: return "REVERSE";
    case Preview::LOCKED: return "STOP LOCKED";
  }
  return "UNKNOWN";
}

Preview updatePreview() {
  const bool allReleased = !usr1.pressed && !usr2.pressed && !usr3.pressed;
  if (!releasedSinceBoot) {
    if (!allReleased) return Preview::RELEASE_KEYS;
    releasedSinceBoot = true;
  }
  if (usr3.pressed || (usr1.pressed && usr2.pressed)) stopLatched = true;
  if (stopLatched) {
    if (!allReleased) return Preview::LOCKED;
    stopLatched = false;
  }
  if (usr1.pressed) return Preview::FORWARD;
  if (usr2.pressed) return Preview::REVERSE;
  return Preview::IDLE;
}

void drawButton(int16_t y, const char* label, const Button& button) {
  const int16_t width = display.width() - 12;
  display.fillRoundRect(6, y, width, 42, 5, button.pressed ? TFT_DARKGREEN : CARD);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE);
  display.setCursor(12, y + 7);
  display.print(label);
  display.setCursor(12, y + 23);
  display.print(button.pressed ? "DOWN" : "UP");
  display.print("  #");
  display.print(button.pressCount);
}

void drawStatus() {
  if (!displayReady) return;
  drawButton(57, "USR1  FORWARD", usr1);
  drawButton(105, "USR2  REVERSE", usr2);
  display.fillRect(0, 158, display.width(), 42, BACKGROUND);
  display.setTextSize(1);
  display.setTextColor(preview == Preview::LOCKED ? TFT_ORANGE : TFT_CYAN,
                       BACKGROUND);
  display.setCursor(8, 160);
  display.print("INPUT PREVIEW");
  display.setCursor(8, 177);
  display.print(previewName(preview));
  display.fillRect(0, 207, display.width(), 16, BACKGROUND);
  display.setCursor(8, 209);
  display.setTextColor(TFT_WHITE, BACKGROUND);
  display.print("USR3 ");
  display.print(usr3.pressed ? "DOWN" : "UP");
}

void report(const char* event) {
  // Diagnostics may be dropped; waiting for a USB reader must never stall keys.
  if (!Serial) return;
  char line[256];
  const int length = snprintf(line, sizeof(line),
                "%s ms=%lu lcd=%s usr1=%u usr2=%u usr3=%u "
                "count1=%lu count2=%lu count3=%lu preview=%s step_pulses=0\n",
                event, static_cast<unsigned long>(millis()),
                displayReady ? "OK" : "FAIL",
                usr1.pressed, usr2.pressed, usr3.pressed,
                static_cast<unsigned long>(usr1.pressCount),
                static_cast<unsigned long>(usr2.pressCount),
                static_cast<unsigned long>(usr3.pressCount), previewName(preview));
  if (length > 0 && length < static_cast<int>(sizeof(line)) &&
      Serial.availableForWrite() >= length) {
    Serial.write(reinterpret_cast<const uint8_t*>(line), length);
  }
}

void setup() {
  // Preload fixed levels before enabling output. EN=HIGH matches the planned
  // active-low setup, but the motor supply must remain OFF because its actual
  // EN configuration has not been verified. No motor control API is present.
  digitalWrite(D12, LOW);
  pinMode(D12, OUTPUT);
  digitalWrite(D13, LOW);
  pinMode(D13, OUTPUT);
  digitalWrite(D11, HIGH);
  pinMode(D11, OUTPUT);

  usr1.begin();
  usr2.begin();
  usr3.begin();
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  // Do not wait for the USB monitor: the screen also works standalone.
  displayReady = display.begin<Board_XIAO_1inch14_LCD<13, 12>,
                                Config_Seeed_1inch14_LCD_ST7789>();
  if (displayReady) {
    display.setRotation(0);
    display.setTextFont(1);
    display.setTextWrap(false);
    display.fillScreen(BACKGROUND);
    display.setTextSize(2);
    display.setTextColor(TFT_WHITE, BACKGROUND);
    display.setCursor(8, 8);
    display.print("KEY CHECK");
    display.setTextSize(1);
    display.setTextColor(TFT_YELLOW, BACKGROUND);
    display.setCursor(8, 35);
    display.print("NO MOTOR PULSES");
    display.setCursor(8, 229);
    display.setTextColor(TFT_WHITE, BACKGROUND);
    display.print("Keep 24V OFF");
  } else {
    if (Serial) Serial.printf("DISPLAY_ERROR %s\n", display.lastResult().message);
  }
  preview = updatePreview();
  drawStatus();
  report("BOOT");
}

void loop() {
  const uint32_t now = millis();
  bool changed = usr1.update(now);
  changed = usr2.update(now) || changed;
  changed = usr3.update(now) || changed;
  if (changed) {
    preview = updatePreview();
    drawStatus();
    report("KEY");
  }
  if (now - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = now;
    report("HEARTBEAT");
  }
  delay(1);
}
