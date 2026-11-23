#include <Arduino.h>
#include "driver/gpio.h"
#include "../../common/UsbJog.h"
#include "../../common/AxisControl.h"
#include "../../XIAO_1.14inch_IPS_Display_ESP32-S3/xiao_pulse_trial/JogTrial.h"
#include "../../XIAO_1.14inch_IPS_Display_ESP32-S3/xiao_pulse_trial/MotionTelemetry.h"
#include "../../XIAO_1.14inch_IPS_Display_ESP32-S3/xiao_pulse_trial/StepPulseTimer.h"
#include "AxisLimits.h"
#include "AxisJogProfile.h"
#include "AxisHoming.h"

using namespace pulse_trial;
using AxisFault = axis_control::Fault;
static_assert(D1 == 3 && D2 == 4 && D3 == 5 && D4 == 6 && D5 == 7 && D10 == 10,
              "Select XIAO ESP32C3");
constexpr uint8_t STEP_PIN = D2, DIR_PIN = D1, ENABLE_PIN = D3;
constexpr uint8_t LIMIT_NEG_PIN = D4, HOME_PIN = D5, LIMIT_POS_PIN = D10;
constexpr TrialConfig PROFILE = axis_c3::JOG_PROFILE;
constexpr bool INVERT_DIRECTION = true;
constexpr uint8_t ENABLE_LEVEL = LOW;
constexpr uint32_t HOME_HOLD_MS = 1500;

JogTrial trial{PROFILE};
MotionTelemetry motion;
StepPulseTimer timer{STEP_PIN, LIMIT_NEG_PIN, LIMIT_POS_PIN};
AxisLimits limits;
AxisHoming home;
usb_jog::LineReader reader;
AxisFault fault = AxisFault::None;
bool linked = false, timerReady = false, appliedReverse = false;
bool enabled = false, haveSequence = false;
uint8_t rawMask = 0, stableMask = 0, inputRaw = 7;
uint32_t lastKeysAt = 0, changedAt = 0, lastSequence = 0, lastReport = 0, lastTotal = 0;
uint32_t ackAt = 0, homeHoldAt = 0;
bool ackTiming = false, ackUsed = false, homeTiming = false, homePending = false;
int64_t originOffset = 0;
uint8_t seenEnds = 0, acknowledgedEnd = 0;
uint32_t originSeen = 0, zeroSequence = 0, originLowAt = 0, originHighAt = 0;
bool originPending = false, originLowTiming = false, originHighTiming = false, manualReference = false;
int64_t pendingOrigin = 0;

void ARDUINO_ISR_ATTR onOriginFalling() { timer.captureOriginFromISR(millis()); }

Keys keys(uint8_t mask) { return {bool(mask & 1), bool(mask & 2), bool(mask & 4)}; }
bool released() { return rawMask == 0 && stableMask == 0; }
uint8_t readInputs() {
  return (digitalRead(LIMIT_NEG_PIN) ? 1 : 0) | (digitalRead(HOME_PIN) ? 2 : 0) |
         (digitalRead(LIMIT_POS_PIN) ? 4 : 0);
}
uint8_t endMask(uint8_t raw) { return (!(raw & 1) ? 1 : 0) | (!(raw & 4) ? 2 : 0); }

