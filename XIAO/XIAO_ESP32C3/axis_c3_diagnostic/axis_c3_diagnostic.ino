#include <Arduino.h>
#include <esp_mac.h>
#include <string_view>

static_assert(D1 == 3 && D2 == 4 && D3 == 5 && D4 == 6 && D5 == 7 && D10 == 10,
              "Select XIAO ESP32C3");

constexpr uint8_t STEP_PIN = D2;
constexpr uint8_t DIR_PIN = D1;
constexpr uint8_t ENABLE_PIN = D3;
constexpr uint8_t LIMIT_NEG_PIN = D4;
constexpr uint8_t LIMIT_POS_PIN = D10;
constexpr uint8_t HOME_PIN = D5;

char deviceId[13] = {};
char line[64];
size_t lineLength = 0;
bool lineOverflow = false;

void reply(std::string_view request) {
  if (request == R"({"cmd":"HELLO"})") {
    Serial.printf("{\"type\":\"HELLO\",\"role\":\"AXIS_C3\",\"device_id\":\"%s\","
                  "\"protocol\":1,\"firmware\":\"axis_c3_diagnostic\"}\n", deviceId);
  } else if (request == R"({"cmd":"STATUS"})") {
    Serial.printf("{\"type\":\"STATUS\",\"state\":\"LOCKED\",\"motion_available\":false,"
                  "\"pulses\":0,\"limit_neg_raw\":%d,\"limit_pos_raw\":%d,\"home_raw\":%d,"
                  "\"limit_neg_gpio\":%u,\"limit_pos_gpio\":%u,\"home_gpio\":%u}\n",
                  digitalRead(LIMIT_NEG_PIN), digitalRead(LIMIT_POS_PIN), digitalRead(HOME_PIN),
                  unsigned(LIMIT_NEG_PIN), unsigned(LIMIT_POS_PIN), unsigned(HOME_PIN));
  } else {
    Serial.println(F("{\"type\":\"ERROR\",\"code\":\"UNSUPPORTED_REQUEST\"}"));
  }
}

void setup() {
  // Diagnostics never drive the motor terminals. Driver EN polarity is not
  // assumed, and the 24 V supply stays off during this diagnostic run.
  pinMode(STEP_PIN, INPUT);
  pinMode(DIR_PIN, INPUT);
  pinMode(ENABLE_PIN, INPUT);

  // Unconnected inputs normally report HIGH. These are raw electrical levels,
  // not interpreted active/inactive switch states.
  pinMode(LIMIT_NEG_PIN, INPUT_PULLUP);
  pinMode(LIMIT_POS_PIN, INPUT_PULLUP);
  pinMode(HOME_PIN, INPUT_PULLUP);

  uint8_t mac[6];
  if (esp_efuse_mac_get_default(mac) == ESP_OK) {
    snprintf(deviceId, sizeof(deviceId), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
  Serial.begin(115200);
}

void loop() {
  while (Serial.available() > 0) {
    const int byte = Serial.read();
    if (byte < 0) break;
    if (byte == '\n') {
      if (lineOverflow) {
        Serial.println(F("{\"type\":\"ERROR\",\"code\":\"LINE_TOO_LONG\"}"));
      } else {
        if (lineLength > 0 && line[lineLength - 1] == '\r') --lineLength;
        if (lineLength > 0) reply(std::string_view(line, lineLength));
      }
      lineLength = 0;
      lineOverflow = false;
    } else if (lineLength < sizeof(line)) {
      line[lineLength++] = static_cast<char>(byte);
    } else {
      lineOverflow = true;
    }
  }
  delay(1);
}
