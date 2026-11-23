#pragma once

#include <stdio.h>
#include <string.h>
#include "JogTrial.h"

namespace pulse_trial {

struct DashboardData {
  State state = State::Boot;
  bool reverse = false;
  double positionMm = 0, forwardMm = 0, reverseMm = 0;
  double speedMmS = 0, viewHalfMm = 100;
  const char* fault = "NONE";
};

// RGB565 palette. One renderer serves both the on-device sprite and host
// previews, so dimensions, state labels and formatting share the same code.
namespace dashboard {
constexpr uint16_t BACKGROUND = 0x0863, PANEL = 0x10E5, TRACK = 0x2188;
constexpr uint16_t INK = 0xEF9F, MUTED = 0x8D34, CYAN = 0x3F39;
constexpr uint16_t AMBER = 0xFDCB, RED = 0xFAEA;

template <class Canvas>
void text(Canvas& c, int x, int y, const char* value, uint16_t color,
          int size = 1) {
  c.setTextSize(size);
  c.setTextColor(color);
  c.setCursor(x, y);
  c.print(value);
}

inline const char* label(State state, bool reverse) {
  switch (state) {
    case State::Boot: return "RELEASE KEYS";
    case State::Disarmed: return "LOCKED";
    case State::ArmRelease: return "RELEASE USR3";
    case State::Ready: return "READY / IDLE";
    case State::Settling: return reverse ? "WAIT REVERSE" : "WAIT FORWARD";
    case State::Forward: return "FORWARD  +";
    case State::Reverse: return "REVERSE  -";
    case State::Release: return "RELEASE KEYS";
    case State::Fault: return "FAULT / LOCK";
  }
  return "UNKNOWN";
}

inline void distance(char* dest, size_t capacity, double mm, bool signedValue) {
  const double magnitude = mm < 0 ? -mm : mm;
  if (magnitude < 0.05) mm = 0;  // Avoid a distracting -0.0 at the origin.
  if (magnitude < 100000)
    snprintf(dest, capacity, signedValue ? "%+.1f" : "%.1f", mm);
  else
    snprintf(dest, capacity, signedValue ? "%+.1e" : "%.1e", mm);
}

inline void scale(char* dest, size_t capacity, double mm) {
  if (mm < 10000) snprintf(dest, capacity, "%.0f", mm);
  else if (mm < 1000000) snprintf(dest, capacity, "%.0fk", mm / 1000);
  else snprintf(dest, capacity, "%.0e", mm);
}

template <class Canvas>
void render(Canvas& c, const DashboardData& d) {
  c.setTextFont(1);
  c.setTextWrap(false);
  c.fillScreen(BACKGROUND);
  text(c, 8, 7, "reBot", INK);
  text(c, 79, 7, "NO LIMIT", AMBER);

  const bool forward = d.state == State::Forward;
  const bool reverse = d.state == State::Reverse;
  const bool locked = d.state == State::Boot || d.state == State::Disarmed ||
                      d.state == State::ArmRelease;
  const uint16_t accent = d.state == State::Fault ? RED :
      reverse || locked || d.state == State::Release ? AMBER : CYAN;
  c.fillRoundRect(7, 23, 121, 24, 5, PANEL);
  if (forward || reverse) {
    if (reverse) c.fillTriangle(15, 35, 22, 30, 22, 40, accent);
    else c.fillTriangle(22, 35, 15, 30, 15, 40, accent);
  } else {
    c.fillCircle(18, 35, 3, accent);
  }
  text(c, 30, 32, label(d.state, d.reverse), accent);

  c.fillRoundRect(7, 53, 121, 59, 6, PANEL);
  text(c, 15, 61, "POSITION", MUTED);
  text(c, 96, 61, "EST.", MUTED);
  char value[32];
  distance(value, sizeof(value), d.positionMm, true);
  const int positionSize = strlen(value) <= 9 ? 2 : 1;
  text(c, 15, 78, value, INK, positionSize);
  text(c, 15, 101, "BOOT ZERO", MUTED);
  text(c, 108, 101, "mm", MUTED);

  const double half = d.viewHalfMm > 0 ? d.viewHalfMm : 100;
  char span[16], endpoint[20];
  scale(span, sizeof(span), half);
  snprintf(endpoint, sizeof(endpoint), "-%s", span);
  text(c, 9, 119, endpoint, MUTED);
  text(c, 65, 119, "0", INK);
  snprintf(endpoint, sizeof(endpoint), "+%s", span);
  text(c, 126 - static_cast<int>(strlen(endpoint)) * 6, 119, endpoint, MUTED);
  c.fillRoundRect(15, 133, 105, 9, 4, TRACK);
  double fraction = d.positionMm / half;
  if (fraction > 1) fraction = 1;
  if (fraction < -1) fraction = -1;
  const int marker = 67 + static_cast<int>(fraction * 49);
  if (marker > 67) c.fillRect(67, 135, marker - 67, 5, CYAN);
  if (marker < 67) c.fillRect(marker, 135, 67 - marker, 5, AMBER);
  c.drawLine(67, 130, 67, 145, MUTED);
  c.fillCircle(marker, 137, 3, INK);
  char caption[32];
  snprintf(caption, sizeof(caption), "VIEW +/-%s mm", span);
  text(c, 67 - static_cast<int>(strlen(caption)) * 3, 150, caption, MUTED);

  c.fillRoundRect(7, 164, 121, 40, 6, PANEL);
  text(c, 15, 171, "PULSE SPEED", MUTED);
  const double speed = forward || reverse ? d.speedMmS : 0;
  if (speed < 10000) snprintf(value, sizeof(value), "%.1f", speed);
  else snprintf(value, sizeof(value), "%.1e", speed);
  text(c, 15, 184, value, forward || reverse ? accent : INK,
       strlen(value) <= 6 ? 2 : 1);
  text(c, 97, 190, "mm/s", MUTED);

  char fwd[32], rev[32];
  distance(fwd, sizeof(fwd), d.forwardMm, false);
  distance(rev, sizeof(rev), d.reverseMm, false);
  snprintf(value, sizeof(value), "F %.7s", fwd);
  text(c, 8, 211, value, CYAN);
  snprintf(value, sizeof(value), "R %.7s", rev);
  text(c, 73, 211, value, AMBER);

  c.drawLine(8, 224, 126, 224, TRACK);
  const char* hint = "USR3 STOP / LOCK";
  if (d.state == State::Disarmed) hint = "HOLD USR3 TO ARM";
  else if (d.state == State::Fault) hint = d.fault;
  else if (d.state == State::Boot || d.state == State::ArmRelease ||
           d.state == State::Release) hint = "RELEASE ALL KEYS";
  else if (d.state == State::Ready) hint = "U1 FWD / U2 REV";
  text(c, 67 - static_cast<int>(strlen(hint)) * 3, 229, hint, accent);
}
}  // namespace dashboard
}  // namespace pulse_trial
