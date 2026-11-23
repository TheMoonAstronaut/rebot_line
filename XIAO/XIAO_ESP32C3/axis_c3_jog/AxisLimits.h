#pragma once
#include <stdint.h>

// Input assertion is immediate; release must stay clear for 20 ms. An arrival
// requires an acknowledged, low-speed escape jog. Actual retreat and stable
// clearance of both switches end recovery, even while the key stays held.
class AxisLimits {
 public:
  static constexpr uint32_t RELEASE_MS = 20;
  static constexpr uint32_t ESCAPE_HZ = 200;
  static constexpr uint32_t ESCAPE_PULSES = 400;
  static constexpr uint32_t ESCAPE_MS = 3000;
  enum class Fault { None, Both, Time, Pulses };

  void sample(uint8_t rawActive, uint8_t timerHits, uint32_t now, bool armed = true) {
    for (unsigned i = 0; i < 2; ++i) {
      const uint8_t bit = 1u << i;
      if ((rawActive | timerHits) & bit) {
        active_ |= bit;
        clearing_ &= ~bit;
      } else if (active_ & bit) {
        if (!(clearing_ & bit)) { clearAt_[i] = now; clearing_ |= bit; }
        if (now - clearAt_[i] >= RELEASE_MS) active_ &= ~bit;
      }
    }
    if (armed || timerHits) {
      if (active_ == 3) fault_ = Fault::Both;
      else if (active_ && recovery_ != active_) {
        // A current single end overrides stale history from another end.
        pause(now);
        recovery_ = active_;
        escaping_ = retreated_ = false;
        runMs_ = 0;
      }
    }
  }

  void observe(uint32_t total, bool /*released*/) {
    if (escaping_ && total != escapeStartTotal_) retreated_ = true;
    if (active_ == 0 && retreated_) {
      recovery_ = 0;
      escaping_ = retreated_ = false;
      running_ = false;
      runMs_ = 0;
    }
  }

  bool blocked(bool reverse) const {
    return fault() || bool((active_ | recovery_) & (reverse ? 1 : 2));
  }

  uint32_t allowedHz(uint32_t requested, bool reverse, uint32_t now, uint32_t total) {
    accountTime(now);
    running_ = false;
    if (!requested || blocked(reverse)) return 0;
    if (!recovery_) return requested;
    if (!escaping_) {
      escaping_ = true;
      escapeStartTotal_ = total;
    }
    if (runMs_ >= ESCAPE_MS || total - escapeStartTotal_ >= ESCAPE_PULSES) {
      fault_ = runMs_ >= ESCAPE_MS ? Fault::Time : Fault::Pulses;
      return 0;
    }
    running_ = true;
    return requested > ESCAPE_HZ ? ESCAPE_HZ : requested;
  }

  uint32_t remaining(uint32_t total) const {
    if (!escaping_) return UINT32_MAX;
    const uint32_t used = total - escapeStartTotal_;
    return used >= ESCAPE_PULSES ? 0 : ESCAPE_PULSES - used;
  }
  uint8_t recovery() const { return recovery_; }
  uint8_t activeMask() const { return active_; }
  bool clear() const { return active_ == 0; }
  bool fault() const { return fault_ != Fault::None; }
  Fault faultCode() const { return fault_; }
  void pause(uint32_t now) { accountTime(now); running_ = false; }

 private:
  uint8_t active_ = 0, recovery_ = 0, clearing_ = 0;
  uint32_t clearAt_[2]{};
  void accountTime(uint32_t now) {
    if (haveTime_ && running_) runMs_ += now - timeAt_;
    timeAt_ = now;
    haveTime_ = true;
  }
  uint32_t escapeStartTotal_ = 0, runMs_ = 0, timeAt_ = 0;
  bool escaping_ = false, retreated_ = false, running_ = false, haveTime_ = false;
  Fault fault_ = Fault::None;
};
