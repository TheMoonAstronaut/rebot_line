// Manual jogging, no physical end-limit input. Keep 24 V OFF during upload.
#include <Arduino.h>
#include <Seeed_GFX.h>
#include "board/boards/XIAO_LCD_Board.h"
#include "driver/tft/Driver_ST7789.h"
#include "panel/Panel_TFT.h"
#include "driver/gpio.h"
#include "JogTrial.h"
#include "StepPulseTimer.h"
#include "MotionTelemetry.h"
#include "Dashboard.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static_assert(D6 == 43 && D7 == 44 && D19 == 11 && D11 == 38 &&
              D12 == 39 && D13 == 40, "Select XIAO ESP32-S3 PLUS");

using pulse_trial::JogTrial;
using pulse_trial::Keys;
using pulse_trial::State;
using pulse_trial::RunMode;
// Target: 80 mm/s at 37.5 mm/rev and 3200 pulses/rev (1.8° motor, 16 microsteps).
constexpr pulse_trial::TrialConfig TRIAL_PROFILE{20, 6827, 2000, 0, 0, RunMode::HoldToRun};
constexpr bool HOLD_MODE = TRIAL_PROFILE.mode == RunMode::HoldToRun;
// Reverse physical travel for both buttons without changing their stop policy.
constexpr bool DIRECTION_INVERTED = true;

struct DebouncedKey {
  uint8_t pin;
  bool raw = false, stable = false;
  uint32_t changedAt = 0;
  void begin() {
    pinMode(pin, INPUT_PULLUP);
    raw = stable = digitalRead(pin) == LOW;
    changedAt = millis();
  }
  void poll(uint32_t now) {
    const bool value = digitalRead(pin) == LOW;
    if (value != raw) { raw = value; changedAt = now; }
    if (now - changedAt >= 20) stable = raw;
  }
};

Seeed_GFX display;
Seeed_Sprite dashboardFrame;
DebouncedKey usr1{D6}, usr2{D7}, usr3{D19};
JogTrial trial{TRIAL_PROFILE};
StepPulseTimer stepOutput{D12};
pulse_trial::MotionTelemetry motion;
bool displayReady = false, timerReady = false;
bool appliedEnable = false, appliedDirection = false;
uint32_t lastDraw = 0, lastReport = 0;
State previousDrawState = State::Fault;
uint32_t previousDrawCount = UINT32_MAX;
uint32_t previousDrawHz = UINT32_MAX;
const char* faultReason = "NONE";
uint32_t controlLastTick = 0, controlMaxGap = 0, lastDrawMs = 0;

// The control task owns inputs, jog state and EN/DIR. The timer owns STEP.
// Rendering and USB logging consume a consistent copy without holding the
// lock during any display, serial or driver operation.
struct Snapshot {
  State state = State::Boot;
  Keys raw{};
  uint32_t count = 0, total = 0, maxGap = 0, targetHz = 0;
  bool reverse = false, enabled = false, timerReady = false;
  bool dirPinHigh = false, enPinHigh = false;
  const char* fault = "NONE";
  int64_t positionPulses = 0;
  uint64_t forwardPulses = 0, reversePulses = 0;
  uint32_t pulseHz = 0;
  double viewHalfMm = 100;
};
portMUX_TYPE snapshotMux = portMUX_INITIALIZER_UNLOCKED;
Snapshot published;

void publishStatus() {
  const Snapshot next{trial.state(), Keys{usr1.raw, usr2.raw, usr3.raw},
                      trial.count(), trial.total(), controlMaxGap, trial.targetHz(millis()),
                      trial.reverse(), appliedEnable, timerReady,
                      digitalRead(D13) == HIGH, digitalRead(D11) == HIGH, faultReason,
                      motion.positionPulses(), motion.forwardPulses(), motion.reversePulses(),
                      motion.pulseHz(), motion.viewHalfMm()};
  portENTER_CRITICAL(&snapshotMux);
  published = next;
  portEXIT_CRITICAL(&snapshotMux);
}

Snapshot readStatus() {
  portENTER_CRITICAL(&snapshotMux);
  const Snapshot copy = published;
  portEXIT_CRITICAL(&snapshotMux);
  return copy;
}

