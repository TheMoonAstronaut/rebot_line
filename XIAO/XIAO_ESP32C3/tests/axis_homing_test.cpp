#include <assert.h>
#include <stdio.h>
#include "../axis_c3_jog/AxisHoming.h"

int main() {
  using F=axis_control::Fault;
  using H=axis_control::Home;
  HomingConfig c;c.seekMs=600;c.clearPulses=10;
  AxisHoming h(c);assert(h.start(0,0,false));
  assert(h.targetHz(499)==0 && h.targetHz(500)==200);
  h.tick(599,0,false,false,false,0);assert(h.fault()==F::None);
  h.tick(600,0,false,false,false,0);assert(h.fault()==F::HomeTime && !h.homed());
  h.reset();assert(h.start(0,0,true));assert(!h.reverse());
  h.tick(501,10,true,false,false,0);assert(h.fault()==F::HomePulses);
  h.reset();assert(h.start(0,0,false));
  h.tick(510,1,false,true,false,0);assert(h.targetHz(520)==0);
  h.tick(760,1,false,false,false,0);assert(h.fault()==F::HomeUnstable);
  h.reset();assert(h.start(0,0,false));h.tick(501,1,false,false,false,3);
  assert(h.fault()==F::HomeEnd);
  h.reset();assert(h.start(0,0,false));h.cancel();assert(!h.active() && !h.homed() && h.state()==H::Cancelled);
  h.reset();assert(h.start(UINT32_MAX-100,UINT32_MAX-10,false));
  assert(h.targetHz(399)==200);h.tick(500,5,false,false,false,0);
  assert(h.fault()==F::HomeTime);  // Millis and pulse counter rollover.
  puts("PASS home settling / per-phase time and distance limits / input instability / dual-end fault / cancel / rollover");
}
