#include <assert.h>
#include <stdio.h>
#include "../axis_c3_jog/AxisLimits.h"
#include "../../XIAO_1.14inch_IPS_Display_ESP32-S3/xiao_pulse_trial/JogTrial.h"

int main() {
  {
    AxisLimits l;
    l.sample(1, 0, 0, false);
    assert(l.blocked(true) && l.recovery() == 0 && !l.fault());
    l.sample(0, 0, 1, false);
    l.sample(0, 0, 21, false);
    l.sample(2, 0, 22, false);
    assert(l.blocked(false) && l.recovery() == 0 && !l.fault());
    l.sample(2, 0, 23, true);  // A switch active on arming still enforces escape.
    assert(l.recovery() == 2 && l.blocked(false) && !l.blocked(true));
  }
  {
    AxisLimits l;
    l.sample(1, 0, 0);
    assert(l.blocked(true) && !l.blocked(false));
    assert(l.allowedHz(6827, false, 10, 100) == 200);
    l.sample(0, 0, 20);
    l.sample(0, 0, 39);
    assert(l.recovery() == 1 && l.blocked(true));
    // Releasing a button while the switch is still debouncing cannot clear it.
    l.observe(101, true);
    assert(l.recovery() == 1);
    l.sample(0, 0, 40);
    l.observe(101, false);
    assert(l.recovery() == 0);
    l.observe(101, true);
    assert(l.recovery() == 0 && !l.blocked(true));
  }
  {
    AxisLimits l;
    l.sample(2, 0, 0);
    assert(l.blocked(false) && !l.blocked(true));
    assert(l.allowedHz(200, false, 0, 0) == 0);
    l.sample(0, 0, 1);
    l.sample(2, 0, 20);
    l.sample(0, 0, 21);
    l.sample(0, 0, 40);
    assert(l.blocked(false));
    // Removing the obstruction without a retreat does not acknowledge arrival.
    l.sample(0, 0, 41);
    l.observe(0, true);
    assert(l.recovery() == 2);
  }
  {
    AxisLimits l;
    l.sample(0, 1, 0);  // Even a transient caught only by the pulse ISR is latched.
    assert(l.blocked(true) && l.recovery() == 1);
    l.sample(0, 0, 1);
    l.sample(0, 0, 21);
    l.sample(2, 0, 22);
    assert(!l.fault() && l.recovery() == 2 && l.blocked(false) && !l.blocked(true));
  }
  {
    AxisLimits l;
    l.sample(1, 0, 0);
    assert(l.allowedHz(6827, false, 100, 0) == 200);
    l.pause(200);
    assert(l.allowedHz(6827, false, 10000, 20) == 200);
    assert(!l.fault());
    assert(l.allowedHz(6827, false, 12900, 30) == 0 && l.fault());
  }
  {
    AxisLimits l;
    l.sample(3, 0, 0);
    assert(l.fault() && l.allowedHz(200, false, 1, 0) == 0);
  }
  {
    AxisLimits l;
    l.sample(1, 0, 0);
    assert(l.allowedHz(20, false, 100, 20) == 20);
    assert(l.remaining(419) == 1);
    assert(l.allowedHz(200, false, 200, 419) == 200);
    assert(l.allowedHz(200, false, 201, 420) == 0 && l.fault());
  }
  {
    AxisLimits l;
    l.sample(2, 0, 0);
    assert(l.allowedHz(200, true, 100, 0) == 200);
    l.observe(10, true);  // Still on the limit: releases cannot renew the budget.
    assert(l.allowedHz(200, true, 3099, 10) == 200);
    assert(l.allowedHz(200, true, 3100, 10) == 0 && l.fault());
  }
  {
    AxisLimits l;
    l.sample(1, 0, UINT32_MAX - 10);
    assert(l.allowedHz(200, false, UINT32_MAX - 5, UINT32_MAX - 10) == 200);
    assert(l.remaining(9) == 380);  // Pulse counter rollover.
    assert(l.allowedHz(200, false, 5, 9) == 200);  // Millis rollover.
  }
  {
    using namespace pulse_trial;
    JogTrial jog{{20, 200, 500, 3000, 400, RunMode::Limited}};
    jog.reset(0);
    jog.tick({}, {}, 2000);
    const Keys arm{false, false, true}, fwd{true, false, false};
    jog.tick(arm, arm, 2001);
    jog.tick(arm, arm, 3001);
    jog.tick({}, {}, 3002);
    assert(jog.state() == State::Ready);
    jog.tick(fwd, fwd, 3003);
    jog.tick(fwd, fwd, 3103);
    assert(jog.moving());
    jog.inhibit(3104);
    jog.tick(fwd, fwd, 4000);
    assert(!jog.moving() && jog.state() == State::Release);
    jog.tick({}, {}, 4001);
    assert(jog.state() == State::Ready);
    jog.tick(fwd, fwd, 4002);
    jog.tick(fwd, fwd, 4102);
    jog.recordPulses(400, 4103);
    assert(!jog.moving() && jog.state() == State::Release);
  }
  puts("PASS: directional blocking, ISR hit latch, release filtering, escape acknowledgement, dual-limit fault, cumulative budgets, rollover and key-release interlock");
}
