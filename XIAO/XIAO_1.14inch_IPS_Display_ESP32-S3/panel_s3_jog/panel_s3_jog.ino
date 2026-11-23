#include <Arduino.h>
#include <Seeed_GFX.h>
#include "board/boards/XIAO_LCD_Board.h"
#include "driver/tft/Driver_ST7789.h"
#include "panel/Panel_TFT.h"
#include "../../common/UsbJog.h"
#include "../xiao_pulse_trial/Dashboard.h"
#include "../xiao_pulse_trial/MotionTelemetry.h"
#include "PanelControl.h"

using namespace pulse_trial;
static_assert(D6 == 43 && D7 == 44 && D19 == 11 && D11 == 38 && D12 == 39 && D13 == 40,
              "Select XIAO ESP32-S3 Plus");

Seeed_GFX display;
Seeed_Sprite frame;
usb_jog::LineReader reader;
bool displayReady = false, helloSeen = false;

struct AxisStatus {
  State state = State::Fault;
  bool reverse = false, linked = false;
  uint32_t hz = 0, at = 0;
  uint64_t forward = 0, back = 0;
  uint8_t raw = 7, recovery = 0;
  bool homed = false;
  axis_control::Home home = axis_control::Home::Unhomed;
  axis_control::Fault fault = axis_control::Fault::None;
  int64_t position = 0;
  bool zeroed = false;
  uint32_t zeroSequence = 0;
};
AxisStatus published;
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;

void receive(std::string_view line) {
  if (line == R"({"cmd":"HELLO"})") {
    if (displayReady) {
      usb_jog::hello("PANEL_S3", "panel_s3_jog", axis_control::PROTOCOL);
      helloSeen = true;
    }
    return;
  }
  unsigned state = 0, reverse = 0, linked = 0;
  unsigned long hz = 0;
  unsigned long long forward = 0, back = 0;
  unsigned raw = 7, recovery = 0, homed = 0;
  unsigned home = 0, fault = 0;
  long long position = 0;
  unsigned zeroed = 0;
  unsigned long zeroSequence = 0;
  int used = 0;
  if (sscanf(line.data(), R"({"cmd":"AXIS","state":%u,"rev":%u,"linked":%u,"hz":%lu,"fwd":%llu,"back":%llu,"raw":%u,"recovery":%u,"homed":%u,"home":%u,"fault":%u,"zeroed":%u,"zero":%lu,"pos":%lld}%n)",
             &state, &reverse, &linked, &hz, &forward, &back, &raw, &recovery, &homed, &home, &fault, &zeroed, &zeroSequence, &position, &used) != 14 ||
      static_cast<size_t>(used) != line.size() || state > unsigned(State::Fault) ||
      reverse > 1 || linked > 1 || hz > 8000 || forward > INT64_MAX || back > INT64_MAX ||
      raw > 7 || recovery > 3 || homed > 1 || home > unsigned(axis_control::Home::EndClear) ||
      fault > unsigned(axis_control::Fault::PositiveEnd) || zeroed > 1 ||
      bool(homed) != (home == unsigned(axis_control::Home::Homed)) || (homed && (fault || !zeroed))) return;
  const AxisStatus next{static_cast<State>(state), bool(reverse), bool(linked),
                        uint32_t(hz), millis(), forward, back, uint8_t(raw), uint8_t(recovery),
                        bool(homed), static_cast<axis_control::Home>(home),
                        static_cast<axis_control::Fault>(fault), int64_t(position), bool(zeroed), uint32_t(zeroSequence)};
  portENTER_CRITICAL(&statusMux);
  published = next;
  portEXIT_CRITICAL(&statusMux);
}

void panelTask(void*) {
  uint32_t lastSent = 0, sequence = 0;
  unsigned sentMask = 0;
  while (true) {
    reader.poll(receive);
    const uint32_t now = millis();
    const unsigned mask = (digitalRead(D6) == LOW ? 1 : 0) |
                          (digitalRead(D7) == LOW ? 2 : 0) |
                          (digitalRead(D19) == LOW ? 4 : 0);
    if (helloSeen && (mask != sentMask || now - lastSent >= usb_jog::KEYS_INTERVAL_MS)) {
      if (usb_jog::send("{\"type\":\"KEYS\",\"seq\":%lu,\"mask\":%u}\n",
                        static_cast<unsigned long>(sequence + 1), mask)) {
        ++sequence;
        lastSent = now;
        sentMask = mask;
      }
    }
    vTaskDelay(1);
  }
}