void controlTask(void*);

const char* stateName(State state) {
  switch (state) {
    case State::Boot: return "RELEASE KEYS";
    case State::Disarmed: return "LOCKED";
    case State::ArmRelease: return "RELEASE USR3";
    case State::Ready: return "READY";
    case State::Settling: return "START WAIT";
    case State::Forward: return "FORWARD";
    case State::Reverse: return "REVERSE";
    case State::Release: return "RELEASE KEYS";
    case State::Fault: return "FAULT";
  }
  return "UNKNOWN";
}

void applyOutputs(bool stepHigh) {
  // EN LOW is the vendor example's trial level, NOT a verified driver setting.
  const bool enable = trial.enabled();
  if (enable != appliedEnable) {
    digitalWrite(D11, enable ? LOW : HIGH);
    appliedEnable = enable;
  }
  // Direction changes occur during the state machine's settle interval.
  if (!stepHigh && trial.reverse() != appliedDirection) {
    digitalWrite(D13, (trial.reverse() != DIRECTION_INVERTED) ? HIGH : LOW);
    appliedDirection = trial.reverse();
  }
}

void failTrial(const char* reason, uint32_t now) {
  faultReason = reason;
  trial.fail(now);
  stepOutput.command(0, 0);
  applyOutputs(stepOutput.status().high);
}

void reportStatus(const char* event, const Snapshot& status) {
  if (!Serial) return;
  char line[512];
  const int size = snprintf(line, sizeof(line),
      "%s ms=%lu state=%s keys=%u%u%u queued=%lu total=%lu "
      "dir=%u dir_inv=%u dir_gpio=%u en_gpio=%u en_pin=%s timer=%s fault=%s target_hz=%lu profile=%s "
      "max_hz=%lu ramp_ms=%lu maxgap_ms=%lu draw_ms=%lu pos_mm=%.2f speed_mm_s=%.2f pulse_hz=%lu\n", event,
      static_cast<unsigned long>(millis()), stateName(status.state),
      status.raw.forward, status.raw.reverse, status.raw.stop,
      static_cast<unsigned long>(status.count),
      static_cast<unsigned long>(status.total), status.reverse,
      DIRECTION_INVERTED ? 1u : 0u,
      status.dirPinHigh ? 1u : 0u, status.enPinHigh ? 1u : 0u,
      status.enabled ? "LOW" : "HIGH", status.timerReady ? "OK" : "FAIL", status.fault,
      static_cast<unsigned long>(status.targetHz),
      HOLD_MODE ? "HOLD" : "LIMIT",
      static_cast<unsigned long>(TRIAL_PROFILE.maxHz),
      static_cast<unsigned long>(TRIAL_PROFILE.rampMs),
      static_cast<unsigned long>(status.maxGap), static_cast<unsigned long>(lastDrawMs),
      status.positionPulses * pulse_trial::MotionTelemetry::MM_PER_PULSE,
      status.pulseHz * pulse_trial::MotionTelemetry::MM_PER_PULSE,
      static_cast<unsigned long>(status.pulseHz));
  if (size > 0 && size < static_cast<int>(sizeof(line)) && Serial.availableForWrite() >= size)
    Serial.write(reinterpret_cast<const uint8_t*>(line), size);
}

void drawStatus(const Snapshot& status, uint32_t now, bool force = false) {
  if (!displayReady || !dashboardFrame.created()) return;
  const bool stateChanged = status.state != previousDrawState;
  if (!force && !stateChanged &&
      ((status.total == previousDrawCount && status.pulseHz == previousDrawHz) ||
       now - lastDraw < 100)) return;
  const uint32_t drawStarted = millis();
  constexpr double mm = pulse_trial::MotionTelemetry::MM_PER_PULSE;
  const pulse_trial::DashboardData data{
      status.state, status.reverse, status.positionPulses * mm,
      status.forwardPulses * mm, status.reversePulses * mm,
      status.pulseHz * mm, status.viewHalfMm, status.fault};
  pulse_trial::dashboard::render(dashboardFrame, data);
  dashboardFrame.pushSprite(0, 0);
  previousDrawState = status.state;
  previousDrawCount = status.total;
  previousDrawHz = status.pulseHz;
  lastDraw = now;
  lastDrawMs = millis() - drawStarted;
}

