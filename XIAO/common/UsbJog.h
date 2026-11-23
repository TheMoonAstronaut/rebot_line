#pragma once

#include <Arduino.h>
#include <esp_mac.h>
#include <stdarg.h>
#include <string_view>

namespace usb_jog {
constexpr uint32_t KEYS_INTERVAL_MS = 20;
constexpr uint32_t KEYS_TIMEOUT_MS = 150;
constexpr uint32_t REPORT_INTERVAL_MS = 100;

class LineReader {
 public:
  template <class Handler>
  void poll(Handler handle) {
    // Give the motor task a chance to check its timeout even with continuous RX.
    for (int budget = 256; budget > 0 && Serial.available() > 0; --budget) {
      const int byte = Serial.read();
      if (byte < 0) break;
      if (byte == '\n') {
        if (!overflow_) {
          if (length_ && line_[length_ - 1] == '\r') --length_;
          line_[length_] = '\0';
          if (length_) handle(std::string_view(line_, length_));
        }
        length_ = 0;
        overflow_ = false;
      } else if (length_ < sizeof(line_) - 1) {
        line_[length_++] = static_cast<char>(byte);
      } else {
        overflow_ = true;
      }
    }
  }

 private:
  char line_[384]{};
  size_t length_ = 0;
  bool overflow_ = false;
};

inline bool send(const char* format, ...) {
  char line[384];
  va_list args;
  va_start(args, format);
  const int size = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (size <= 0 || size >= static_cast<int>(sizeof(line)) ||
      Serial.availableForWrite() < size) return false;
  return Serial.write(reinterpret_cast<const uint8_t*>(line), size) == static_cast<size_t>(size);
}

inline void hello(const char* role, const char* firmware, unsigned protocol = 1) {
  uint8_t mac[6]{};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) return;
  send("{\"type\":\"HELLO\",\"role\":\"%s\",\"device_id\":\"%02X%02X%02X%02X%02X%02X\","
       "\"protocol\":%u,\"firmware\":\"%s\"}\n", role,
       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], protocol, firmware);
}
}  // namespace usb_jog