void stopOutputs(uint32_t now) {
  timer.command(0, 0);
  limits.pause(now);
  digitalWrite(ENABLE_PIN, !ENABLE_LEVEL);
  enabled = false;
}
void lock(uint32_t now, bool linkLost = false) {
  stopOutputs(now);
  home.cancel();
  if (linkLost) { manualReference = manualReference || home.homed(); home.invalidate(); }
  homePending = homeTiming = false;
  ackTiming = ackUsed = false;
  if (trial.state() != State::Fault) trial.reset(now);
  linked = false;
  rawMask = stableMask = 0;
}
void failAxis(AxisFault why, uint32_t now) {
  if (fault == AxisFault::None || why == AxisFault::Timer ||
      (why == AxisFault::BothLimits && fault != AxisFault::Timer) ||
      ((why == AxisFault::NegativeEnd || why == AxisFault::PositiveEnd) &&
       (fault == AxisFault::NegativeEnd || fault == AxisFault::PositiveEnd))) fault = why;
  acknowledgedEnd = 0;
  // Pulses remain counted after a stop: preserve the displayed coordinate.
  manualReference = manualReference || home.homed();
  home.cancel();
  home.invalidate();
  homePending = homeTiming = false;
  stopOutputs(now);
  trial.fail(now);
}
bool resetFault(uint32_t now, bool fromButton) {
  const auto status = timer.status();
  const bool safeKeys = fromButton ? rawMask == 4 && stableMask == 4 : released();
  const uint8_t ends = endMask(readInputs()) | limits.activeMask();
  const uint8_t faultEnd = fault == AxisFault::NegativeEnd ? 1 : fault == AxisFault::PositiveEnd ? 2 : 0;
  const bool heldEnd = faultEnd && ends == faultEnd;
  // HOME may legitimately be active at a stopped reference position.
  const bool allowed = timerReady && !enabled && !status.high && !status.fault &&
      !home.active() && safeKeys && (ends == 0 || heldEnd);
  if (!allowed) return false;
  timer.acknowledgeLimits();
  timer.acknowledgeInput();
  limits = AxisLimits{};
  acknowledgedEnd = heldEnd ? ends : 0;
  if (faultEnd) limits.sample(faultEnd, 0, now, true);
  fault = AxisFault::None;
  if (!home.homed()) home.reset();
  // The same deliberate USR3 hold acknowledges and arms. No motion until release.
  if (fromButton) trial.armOnRelease(now);
  else trial.reset(now - JogTrial::BOOT_MS);
  homePending = homeTiming = false;
  return true;
}
bool startHome(uint32_t now) {
  if (!axis_control::HOMING_ENABLED) return false;
  const auto status = timer.status();
  const uint8_t raw = readInputs();
  const uint8_t ends = endMask(raw) | limits.activeMask();
  if (!linked || !timerReady || fault != AxisFault::None || home.active() ||
      trial.state() != State::Disarmed || !released() || status.high || status.fault ||
      limits.recovery() || ends == 3) return false;
  stopOutputs(now);
  timer.acknowledgeInput();
  timer.acknowledgeLimits();
  trial.reset(now);
  manualReference = false;
  originPending = false;
  return home.start(now, status.total, !(raw & 2), ends);
}

void updateOrigin(const StepPulseTimer::Status& output, uint32_t now) {
  if (fault != AxisFault::None || home.active()) originPending = false;
  if (output.originSequence != originSeen) {
    originSeen = output.originSequence;
    pendingOrigin = output.originPosition;
    originPending = !home.active() && fault == AxisFault::None;
    originLowAt = output.originAt;
    originLowTiming = true;
  }
  if (!(inputRaw & 2)) {
    originHighTiming = false;
    if (originPending) {
      if (!originLowTiming) { originLowAt = now; originLowTiming = true; }
      if (now - originLowAt >= 2) {
        originOffset = pendingOrigin;
        manualReference = true;
        ++zeroSequence;
        originPending = false;
      }
    }
  } else {
    originPending = originLowTiming = false;
    if (!originHighTiming) { originHighAt = now; originHighTiming = true; }
    if (now - originHighAt >= 20) timer.armOrigin(HOME_PIN);
  }
}

void receive(std::string_view line) {
  const uint32_t now = millis();
  if (line == R"({"cmd":"HELLO"})") {
    usb_jog::hello("AXIS_C3", "axis_c3_jog", axis_control::PROTOCOL);
    return;
  }
  if (line == R"({"cmd":"STOP"})") { lock(now); haveSequence = false; return; }
  if (line == R"({"cmd":"RESET_LIMITS"})") {
    usb_jog::send("{\"type\":\"RESET_LIMITS\",\"ok\":%u}\n", unsigned(resetFault(now, false)));
    return;
  }
  if (line == R"({"cmd":"HOME"})") {
    usb_jog::send("{\"type\":\"HOME\",\"ok\":%u}\n", unsigned(startHome(now)));
    return;
  }
  unsigned long seq = 0;
  unsigned mask = 0;
  int used = 0;
  if (sscanf(line.data(), R"({"cmd":"KEYS","seq":%lu,"mask":%u}%n)",
             &seq, &mask, &used) != 2 || static_cast<size_t>(used) != line.size() || mask > 7) return;
  if (haveSequence && static_cast<int32_t>(uint32_t(seq) - lastSequence) <= 0) return;
  haveSequence = true;
  lastSequence = seq;
  if (!linked && mask != 0) return;
  if (!linked) {
    if (trial.state() != State::Fault) trial.reset(now);
    linked = true;
  }
  lastKeysAt = now;
  if (mask != rawMask) {
    if (home.active() && mask) {
      stopOutputs(now);
      home.cancel();
      timer.acknowledgeInput();
      trial.reset(now);
    }
    rawMask = mask;
    changedAt = now;
    if (!home.active()) trial.tick(keys(stableMask), keys(rawMask), now);
    if (!trial.moving() && !home.active()) { timer.command(0, 0); limits.pause(now); }
  }
}

