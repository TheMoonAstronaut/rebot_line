#pragma once
#include "../../common/AxisControl.h"
#include "../xiao_pulse_trial/JogTrial.h"

namespace panel_control {
inline bool homing(axis_control::Home h) {
  using H = axis_control::Home;
  return h == H::Clear || h == H::Seek || h == H::Backoff || h == H::Latch || h == H::EndClear;
}
inline const char* hint(pulse_trial::State state, bool online, axis_control::Fault fault,
                        axis_control::Home home, uint8_t recovery, uint8_t raw) {
  using S = pulse_trial::State;
  if (!online) return "USB LINK LOST";
  if (state == S::Fault) {
    if (fault == axis_control::Fault::Timer) return "RESTART REQUIRED";
    const uint8_t ends = (!(raw & 1) ? 1 : 0) | (!(raw & 4) ? 2 : 0);
    const uint8_t faultEnd = fault == axis_control::Fault::NegativeEnd ? 1 :
                             fault == axis_control::Fault::PositiveEnd ? 2 : 0;
    if (ends && ends != faultEnd) return "CLEAR END LIMITS";
    return "HOLD U3 TO UNLOCK";
  }
  if (home == axis_control::Home::Release) return "RELEASE ALL KEYS";
  if (homing(home)) return "U3 STOP / CANCEL";
  if (state == S::Boot || state == S::ArmRelease || state == S::Release) return "RELEASE ALL KEYS";
  if (state == S::Disarmed) return axis_control::HOMING_ENABLED ? "U3 ARM U1+U2 HOME" : "HOLD USR3 TO ARM";
  if (recovery == 1) return "ESCAPE: U1 FWD";
  if (recovery == 2) return "ESCAPE: U2 REV";
  return state == S::Ready ? "U1 FWD / U2 REV" : "USR3 STOP / LOCK";
}
}  // namespace panel_control
