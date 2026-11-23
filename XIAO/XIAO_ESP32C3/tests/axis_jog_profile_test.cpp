#include <assert.h>
#include <stdio.h>
#include "../axis_c3_jog/AxisJogProfile.h"
#include "../axis_c3_jog/AxisLimits.h"

int main() {
  using namespace pulse_trial;
  JogTrial jog{axis_c3::JOG_PROFILE};
  jog.reset(0);
  jog.tick({}, {}, 2000);
  const Keys arm{false, false, true}, fwd{true, false, false};
  jog.tick(arm, arm, 2001);
  jog.tick(arm, arm, 3001);
  jog.tick({}, {}, 3002);
  jog.tick(fwd, fwd, 3003);
  jog.tick(fwd, fwd, 3103);
  for (uint32_t now = 3113; now <= 8103; now += 10) {
    jog.recordPulses(20, now);
    jog.tick(fwd, fwd, now);
    assert(jog.moving() && "A held normal jog must not stop at 400 pulses / 3 seconds");
    assert(axis_c3::remainingJogPulses(jog.count()) == UINT32_MAX);
    if (now >= 5103) assert(jog.targetHz(now) == 6827);
  }
  assert(jog.count() > 400);
  jog.tick({}, {}, 8104);
  assert(!jog.moving() && jog.targetHz(8104) == 0);
  AxisLimits limits;
  limits.sample(1, 0, 0);
  assert(limits.allowedHz(6827, true, 0, 0) == 0);
  assert(limits.allowedHz(6827, false, 0, 0) == 200);
  assert(limits.allowedHz(6827, false, 3000, 20) == 0 && limits.fault());
  puts("PASS: normal jog holds beyond 400 pulses / 3 seconds at 6827Hz; release stops; limit escape remains capped and bounded");
}
