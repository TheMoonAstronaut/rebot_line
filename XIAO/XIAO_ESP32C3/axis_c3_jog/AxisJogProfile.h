#pragma once
#include "../../XIAO_1.14inch_IPS_Display_ESP32-S3/xiao_pulse_trial/JogTrial.h"

namespace axis_c3 {
constexpr pulse_trial::TrialConfig JOG_PROFILE{20, 6827, 2000, 0, 0,
                                             pulse_trial::RunMode::HoldToRun};
inline uint32_t remainingJogPulses(uint32_t count) {
  if (JOG_PROFILE.mode == pulse_trial::RunMode::HoldToRun) return UINT32_MAX;
  return count >= JOG_PROFILE.maxPulses ? 0 : JOG_PROFILE.maxPulses - count;
}
}  // namespace axis_c3
