#include "AxisTestHarness.h"
#include <cstdlib>

int main(int argc,char** argv) {
  const int test=argc>1 ? std::atoi(argv[1]) : 0;
  if(test==0 || test==1) {
    fresh();arm();run(800,1);run(30);setInput(HOME_PIN,LOW);run(5);
    assert(manualReference && originOffset!=0);
    const auto position=timer.status().position-originOffset;
    pins[LIMIT_POS_PIN]=LOW;run(10);
    assert(fault==AxisFault::PositiveEnd);
    assert(manualReference && timer.status().position-originOffset==position);
    clearEndFault();run(1100,4);run(50);
    assert(manualReference && timer.status().position-originOffset==position);
    run(400,0,false);assert(manualReference && timer.status().position-originOffset==position);
    puts("PASS end-stop, acknowledgement and link loss preserve origin coordinate");
  }
  if(test==0 || test==2) {
    fresh();arm();pins[LIMIT_POS_PIN]=LOW;run(30);
    run(1100,4);run(50); // One acknowledgement must arm on release.
    assert(trial.state()==State::Ready);
    const auto before=timer.status().total;
    run(350,2);assert(timer.status().total>before && trial.reverse());
    pins[LIMIT_POS_PIN]=HIGH;run(5000,2);
    assert(fault==AxisFault::None && trial.moving() && limits.recovery()==0);
    assert(trial.targetHz(millis())==6827);
    assert(timer.status().total-before>15000);
    puts("PASS held retreat continues normally after sensor clears, without escape-budget lock");
  }
  if(test==0 || test==3) {
    // Test both ends with both previous jog directions, first key is the away key.
    for(int end=1;end<=2;++end) for(int prior=1;prior<=2;++prior) {
      fresh();arm();run(500,prior);run(50);
      pins[end==1 ? LIMIT_NEG_PIN : LIMIT_POS_PIN]=LOW;run(30);
      run(1100,4);run(50);
      assert(trial.state()==State::Ready);
      const auto before=timer.status().total;
      const int away=end==1 ? 1 : 2;
      run(500,away);
      assert(timer.status().total>before && fault==AxisFault::None);
      assert(trial.reverse()==(end==2));
    }
    puts("PASS first away request works for D4->USR1 and D10->USR2, regardless of prior direction");
  }
  if(test==0 || test==4) {
    fresh();arm();pins[LIMIT_NEG_PIN]=LOW;run(30);run(1100,4);
    assert(fault==AxisFault::None && trial.state()==State::ArmRelease);
    const auto before=timer.status().total;run(1500,4);
    assert(timer.status().total==before && !enabled);
    run(50);assert(trial.state()==State::Ready);
    puts("PASS single USR3 hold acknowledges and arms only after release");
  }
  if(test==0 || test==5) {
    fresh();arm();pins[LIMIT_NEG_PIN]=LOW;run(30);
    run(1100,4);run(50);run(3500,1);
    assert(fault==AxisFault::EscapePulses && timer.status().total==400 && !enabled);
    fresh();arm();pins[LIMIT_NEG_PIN]=pins[LIMIT_POS_PIN]=LOW;run(30);run(1500,4);run(50);
    assert(fault==AxisFault::BothLimits && !enabled && timer.status().total==0);
    fresh();arm();pins[LIMIT_POS_PIN]=LOW;run(30);run(1100,4);run(200,2);
    assert(!enabled && timer.status().total==0);
    run(50);run(300,2);assert(timer.status().total==0 && !enabled);
    puts("PASS still-held sensor retains 400-pulse cap; both ends and release-barrier violations stay locked");
  }
}