void buttonActions(uint32_t now) {
  if (!linked) return;
  if (trial.state() == State::Fault) {
    homePending = homeTiming = false;
    if (rawMask == 4 && stableMask == 4) {
      if (!ackTiming) { ackAt = now; ackTiming = true; }
      if (!ackUsed && now - ackAt >= JogTrial::ARM_MS) {
        ackUsed = true;
        resetFault(now, true);
      }
    } else if (rawMask != 4) ackTiming = ackUsed = false;
    return;
  }
  ackTiming = ackUsed = false;
  if (!axis_control::HOMING_ENABLED) {
    homeTiming = homePending = false;
    return;
  }
  if (home.active() || trial.state() != State::Disarmed) {
    homeTiming = homePending = false;
    return;
  }
  if (rawMask == 3 && stableMask == 3) {
    if (!homeTiming) { homeHoldAt = now; homeTiming = true; }
    if (now - homeHoldAt >= HOME_HOLD_MS) { homePending = true; home.invalidate(); }
  } else if (homePending && released()) {
    homePending = homeTiming = false;
    if (!startHome(now)) failAxis(AxisFault::HomeNotReady, now);
  } else if (rawMask != 0 || !homePending) homePending = homeTiming = false;
}

void setup() {
  gpio_set_level(static_cast<gpio_num_t>(STEP_PIN), LOW);
  gpio_set_level(static_cast<gpio_num_t>(DIR_PIN), INVERT_DIRECTION);
  gpio_set_level(static_cast<gpio_num_t>(ENABLE_PIN), !ENABLE_LEVEL);
  pinMode(ENABLE_PIN, OUTPUT); pinMode(STEP_PIN, OUTPUT); pinMode(DIR_PIN, OUTPUT);
  pinMode(LIMIT_NEG_PIN, INPUT_PULLUP); pinMode(HOME_PIN, INPUT_PULLUP); pinMode(LIMIT_POS_PIN, INPUT_PULLUP);
  const uint32_t now = millis();
  trial.reset(now); motion.reset(now);
  timerReady = timer.begin();
  if (!timerReady) failAxis(AxisFault::Timer, now);
  attachInterrupt(digitalPinToInterrupt(HOME_PIN), onOriginFalling, FALLING);
  Serial.begin(115200); Serial.setTxTimeoutMs(0);
}