void setup() {
  pinMode(D11, INPUT);
  pinMode(D12, INPUT);
  pinMode(D13, INPUT);
  pinMode(D6, INPUT_PULLUP);
  pinMode(D7, INPUT_PULLUP);
  pinMode(D19, INPUT_PULLUP);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  if (!display.begin<Board_XIAO_1inch14_LCD<13, 12>, Config_Seeed_1inch14_LCD_ST7789>()) return;
  display.setRotation(0);
  frame.setColorDepth(16);
  if (!frame.createSprite(display, 135, 240)) return;
  displayReady = true;
  xTaskCreatePinnedToCore(panelTask, "panel-usb", 4096, nullptr, 3, nullptr, 0);
}

void loop() {
  if (!displayReady) { delay(100); return; }
  static uint32_t lastDraw = 0;
  static bool drawn = false;
  static AxisStatus previous;
  static double halfRange = 100;
  const uint32_t now = millis();
  AxisStatus status;
  portENTER_CRITICAL(&statusMux);
  status = published;
  portEXIT_CRITICAL(&statusMux);
  const bool online = status.at != 0 && now - status.at < 500 && status.linked;
  const char* fault = online ? axis_control::faultText(status.fault) : "USB LINK LOST";
  if (!online) {
    status.state = State::Fault;
    status.hz = 0;
  }
  status.linked = online;
  if (!drawn || status.state != previous.state ||
      status.linked != previous.linked ||
      status.raw != previous.raw || status.recovery != previous.recovery ||
      status.home != previous.home || status.homed != previous.homed || status.fault != previous.fault ||
      status.zeroed != previous.zeroed || status.zeroSequence != previous.zeroSequence ||
      (now - lastDraw >= 100 && (status.forward != previous.forward ||
                                 status.back != previous.back || status.hz != previous.hz ||
                                 status.position != previous.position))) {
    constexpr double mm = MotionTelemetry::MM_PER_PULSE;
    const double position = status.position * mm;
    while (fabs(position) > halfRange) halfRange *= 2;
    const DashboardData data{status.state, status.reverse, position, status.forward * mm,
                             status.back * mm, status.hz * mm, halfRange, fault};
    dashboard::render(frame, data);
    // Replace the legacy no-sensor labels with C3-reported physical inputs.
    frame.fillRect(75, 4, 60, 14, dashboard::BACKGROUND);
    dashboard::text(frame, 79, 7, !online ? "LINK?" : status.fault != axis_control::Fault::None ? "FAULT" : "LIMITS",
                    online && status.fault == axis_control::Fault::None ? dashboard::CYAN : dashboard::RED);
    if (online && (panel_control::homing(status.home) || status.home == axis_control::Home::Release)) {
      frame.fillRect(28, 27, 99, 18, dashboard::PANEL);
      dashboard::text(frame, 30, 32, axis_control::homeText(status.home), dashboard::AMBER);
    }
    frame.fillRect(8, 99, 119, 12, dashboard::PANEL);
    const char* reference = !online ? "REFERENCE UNKNOWN" : status.fault != axis_control::Fault::None ? fault :
                            panel_control::homing(status.home) || status.home == axis_control::Home::Release ? axis_control::homeText(status.home) :
                            status.zeroed && !status.homed ? "ORIGIN ZERO" :
                            !axis_control::HOMING_ENABLED ? "RELATIVE / EST." : axis_control::homeText(status.home);
    dashboard::text(frame, 10, 101, reference, status.homed && online ? dashboard::CYAN : dashboard::AMBER);
    frame.fillRect(7, 208, 121, 13, dashboard::BACKGROUND);
    char inputs[24];
    if (online) snprintf(inputs, sizeof(inputs), "N:%u H:%u P:%u",
                         !(status.raw & 1), !(status.raw & 2), !(status.raw & 4));
    else snprintf(inputs, sizeof(inputs), "N:? H:? P:?");
    dashboard::text(frame, 8, 211, inputs, online && status.raw == 7 ? dashboard::CYAN : dashboard::AMBER);
    frame.fillRect(0, 226, 135, 14, dashboard::BACKGROUND);
    const char* hint = panel_control::hint(status.state, online, status.fault, status.home, status.recovery, status.raw);
    dashboard::text(frame, 67 - int(strlen(hint)) * 3, 229, hint, dashboard::AMBER);
    frame.pushSprite(0, 0);
    previous = status;
    lastDraw = now;
    drawn = true;
  }
  delay(1);
}