void setup() {
  // Preload the ESP32 output latches before driving the pins. Arduino
  // digitalWrite ignores pins that pinMode has not yet claimed as GPIO.
  gpio_set_level(static_cast<gpio_num_t>(D12), LOW);
  gpio_set_level(static_cast<gpio_num_t>(D13), DIRECTION_INVERTED ? HIGH : LOW);
  gpio_set_level(static_cast<gpio_num_t>(D11), HIGH);
  pinMode(D12, OUTPUT);
  pinMode(D13, OUTPUT);
  pinMode(D11, OUTPUT);
  usr1.begin(); usr2.begin(); usr3.begin();
  trial.reset(millis());
  motion.reset(millis());
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  displayReady = display.begin<Board_XIAO_1inch14_LCD<13, 12>,
                                Config_Seeed_1inch14_LCD_ST7789>();
  if (displayReady) {
    display.setRotation(0);
    dashboardFrame.setColorDepth(16);
    if (!dashboardFrame.createSprite(display, 135, 240)) {
      failTrial("UI BUFFER", millis());
      display.fillScreen(TFT_BLACK);
      display.setTextColor(TFT_RED);
      display.setCursor(8, 20);
      display.print("UI BUFFER FAULT");
      publishStatus();
      return;
    }
  } else {
    failTrial("LCD INIT", millis());
    publishStatus();
    return;
  }

  timerReady = stepOutput.begin();
  if (!timerReady) {
    failTrial("STEP TIMER INIT", millis());
  }
  publishStatus();
  // Arduino UI loop runs on core 1; motor work runs independently on core 0.
  if (xTaskCreatePinnedToCore(controlTask, "jog-control", 4096, nullptr, 3,
                              nullptr, 0) != pdPASS) {
    failTrial("TASK INIT", millis());
    publishStatus();
  }
  const Snapshot status = readStatus();
  drawStatus(status, millis(), true);
  reportStatus("BOOT", status);
}

void controlTask(void*) {
  uint32_t lastTotal = 0;
  while (true) {
    const uint32_t now = millis();
    if (controlLastTick != 0 && now - controlLastTick > controlMaxGap)
      controlMaxGap = now - controlLastTick;
    controlLastTick = now;
    usr1.poll(now); usr2.poll(now); usr3.poll(now);
    const auto output = stepOutput.status();
    // Consume every emitted edge using the still-applied direction before
    // tick()/applyOutputs() can prepare a new direction. The existing stopped
    // interval and 100 ms DIR settle keep old edges out of the next run.
    motion.observe(output.total, appliedDirection, now);
    if (output.total != lastTotal) {
      trial.recordPulses(output.total - lastTotal, now);
      lastTotal = output.total;
    }
    if (output.fault && trial.state() != State::Fault) {
      timerReady = false;
      failTrial("STEP TIMER", now);
    }
    trial.tick(Keys{usr1.stable, usr2.stable, usr3.stable},
               Keys{usr1.raw, usr2.raw, usr3.raw}, now);
    const uint32_t hz = trial.targetHz(now);
    if (hz == 0) stepOutput.command(0, 0);
    applyOutputs(stepOutput.status().high);
    if (hz != 0 && timerReady) {
      if (trial.reverse() == appliedDirection)
        stepOutput.command(hz, HOLD_MODE ? UINT32_MAX : TRIAL_PROFILE.maxPulses);
      else
        failTrial("DIR NOT READY", now);
    }
    motion.setMoving(trial.moving(), now);
    publishStatus();
    vTaskDelay(1);
  }
}

void loop() {
  static State lastReportedState = State::Fault;
  const Snapshot status = readStatus();
  drawStatus(status, millis());
  if (status.state != lastReportedState) {
    reportStatus("STATE", status);
    lastReportedState = status.state;
  }
  const uint32_t now = millis();
  if (now - lastReport >= 1000) {
    lastReport = now;
    reportStatus("HEARTBEAT", readStatus());
  }
  delay(1);
}
