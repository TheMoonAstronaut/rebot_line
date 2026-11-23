#include <Arduino.h>
#include <Seeed_GFX.h>
#include <esp_mac.h>
#include <string_view>
#include "board/boards/XIAO_LCD_Board.h"
#include "driver/tft/Driver_ST7789.h"
#include "panel/Panel_TFT.h"

static_assert(D6 == 43 && D11 == 38 && D12 == 39 && D13 == 40,
              "Select XIAO ESP32-S3 Plus");

Seeed_GFX display;
Seeed_Sprite frame;
char deviceId[13] = {};
char line[96];
size_t lineLength = 0;
bool lineOverflow = false;
bool previousRaw = false, stable = false;
uint32_t changedAt = 0, sequence = 0;
bool requestPending = false;
uint32_t requestSentAt = 0;

bool draw(const char* status, const char* detail, const char* hint) {
  if (!frame.created()) return false;
  constexpr uint16_t background = 0x0863, card = 0x10E5;
  constexpr uint16_t white = 0xEF9F, cyan = 0x3F39, muted = 0x8D34;
  frame.fillSprite(background);
  frame.setTextFont(1);
  frame.setTextWrap(false);
  frame.setTextColor(white);
  frame.setTextSize(2);
  frame.setCursor(9, 11);
  frame.print("reBot");
  frame.setTextSize(1);
  frame.setTextColor(muted);
  frame.setCursor(9, 35);
  frame.print("USB DIAGNOSTICS");
  frame.fillRoundRect(7, 55, 121, 102, 6, card);
  frame.setTextColor(cyan);
  frame.setCursor(14, 70);
  frame.print(status);
  frame.setTextColor(white);
  frame.setCursor(14, 92);
  frame.print(detail);
  frame.setTextColor(muted);
  frame.setCursor(14, 124);
  frame.print(hint);
  frame.setCursor(9, 180);
  frame.print("USR1: QUERY AXIS");
  frame.setCursor(9, 213);
  frame.print("PANEL_S3");
  return frame.pushSprite(0, 0);
}

void requestStatus(const char* source) {
  if (requestPending) return;
  if (++sequence == 0) ++sequence;
  requestPending = true;
  requestSentAt = millis();
  Serial.printf("{\"type\":\"TEST_REQUEST\",\"seq\":%lu,\"source\":\"%s\"}\n",
                static_cast<unsigned long>(sequence), source);
  draw("REQUEST SENT", "Waiting for PC", "via USB serial");
}

void acceptLine(std::string_view request) {
  if (request == R"({"cmd":"HELLO"})") {
    Serial.printf("{\"type\":\"HELLO\",\"role\":\"PANEL_S3\",\"device_id\":\"%s\","
                  "\"protocol\":1,\"firmware\":\"panel_s3_diagnostic\",\"display_ready\":%s}\n",
                  deviceId, frame.created() ? "true" : "false");
    return;
  }
  if (request == R"({"cmd":"TEST"})") {
    requestStatus("PC");
    return;
  }

  unsigned long received = 0;
  int negative = -1, positive = -1, home = -1, consumed = 0;
  if (sscanf(request.data(), R"({"cmd":"RESULT","seq":%lu,"negative":%d,"positive":%d,"home":%d}%n)",
             &received, &negative, &positive, &home, &consumed) == 4 &&
      static_cast<size_t>(consumed) == request.size() && requestPending && received == sequence &&
      (negative == 0 || negative == 1) && (positive == 0 || positive == 1) &&
      (home == 0 || home == 1)) {
    char inputs[24];
    snprintf(inputs, sizeof(inputs), "L:%d  R:%d  H:%d", negative, positive, home);
    const bool drawn = draw("AXIS LOCKED", inputs, "C3 reply received");
    requestPending = false;
    Serial.printf("{\"type\":\"RESULT_SHOWN\",\"seq\":%lu,\"lcd_drawn\":%s}\n",
                  received, drawn ? "true" : "false");
  } else {
    Serial.println(F("{\"type\":\"ERROR\",\"code\":\"UNSUPPORTED_REQUEST\"}"));
  }
}

void setup() {
  // The former motor pads stay as GPIO inputs on the panel board.
  pinMode(D11, INPUT);
  pinMode(D12, INPUT);
  pinMode(D13, INPUT);
  pinMode(D6, INPUT_PULLUP);
  previousRaw = stable = digitalRead(D6) == LOW;
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);

  uint8_t mac[6];
  if (esp_efuse_mac_get_default(mac) == ESP_OK) {
    snprintf(deviceId, sizeof(deviceId), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
  if (display.begin<Board_XIAO_1inch14_LCD<13, 12>, Config_Seeed_1inch14_LCD_ST7789>()) {
    display.setRotation(0);
    frame.setColorDepth(16);
    if (frame.createSprite(display, 135, 240)) draw("USB READY", "Awaiting PC", "Diagnostic mode");
  }
}

void loop() {
  const bool raw = digitalRead(D6) == LOW;
  const uint32_t now = millis();
  if (requestPending && now - requestSentAt >= 5000) {
    requestPending = false;
    draw("USB TIMEOUT", "No axis reply", "Press USR1 to retry");
  }
  if (raw != previousRaw) {
    previousRaw = raw;
    changedAt = now;
  }
  if (raw != stable && now - changedAt >= 20) {
    stable = raw;
    if (stable) requestStatus("USR1");
  }

  while (Serial.available() > 0) {
    const int byte = Serial.read();
    if (byte < 0) break;
    if (byte == '\n') {
      if (lineOverflow) {
        Serial.println(F("{\"type\":\"ERROR\",\"code\":\"LINE_TOO_LONG\"}"));
      } else {
        if (lineLength > 0 && line[lineLength - 1] == '\r') --lineLength;
        line[lineLength] = '\0';
        if (lineLength > 0) acceptLine(std::string_view(line, lineLength));
      }
      lineLength = 0;
      lineOverflow = false;
    } else if (lineLength + 1 < sizeof(line)) {
      line[lineLength++] = static_cast<char>(byte);
    } else {
      lineOverflow = true;
    }
  }
  delay(1);
}
