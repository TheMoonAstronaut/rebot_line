#pragma once
#include <stdint.h>

namespace axis_control {
constexpr unsigned PROTOCOL = 4;
// Temporarily disabled at the user's request. Only test builds opt in.
#ifndef REBOT_ENABLE_HOMING
#define REBOT_ENABLE_HOMING 0
#endif
constexpr bool HOMING_ENABLED = REBOT_ENABLE_HOMING != 0;
enum class Fault : uint8_t {
  None, Timer, BothLimits, EscapeTime, EscapePulses,
  HomeTime, HomePulses, HomeUnstable, HomeEnd, HomeNotReady, NegativeEnd, PositiveEnd
};
enum class Home : uint8_t { Unhomed, Clear, Seek, Backoff, Latch, Homed, Cancelled, Failed, Release, EndClear };
inline const char* faultText(Fault f) {
  switch (f) {
    case Fault::None: return "NONE";
    case Fault::Timer: return "TIMER FAULT";
    case Fault::BothLimits: return "BOTH END LIMITS";
    case Fault::EscapeTime: return "ESCAPE TIME LIMIT";
    case Fault::EscapePulses: return "ESCAPE PULSE LIMIT";
    case Fault::HomeTime: return "HOME TIME LIMIT";
    case Fault::HomePulses: return "HOME TRAVEL LIMIT";
    case Fault::HomeUnstable: return "HOME INPUT UNSTABLE";
    case Fault::HomeEnd: return "END DURING HOME";
    case Fault::HomeNotReady: return "HOME NOT READY";
    case Fault::NegativeEnd: return "NEG END STOP";
    case Fault::PositiveEnd: return "POS END STOP";
  }
  return "UNKNOWN FAULT";
}
inline const char* homeText(Home h) {
  switch (h) {
    case Home::Unhomed: return "NOT HOMED";
    case Home::Clear: return "CLEAR HOME";
    case Home::Seek: return "SEEK HOME";
    case Home::Backoff: return "BACKOFF HOME";
    case Home::Latch: return "LATCH HOME";
    case Home::Homed: return "HOMED ZERO";
    case Home::Cancelled: return "HOME CANCELLED";
    case Home::Failed: return "HOME FAILED";
    case Home::Release: return "RELEASE FOR HOME";
    case Home::EndClear: return "LEAVE END LIMIT";
  }
  return "HOME UNKNOWN";
}
}  // namespace axis_control
