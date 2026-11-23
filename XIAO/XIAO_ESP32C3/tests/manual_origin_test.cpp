#include "AxisTestHarness.h"

int main() {
  fresh();arm();run(2500,1);
  const auto edge=timer.status().position;
  const auto before=timer.status().total;
  setInput(HOME_PIN,LOW);advance(10000);run(5,1);
  assert(manualReference && zeroSequence==1 && originOffset==edge);
  assert(trial.moving() && enabled && fault==AxisFault::None && trial.targetHz(millis())==6827);
  assert(timer.status().total>before && timer.status().position-originOffset>0);
  run(100,1);assert(zeroSequence==1);
  setInput(HOME_PIN,HIGH);run(30,1);
  const auto second=timer.status().position;setInput(HOME_PIN,LOW);run(5,1);
  assert(zeroSequence==2 && originOffset==second && trial.moving());
  puts("PASS moving origin edge captures exact STEP coordinate despite delayed loop, without stopping or resetting total");

  fresh();arm();run(500,1);run(100);const auto stopped=timer.status().position;
  setInput(HOME_PIN,LOW);run(5);
  assert(manualReference && originOffset==stopped && timer.status().position-originOffset==0);
  assert(!home.homed() && home.state()==axis_control::Home::Unhomed);
  puts("PASS stationary origin input zeroes reference; automatic homing remains disabled");

  fresh();arm();run(1000,1);setInput(HOME_PIN,LOW);run(1,1);setInput(HOME_PIN,HIGH);run(30,1);
  assert(!manualReference && zeroSequence==0 && trial.moving());
  puts("PASS sub-2ms origin glitch cannot reset reference or interrupt motion");

  fresh();arm();run(1000,1);const auto arrival=timer.status().total;
  pins[LIMIT_NEG_PIN]=0;advance(4000);run(10,1);
  assert(fault==AxisFault::NegativeEnd && !enabled && timer.status().total==arrival);
  run(300,2);assert(timer.status().total==arrival);
  run(300,1);assert(timer.status().total==arrival);
  run(1100,4);run(50);assert(fault==AxisFault::None);
  assert(trial.state()==State::Ready && limits.recovery()==1);
  run(200,2);assert(timer.status().total==arrival);
  run(50);run(500,1);assert(timer.status().total>arrival);
  assert(Serial.tx.find("\"target\":200")!=std::string::npos);
  puts("PASS opposite-end trip stops BOTH directions before ACK; after one-hold unlock only low-speed escape is permitted");
}
