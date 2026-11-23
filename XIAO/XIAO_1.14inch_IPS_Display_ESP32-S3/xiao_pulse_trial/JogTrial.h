#pragma once
#include <stdint.h>

// Manual jog policy with explicit trial limits; no physical end-limit input.
namespace pulse_trial {
struct Keys {
  bool forward = false;
  bool reverse = false;
  bool stop = false;
  bool released() const { return !forward && !reverse && !stop; }
};

enum class State { Boot, Disarmed, ArmRelease, Ready, Settling,
                   Forward, Reverse, Release, Fault };
enum class RunMode { Limited, HoldToRun };

struct TrialConfig {
  uint32_t startHz = 20;
  uint32_t maxHz = 20;
  uint32_t rampMs = 0;
  uint32_t maxRunMs = 1000;
  uint32_t maxPulses = 20;
  RunMode mode = RunMode::Limited;
};

class JogTrial {
 public:
  static constexpr uint32_t BOOT_MS = 2000;
  static constexpr uint32_t ARM_MS = 1000;
  static constexpr uint32_t SETTLE_MS = 100;
  static constexpr uint32_t REVERSE_GUARD_MS = 500;

  explicit JogTrial(TrialConfig config = {}) : config_(config) {
    if (config.startHz == 0 || config.maxHz < config.startHz ||
        config.maxHz > 7000 ||
        (config.mode != RunMode::Limited && config.mode != RunMode::HoldToRun) ||
        (config.mode == RunMode::Limited &&
         (config.maxRunMs == 0 || config.maxPulses == 0 || config.rampMs > config.maxRunMs)) ||
        (config.mode == RunMode::HoldToRun && (config.maxRunMs != 0 || config.maxPulses != 0)))
      state_ = State::Fault;
  }

  void reset(uint32_t now) {
    *this = JogTrial(config_);
    bootAt_ = now;
  }

  // Fault recovery consumes one deliberate arm hold; motion still waits for release.
  void armOnRelease(uint32_t now) {
    reset(now);
    state_ = State::ArmRelease;
  }

  void restartRamp(uint32_t now) {
    if (moving()) { runAt_ = now; rampFinished_ = false; }
  }

  void tick(Keys stable, Keys raw, uint32_t now) {
    const bool released = stable.released() && raw.released();
    if (state_ == State::Fault) return;
    if (state_ == State::Boot) {
      if (now - bootAt_ >= BOOT_MS && released) state_ = State::Disarmed;
      return;
    }
    if (state_ == State::Disarmed) {
      if (stable.stop && raw.stop && !stable.forward && !stable.reverse &&
          !raw.forward && !raw.reverse) {
        if (!armTiming_) { armTiming_ = true; armAt_ = now; }
        if (now - armAt_ >= ARM_MS) {
          state_ = State::ArmRelease;
          armTiming_ = false;
        }
      } else {
        armTiming_ = false;
      }
      return;
    }
    if (state_ == State::ArmRelease) {
      if (released) state_ = State::Ready;
      // A direction key during arming cancels it; it never starts movement.
      else if (raw.forward || raw.reverse) state_ = State::Boot;
      return;
    }
    if (raw.stop) {
      rememberStop(now);
      state_ = State::Boot;  // Release every key before allowing re-arming.
      return;
    }
    if (state_ == State::Release) {
      if (released) state_ = State::Ready;
      return;
    }
    if (raw.forward && raw.reverse) {
      stopForRelease(now);
      return;
    }
    if (state_ == State::Ready) {
      if (stable.forward && raw.forward && !stable.reverse && !raw.reverse) {
        start(false, now);
      } else if (stable.reverse && raw.reverse && !stable.forward && !raw.forward) {
        start(true, now);
      }
      return;
    }
    // Raw release/stop takes precedence over debounce and pulse scheduling.
    if ((reverse_ && (!raw.reverse || raw.forward)) ||
        (!reverse_ && (!raw.forward || raw.reverse))) {
      stopForRelease(now);
      return;
    }
    if (state_ == State::Settling) {
      if (now - settleAt_ < settleFor_) return;
      state_ = reverse_ ? State::Reverse : State::Forward;
      runAt_ = now;
    }
    if (moving()) {
      if (now - runAt_ >= config_.rampMs) rampFinished_ = true;
      if (config_.mode == RunMode::Limited &&
          (now - runAt_ >= config_.maxRunMs || count_ >= config_.maxPulses)) {
        stopForRelease(now);
      }
    }
  }

  // The output timer counts real STEP rising edges independently of this loop.
  void recordPulses(uint32_t count, uint32_t now) {
    count_ += count;
    total_ += count;
    if (config_.mode == RunMode::Limited && count_ >= config_.maxPulses)
      stopForRelease(now);
  }

  void fail(uint32_t now) { rememberStop(now); state_ = State::Fault; }
  void inhibit(uint32_t now) {
    if (moving() || state_ == State::Settling) stopForRelease(now);
  }
  State state() const { return state_; }
  bool reverse() const { return reverse_; }
  bool moving() const { return state_ == State::Forward || state_ == State::Reverse; }
  bool enabled() const {
    return state_ == State::Ready || state_ == State::Settling || moving() ||
           state_ == State::Release;
  }
  uint32_t count() const { return count_; }
  uint32_t total() const { return total_; }
  uint32_t targetHz(uint32_t now) const {
    if (!moving()) return 0;
    const uint32_t elapsed = now - runAt_;
    if (rampFinished_ || config_.rampMs == 0 || elapsed >= config_.rampMs)
      return config_.maxHz;
    return config_.startHz + static_cast<uint32_t>(
        uint64_t(config_.maxHz - config_.startHz) * elapsed / config_.rampMs);
  }

 private:
  void start(bool reverse, uint32_t now) {
    reverse_ = reverse;
    count_ = 0;
    rampFinished_ = false;
    settleAt_ = now;
    settleFor_ = SETTLE_MS;
    if (hadRun_ && reverse != previousReverse_) {
      const uint32_t elapsed = now - stoppedAt_;
      if (elapsed < REVERSE_GUARD_MS && REVERSE_GUARD_MS - elapsed > settleFor_)
        settleFor_ = REVERSE_GUARD_MS - elapsed;
    }
    state_ = State::Settling;
  }
  void rememberStop(uint32_t now) {
    if (moving() || state_ == State::Settling) {
      previousReverse_ = reverse_;
      stoppedAt_ = now;
      hadRun_ = true;
    }
  }
  void stopForRelease(uint32_t now) {
    rememberStop(now);
    state_ = State::Release;
  }

  State state_ = State::Boot;
  TrialConfig config_;
  bool reverse_ = false, previousReverse_ = false, hadRun_ = false;
  bool armTiming_ = false;
  bool rampFinished_ = false;
  uint32_t bootAt_ = 0, armAt_ = 0, settleAt_ = 0, settleFor_ = 0;
  uint32_t runAt_ = 0, stoppedAt_ = 0;
  uint32_t count_ = 0, total_ = 0;
};
}  // namespace pulse_trial
