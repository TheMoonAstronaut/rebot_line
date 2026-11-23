#pragma once
#include "mock/MockArduino.h"
#include "../axis_c3_jog/axis_c3_jog.ino"

uint32_t sequence=0,nextFrame=0;
int previousMask=-1;
int physical=0;
void physicalInputs() {
  setInput(HOME_PIN,physical>=0 && physical<=4 ? LOW : HIGH);
  pins[LIMIT_NEG_PIN]=physical<=-800 ? LOW : HIGH;
  pins[LIMIT_POS_PIN]=physical>=800 ? LOW : HIGH;
}
void physicalStep() { physical+=pins[DIR_PIN] ? 1 : -1;physicalInputs(); }
void fresh() {
  timers.clear();ticks=0;stepHook=nullptr;std::fill(std::begin(pins),std::end(pins),1);Serial={};
  std::fill(std::begin(inputInterrupts),std::end(inputInterrupts),nullptr);
  trial=JogTrial{PROFILE};motion=MotionTelemetry{};
  timer.~StepPulseTimer();new (&timer) StepPulseTimer{STEP_PIN,LIMIT_NEG_PIN,LIMIT_POS_PIN};
  limits=AxisLimits{};home=AxisHoming{};reader=usb_jog::LineReader{};fault=AxisFault::None;
  linked=timerReady=appliedReverse=enabled=haveSequence=false;
  rawMask=stableMask=0;inputRaw=7;
  lastKeysAt=changedAt=lastSequence=lastReport=lastTotal=0;
  ackAt=homeHoldAt=0;ackTiming=ackUsed=homeTiming=homePending=false;originOffset=0;
  seenEnds=acknowledgedEnd=0;originSeen=zeroSequence=originLowAt=originHighAt=0;
  originPending=originLowTiming=originHighTiming=manualReference=false;pendingOrigin=0;
  sequence=nextFrame=0;previousMask=-1;setup();
}
void run(uint32_t ms,int mask=0,bool heartbeat=true) {
  const uint32_t until=millis()+ms;
  while(millis()<until) {
    if(heartbeat && (mask!=previousMask || millis()>=nextFrame)) {
      char line[96];snprintf(line,sizeof(line),"{\"cmd\":\"KEYS\",\"seq\":%u,\"mask\":%d}\n",++sequence,mask);
      Serial.rx+=line;nextFrame=millis()+20;previousMask=mask;
    }
    loop();
  }
}
void arm() {run(2100);run(1100,4);run(50);assert(trial.state()==State::Ready);}
void clearEndFault() {
  pins[LIMIT_NEG_PIN]=pins[LIMIT_POS_PIN]=1;run(30);
}
void requestHome() {
  const auto before=timer.status().total;
  run(2100);assert(trial.state()==State::Disarmed);
  run(1600,3);assert(homePending && !home.active() && timer.status().total==before);
  run(50);assert(home.active());
}
void completeHome(int initial) {
  fresh();physical=initial;physicalInputs();stepHook=physicalStep;
  if(initial==-800 || initial==800) {
    run(2100);run(1100,4);run(50);
    run(150,initial<0 ? 1 : 2);run(100);run(5,4);run(100);
  }
  requestHome();
  const uint32_t deadline=millis()+30000;
  while(home.active() && millis()<deadline) run(1);
  assert(home.homed() && home.state()==axis_control::Home::Homed);
  assert(fault==AxisFault::None && !enabled && physical==4);
  assert(motion.positionPulses()==originOffset && limits.recovery()==0);
  const auto total=timer.status().total;run(500);
  assert(timer.status().total==total && !enabled);
  assert(Serial.tx.find("\"homed\":1,\"home\":5,\"fault\":0,\"zeroed\":1")!=std::string::npos);
}
