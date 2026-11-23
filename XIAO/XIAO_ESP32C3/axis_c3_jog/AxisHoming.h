#pragma once
#include "../../common/AxisControl.h"

struct HomingConfig {
  bool reverse = true;  // Confirmed: seek toward USR2, between the end switches.
  uint32_t seekHz = 200, latchHz = 50;
  // User estimate: -800 .. +800 mm. 136534 pulses ~= 1600 mm.
  uint32_t seekPulses = 136534, clearPulses = 800, latchPulses = 1600;
  uint32_t seekMs = 720000, clearMs = 10000, latchMs = 40000;
};

class AxisHoming {
 public:
  static constexpr uint32_t SETTLE_MS = 500, STABLE_MS = 20, EDGE_WAIT_MS = 250;
  explicit AxisHoming(HomingConfig config = {}) : config_(config) {}
  bool start(uint32_t now, uint32_t total, bool atHome, uint8_t ends = 0) {
    if (active() || ends == 3) return false;
    homed_ = false;
    seekReverse_ = config_.reverse;
    retried_ = false;
    fault_ = axis_control::Fault::None;
    if (ends) {
      expectedEnd_ = ends; retried_ = true;
      enter(axis_control::Home::EndClear, now, total);
    } else enter(atHome ? axis_control::Home::Clear : axis_control::Home::Seek, now, total);
    return true;
  }
  void cancel() {
    if (active()) { phase_ = axis_control::Home::Cancelled; homed_ = false; }
  }
  void invalidate() { homed_ = false; if (phase_ == axis_control::Home::Homed) phase_ = axis_control::Home::Unhomed; }
  void reset() { *this = AxisHoming(config_); }
  // Returns true only on the single successful slow-latch completion.
  bool tick(uint32_t now, uint32_t total, bool atHome, bool edgeHit, bool pulseHigh, uint8_t ends) {
    if (!active()) return false;
    if (phase_ == axis_control::Home::EndClear) {
      if (ends & ~expectedEnd_) { fail(axis_control::Fault::HomeEnd); return false; }
      if (!ends) {
        if (!matching_) { matching_ = true; matchAt_ = now; }
        if (!pulseHigh && now - matchAt_ >= STABLE_MS && total != phaseTotal_) {
          seekReverse_ = expectedEnd_ == 2;
          enter(axis_control::Home::Seek, now, total);
          return false;
        }
      } else matching_ = false;
      if (now - phaseAt_ >= 3000) fail(axis_control::Fault::HomeTime);
      else if (total - phaseTotal_ >= 400) fail(axis_control::Fault::HomePulses);
      return false;
    }
    if (ends) {
      if (phase_ == axis_control::Home::Seek && !retried_ && ends == (seekReverse_ ? 1 : 2)) {
        retried_ = true;
        expectedEnd_ = ends;
        enter(axis_control::Home::EndClear, now, total);
      } else fail(axis_control::Fault::HomeEnd);
      return false;
    }
    const bool match = atHome == wantedActive();
    if (edgeHit || match) {
      if (!edgeSeen_) { edgeSeen_ = true; edgeAt_ = now; }
      if (match) {
        if (!matching_) { matching_ = true; matchAt_ = now; }
        if (!pulseHigh && now - matchAt_ >= STABLE_MS) {
          if (total == phaseTotal_) { fail(axis_control::Fault::HomeUnstable); return false; }
          using H = axis_control::Home;
          if (phase_ == H::Latch) { phase_ = H::Homed; homed_ = true; return true; }
          enter(phase_ == H::Clear ? H::Seek : phase_ == H::Seek ? H::Backoff : H::Latch, now, total);
          return false;
        }
      } else matching_ = false;
    }
    if (edgeSeen_ && now - edgeAt_ >= EDGE_WAIT_MS) fail(axis_control::Fault::HomeUnstable);
    else if (now - phaseAt_ >= timeout()) fail(axis_control::Fault::HomeTime);
    else if (!edgeSeen_ && total - phaseTotal_ >= budget()) fail(axis_control::Fault::HomePulses);
    return false;
  }
  bool active() const {
    using H = axis_control::Home;
    return phase_ == H::Clear || phase_ == H::Seek || phase_ == H::Backoff || phase_ == H::Latch || phase_ == H::EndClear;
  }
  bool reverse() const {
    using H = axis_control::Home;
    if (phase_ == H::Seek) return seekReverse_;
    if (phase_ == H::EndClear) return expectedEnd_ == 2;
    return phase_ == H::Clear || phase_ == H::Backoff || phase_ == H::EndClear ? !config_.reverse : config_.reverse;
  }
  bool wantedActive() const { return phase_ == axis_control::Home::Seek || phase_ == axis_control::Home::Latch; }
  bool guardHome() const { return active() && phase_ != axis_control::Home::EndClear; }
  uint32_t targetHz(uint32_t now) const {
    if (!active() || edgeSeen_ || now - phaseAt_ < SETTLE_MS) return 0;
    return phase_ == axis_control::Home::Latch ? config_.latchHz : config_.seekHz;
  }
  uint32_t remaining(uint32_t total) const {
    const uint32_t used = total - phaseTotal_;
    return used >= budget() ? 0 : budget() - used;
  }
  bool homed() const { return homed_; }
  axis_control::Home state() const { return phase_; }
  axis_control::Fault fault() const { return fault_; }
 private:
  void enter(axis_control::Home phase, uint32_t now, uint32_t total) {
    phase_ = phase; phaseAt_ = now; phaseTotal_ = total;
    edgeSeen_ = matching_ = false;
  }
  void fail(axis_control::Fault f) { fault_ = f; homed_ = false; phase_ = axis_control::Home::Failed; }
  uint32_t budget() const {
    using H = axis_control::Home;
    return phase_ == H::EndClear ? 400 : phase_ == H::Seek ? config_.seekPulses : phase_ == H::Latch ? config_.latchPulses : config_.clearPulses;
  }
  uint32_t timeout() const {
    using H = axis_control::Home;
    return phase_ == H::Seek ? config_.seekMs : phase_ == H::Latch ? config_.latchMs : config_.clearMs;
  }
  HomingConfig config_;
  axis_control::Home phase_ = axis_control::Home::Unhomed;
  axis_control::Fault fault_ = axis_control::Fault::None;
  uint32_t phaseAt_ = 0, phaseTotal_ = 0, edgeAt_ = 0, matchAt_ = 0;
  bool edgeSeen_ = false, matching_ = false, homed_ = false, seekReverse_ = true, retried_ = false;
  uint8_t expectedEnd_ = 0;
};
