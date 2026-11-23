#include <cassert>
#include <cmath>
#include <cstdio>
#include "../xiao_pulse_trial/MotionTelemetry.h"

using pulse_trial::MotionTelemetry;
void near(double actual, double expected) {
  assert(std::abs(actual - expected) < 0.000001);
}

int main() {
  MotionTelemetry m;
  m.reset(0);
  m.observe(0, false, 2000);
  near(m.positionMm(), 0);
  m.setMoving(true, 2000);
  m.observe(3200, false, 2100);
  near(m.positionMm(), 37.5);
  near(m.forwardMm(), 37.5);
  near(m.reverseMm(), 0);
  near(m.speedMmS(), 375);

  // The last emitted edge still belongs to forward travel at stop.
  m.observe(3201, false, 2101);
  m.setMoving(false, 2101);
  near(m.speedMmS(), 0);
  m.observe(3201, false, 2700);
  m.setMoving(true, 2700);
  near(m.speedMmS(), 0);
  m.observe(4801, true, 2800);
  near(m.positionMm(), 1601 * MotionTelemetry::MM_PER_PULSE);
  near(m.reverseMm(), 18.75);
  assert(m.positionPulses() == 1601);
  m.observe(6402, true, 2900);
  near(m.positionMm(), 0);
  // Continue through zero, then return. Cumulative totals never cancel.
  m.observe(9602, true, 3000);
  near(m.positionMm(), -37.5);
  m.observe(12802, false, 3100);
  near(m.positionMm(), 0);
  near(m.forwardMm(), m.reverseMm());

  // View scale expands at either end and stays stable on the return to zero.
  m.reset(0);
  m.observe(10000, false, 100);
  m.setMoving(true, 100);
  near(m.viewHalfMm(), 200);
  m.observe(20000, true, 200);
  m.setMoving(false, 200);
  near(m.positionMm(), 0);
  near(m.viewHalfMm(), 200);
  m.observe(60000, true, 300);
  m.setMoving(true, 300);
  near(m.viewHalfMm(), 800);

  // A wrapped 32-bit hardware total must still add exactly seven pulses;
  // position and cumulative distance must not wrap with it.
  m.reset(0);
  m.observe(UINT32_MAX - 4, false, 100);
  const int64_t before = m.positionPulses();
  m.observe(2, false, 200);
  assert(m.positionPulses() == before + 7);
  assert(m.forwardPulses() == static_cast<uint64_t>(UINT32_MAX) + 3);
  m.observe(9, true, 300);
  assert(m.positionPulses() == before);

  // Rate windows work across millis() wrap and idle always displays zero.
  m.reset(UINT32_MAX - 20);
  m.setMoving(true, UINT32_MAX - 20);
  m.observe(100, false, 79);
  assert(m.pulseHz() == 1000);
  m.setMoving(false, 80);
  assert(m.pulseHz() == 0);
  m.setMoving(true, 81);
  assert(m.pulseHz() == 0);
  m.observe(782, false, 181);
  assert(m.pulseHz() == 6820);
  near(m.speedMmS(), 79.921875);
  m.reset(200);
  near(m.positionMm(), 0);
  near(m.forwardMm(), 0);
  near(m.reverseMm(), 0);
  near(m.viewHalfMm(), 100);
  puts("PASS: signed travel, cumulative totals, speed, viewport and counter wraps");
}
