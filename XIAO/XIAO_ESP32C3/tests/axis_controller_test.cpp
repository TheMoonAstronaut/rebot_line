#define REBOT_ENABLE_HOMING 1
#include "AxisTestHarness.h"

int main() {
  fresh();arm();run(5000,1);
  assert(trial.moving() && trial.targetHz(millis())==6827 && timer.status().total>400);
  run(5,4);auto stopped=timer.status().total;run(1500,4);
  assert(!enabled && timer.status().total==stopped);run(50);run(500,1);assert(!enabled);
  puts("PASS normal jog 5s / 6827Hz / USR3 immediate stop / no held-stop re-arm");

  fresh();arm();run(1000,1);run(400,1,false);stopped=timer.status().total;
  run(300,1);assert(!linked && !enabled && timer.status().total==stopped);
  run(50);run(500,1);assert(!enabled && timer.status().total==stopped);
  puts("PASS heartbeat lock / held reconnect cannot resume");

  fresh();arm();run(1000,1);pins[LIMIT_POS_PIN]=0;stopped=timer.status().total;
  run(500,1);pins[LIMIT_POS_PIN]=1;run(500,1);assert(timer.status().total==stopped);
  run(1100,4);run(50);
  run(1000,2);assert(timer.status().total>stopped);
  assert(fault==AxisFault::None && limits.recovery()==0);
  puts("PASS arrival ISR interlock / no same-key restart / bounded slow escape");

  fresh();arm();pins[LIMIT_NEG_PIN]=0;run(30);
  run(1100,4);run(50);run(300,1);run(100);
  auto beforePause=timer.status().total;run(3500);run(200,1);
  assert(!limits.fault() && trial.state()!=State::Fault && timer.status().total>beforePause);
  puts("PASS 3.5s released pause does not consume escape movement budget");

  fresh();arm();pins[LIMIT_NEG_PIN]=0;run(30);pins[LIMIT_NEG_PIN]=1;run(30);
  pins[LIMIT_POS_PIN]=0;run(30);assert(!limits.fault() && fault==AxisFault::PositiveEnd && !enabled);
  puts("PASS sequential end inputs are not classified as simultaneous");

  fresh();arm();pins[LIMIT_NEG_PIN]=pins[LIMIT_POS_PIN]=0;run(30);
  assert(trial.state()==State::Fault && fault==AxisFault::BothLimits && !enabled);
  clearEndFault();pins[HOME_PIN]=0;run(1100,4);
  assert(fault==AxisFault::None && !enabled && trial.state()==State::ArmRelease);
  run(1500,4);assert(!enabled);run(50);assert(trial.state()==State::Ready);
  puts("PASS fault ACK with HOME active / one hold / release barrier before ready");

  fresh();arm();pins[LIMIT_NEG_PIN]=pins[LIMIT_POS_PIN]=0;run(30);clearEndFault();pins[HOME_PIN]=0;
  Serial.rx+="{\"cmd\":\"RESET_LIMITS\"}\n";run(5);
  assert(fault==AxisFault::None && linked && !enabled);
  assert(Serial.tx.find("\"type\":\"RESET_LIMITS\",\"ok\":1")!=std::string::npos);
  puts("PASS explicit reset works with live heartbeat and HOME active");

  completeHome(300);completeHome(-300);completeHome(2);completeHome(-800);completeHome(800);
  puts("PASS homing from both sides / already on home / both end start positions / canonical final edge / origin zero");

  completeHome(300);run(2100);run(1600,3);
  assert(homePending && !home.homed());
  assert(Serial.tx.find("\"homed\":0,\"home\":8")!=std::string::npos);
  run(50);const uint32_t rehomeUntil=millis()+10000;
  while(home.active() && millis()<rehomeUntil) run(1);
  assert(home.homed() && physical==4 && motion.positionPulses()==originOffset);
  puts("PASS explicit re-home invalidates old reference while pending and re-establishes the same zero edge");

  fresh();physical=300;physicalInputs();stepHook=physicalStep;requestHome();run(800);
  assert(timer.status().total>0);run(5,4);stopped=timer.status().total;run(1500,4);
  assert(!home.active() && !home.homed() && !enabled && timer.status().total==stopped);
  run(50);assert(!enabled);
  puts("PASS USR3 aborts homing / no home success / no same-hold auto-arm");

  fresh();physical=300;physicalInputs();stepHook=physicalStep;requestHome();run(800);run(400,0,false);
  stopped=timer.status().total;run(300,0,false);
  assert(!home.active() && !home.homed() && !linked && timer.status().total==stopped);
  puts("PASS heartbeat aborts homing without continuing on reconnect");

  fresh();run(2100);HomingConfig small;small.seekPulses=10;home=AxisHoming(small);
  assert(startHome(millis()));run(1000);
  assert(!home.homed() && fault==AxisFault::HomePulses && timer.status().total==10);
  puts("PASS missing home input stops at exact hardware pulse budget");

  fresh();run(2100);assert(startHome(millis()));run(700);pins[HOME_PIN]=0;run(30);
  assert(home.state()==axis_control::Home::Backoff);run(600);pins[LIMIT_POS_PIN]=0;run(30);
  assert(!home.homed() && fault==AxisFault::HomeEnd && !enabled);
  puts("PASS unexpected end during backoff faults and disables driver");

  fresh();arm();run(1000,1);stopped=timer.status().total;
  pins[LIMIT_POS_PIN]=0;advance(4000);
  assert(timer.status().total==stopped && timer.status().limitHits==2 && !timer.status().fault);
  run(1);assert(!trial.moving());
  puts("PASS end-limit ISR blocks rising edges before delayed control loop reads the input");

  fresh();requestHome();run(700);stopped=timer.status().total;
  pins[HOME_PIN]=0;advance(12000);
  assert(timer.status().total==stopped && timer.status().inputHit && !timer.status().fault);
  pins[HOME_PIN]=1;run(300);
  assert(!home.homed() && fault==AxisFault::HomeUnstable && !enabled);
  puts("PASS HOME ISR stops before delayed loop / transient edge cannot establish a false zero");

  fresh();physical=300;physicalInputs();stepHook=physicalStep;run(2100);
  run(800,3);run(100);assert(!home.active() && !homePending && timer.status().total==0);
  run(1600,3);assert(homePending);run(5,4);run(100);
  assert(!home.active() && !homePending && timer.status().total==0);
  puts("PASS incomplete home gesture / USR3 cancels pending home without motion");

  fresh();arm();run(1000,1);advance(50000);run(5);
  assert(fault==AxisFault::Timer);run(1500,4);run(50);
  assert(fault==AxisFault::Timer && !timerReady && !enabled);
  puts("PASS timer fault cannot be cleared by button acknowledgement");

  fresh();run(2100);pins[LIMIT_NEG_PIN]=pins[LIMIT_POS_PIN]=0;run(30);
  run(1100,4);run(50);
  assert(fault==AxisFault::BothLimits && !enabled && timer.status().total==0);
  puts("PASS arming with both end switches active cannot enable motion");
}
