#pragma once

#include <stdint.h>

namespace pulse_trial {

// Estimates from emitted STEP edges, not encoder feedback. Owned only by the
// control task, sampled before that task changes the applied DIR output.
class MotionTelemetry {
 public:
  static constexpr double MM_PER_PULSE = 37.5 / 3200.0;

  void reset(uint32_t now) {
    *this = MotionTelemetry{};
    rateAt_ = now;
  }

  void observe(uint32_t total, bool appliedReverse, uint32_t now) {
    const uint32_t delta = total - lastTotal_;  // Timer count may wrap.
    lastTotal_ = total;
    if (appliedReverse) reverse_ += delta;
    else forward_ += delta;
    windowPulses_ += delta;
    const uint32_t elapsed = now - rateAt_;
    if (elapsed >= 100) {
      const uint64_t rate = windowPulses_ * 1000 / elapsed;
      pulseHz_ = rate > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(rate);
      windowPulses_ = 0;
      rateAt_ = now;
    }
  }

  void setMoving(bool moving, uint32_t now) {
    if (moving != moving_) {
      moving_ = moving;
      pulseHz_ = 0;
      windowPulses_ = 0;
      rateAt_ = now;
    }
    // Expand the viewport without silently changing the scale on the return.
    const double position = positionMm();
    const double magnitude = position < 0 ? -position : position;
    while (magnitude > viewHalfMm_) viewHalfMm_ *= 2;
  }

  int64_t positionPulses() const {
    return static_cast<int64_t>(forward_) - static_cast<int64_t>(reverse_);
  }
  double positionMm() const { return positionPulses() * MM_PER_PULSE; }
  uint64_t forwardPulses() const { return forward_; }
  uint64_t reversePulses() const { return reverse_; }
  double forwardMm() const { return forward_ * MM_PER_PULSE; }
  double reverseMm() const { return reverse_ * MM_PER_PULSE; }
  uint32_t pulseHz() const { return moving_ ? pulseHz_ : 0; }
  double speedMmS() const { return pulseHz() * MM_PER_PULSE; }
  double viewHalfMm() const { return viewHalfMm_; }

 private:
  uint64_t forward_ = 0, reverse_ = 0, windowPulses_ = 0;
  uint32_t lastTotal_ = 0, rateAt_ = 0, pulseHz_ = 0;
  bool moving_ = false;
  double viewHalfMm_ = 100;
};

}  // namespace pulse_trial