void loop() {
  const uint32_t now = millis();
  if (linked && now - lastKeysAt >= usb_jog::KEYS_TIMEOUT_MS) lock(now, true);
  reader.poll(receive);
  const uint32_t tickAt = millis();
  const auto output = timer.status();
  inputRaw = readInputs();
  const uint8_t ends = endMask(inputRaw);
  limits.sample(ends, output.limitHits, tickAt, linked && trial.enabled() && !home.active());
  const uint8_t newEnd = (ends & ~seenEnds) | output.limitHits;
  seenEnds = (seenEnds | ends | output.limitHits) & limits.activeMask();
  acknowledgedEnd &= limits.activeMask();
  motion.observe(output.total, appliedReverse, tickAt);
  if (output.total != lastTotal) { trial.recordPulses(output.total - lastTotal, tickAt); lastTotal = output.total; }
  if (output.fault) { timerReady = false; failAxis(AxisFault::Timer, tickAt); }
  if (!home.active() && newEnd) {
    failAxis(ends == 3 || newEnd == 3 ? AxisFault::BothLimits :
             (newEnd & 1) ? AxisFault::NegativeEnd : AxisFault::PositiveEnd, tickAt);
  }
  if (output.limitHits && !home.active()) trial.inhibit(tickAt);
  if (output.limitHits) timer.acknowledgeLimits();
  const uint8_t beforeRecovery = limits.recovery();
  limits.observe(output.total, released());
  if (beforeRecovery && !limits.recovery()) {
    // End the bounded timer lease too; command() cannot grow a running budget.
    timer.command(0, 0);
    trial.restartRamp(tickAt);
  }
  if (limits.fault()) {
    const auto why = limits.faultCode();
    failAxis(why == AxisLimits::Fault::Both ? AxisFault::BothLimits :
             why == AxisLimits::Fault::Time ? AxisFault::EscapeTime : AxisFault::EscapePulses, tickAt);
  }
  if (linked && tickAt - changedAt >= 20) stableMask = rawMask;
  updateOrigin(output, tickAt);
  buttonActions(tickAt);

  uint32_t hz = 0, pulseBudget = UINT32_MAX;
  bool desiredReverse = trial.reverse(), wantEnable = false;
  int inputGuard = -1;
  bool guardLevel = false;
  if (home.active() && fault == AxisFault::None) {
    limits.pause(tickAt);
    const bool completed = home.tick(tickAt, output.total, !(inputRaw & 2),
                                     output.inputHit, output.high, ends | output.limitHits | limits.activeMask());
    if (output.inputHit) timer.acknowledgeInput();
    if (completed) {
      timer.command(0, 0);
      originOffset = output.position;
      ++zeroSequence;
      limits = AxisLimits{};
      trial.reset(tickAt);
    }
    if (home.fault() != AxisFault::None) failAxis(home.fault(), tickAt);
    hz = home.targetHz(tickAt);
    desiredReverse = home.reverse();
    wantEnable = home.active() && fault == AxisFault::None;
    pulseBudget = home.remaining(output.total);
    if (home.guardHome()) { inputGuard = HOME_PIN; guardLevel = !home.wantedActive(); }
  } else {
    if (output.inputHit) timer.acknowledgeInput();
    if (linked) trial.tick(keys(stableMask), keys(rawMask), tickAt);
    if (limits.blocked(trial.reverse())) trial.inhibit(tickAt);
    const uint32_t requested = linked && timerReady ? trial.targetHz(tickAt) : 0;
    hz = limits.allowedHz(requested, trial.reverse(), tickAt, output.total);
    if (requested && !hz) trial.inhibit(tickAt);
    if (limits.fault()) {
      failAxis(limits.faultCode() == AxisLimits::Fault::Time ? AxisFault::EscapeTime : AxisFault::EscapePulses, tickAt);
      hz = 0;
    }
    desiredReverse = trial.reverse();
    wantEnable = linked && timerReady && fault == AxisFault::None && trial.enabled();
    const uint32_t jog = axis_c3::remainingJogPulses(trial.count());
    const uint32_t escape = limits.remaining(output.total);
    pulseBudget = jog < escape ? jog : escape;
  }
  if (wantEnable && limits.activeMask() == 3) failAxis(AxisFault::BothLimits, tickAt);
  if (!linked || !timerReady || fault != AxisFault::None) { hz = 0; wantEnable = false; }
  if (!hz) timer.command(0, 0);
  if (!timer.status().high && desiredReverse != appliedReverse) {
    appliedReverse = desiredReverse;
    digitalWrite(DIR_PIN, appliedReverse != INVERT_DIRECTION);
  }
  if (wantEnable != enabled) {
    enabled = wantEnable;
    digitalWrite(ENABLE_PIN, enabled ? ENABLE_LEVEL : !ENABLE_LEVEL);
  }
  if (hz && desiredReverse == appliedReverse) {
    const uint8_t allowedEnd = home.active() ? limits.activeMask() : acknowledgedEnd & limits.activeMask();
    timer.command(hz, pulseBudget, appliedReverse, inputGuard, guardLevel, allowedEnd);
  }
  motion.setMoving(hz != 0, tickAt);

  if (tickAt - lastReport >= usb_jog::REPORT_INTERVAL_MS) {
    lastReport = tickAt;
    const auto state = home.active() ? (appliedReverse ? State::Reverse : State::Forward) : trial.state();
    const auto homeState = homePending ? axis_control::Home::Release : home.state();
    const bool zeroed = home.homed() || manualReference;
    const int64_t position = output.position - (zeroed ? originOffset : 0);
    usb_jog::send("{\"type\":\"AXIS\",\"state\":%u,\"rev\":%u,\"linked\":%u,\"hz\":%lu,"
                  "\"target\":%lu,\"fwd\":%llu,\"back\":%llu,\"total\":%lu,\"raw\":%u,"
                  "\"recovery\":%u,\"homed\":%u,\"home\":%u,\"fault\":%u,\"zeroed\":%u,\"zero\":%lu,\"pos\":%lld}\n",
                  unsigned(state), unsigned(appliedReverse), unsigned(linked),
                  static_cast<unsigned long>(motion.pulseHz()), static_cast<unsigned long>(hz),
                  static_cast<unsigned long long>(motion.forwardPulses()),
                  static_cast<unsigned long long>(motion.reversePulses()), static_cast<unsigned long>(output.total),
                  unsigned(inputRaw), unsigned(limits.recovery()), unsigned(home.homed()), unsigned(homeState),
                  unsigned(fault), unsigned(zeroed), static_cast<unsigned long>(zeroSequence), static_cast<long long>(position));
  }
  delay(1);
}
