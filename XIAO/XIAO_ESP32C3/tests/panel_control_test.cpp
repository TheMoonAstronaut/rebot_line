#define REBOT_ENABLE_HOMING 1
#define MOCK_PANEL
#include "mock/MockArduino.h"
#include "../../XIAO_1.14inch_IPS_Display_ESP32-S3/panel_s3_jog/panel_s3_jog.ino"

int main() {
  ticks=2000000;std::fill(std::begin(pins),std::end(pins),1);setup();
  published={State::Disarmed,false,true,0,millis(),0,0,7,1};loop();
  assert(frame.lastTextAt(229)=="U3 ARM U1+U2 HOME");
  published.state=State::Release;loop();assert(frame.lastTextAt(229)=="RELEASE ALL KEYS");
  published.state=State::Fault;published.fault=axis_control::Fault::BothLimits;loop();
  assert(frame.lastTextAt(229)=="HOLD U3 TO UNLOCK");
  published.raw=6;loop();assert(frame.lastTextAt(229)=="CLEAR END LIMITS");
  published.fault=axis_control::Fault::Timer;loop();assert(frame.lastTextAt(229)=="RESTART REQUIRED");
  receive(R"({"cmd":"AXIS","state":1,"rev":0,"linked":1,"hz":0,"fwd":999,"back":2,"raw":5,"recovery":0,"homed":1,"home":5,"fault":0,"zeroed":1,"zero":1,"pos":0})");
  assert(published.homed && published.home==axis_control::Home::Homed && published.position==0);
  loop();assert(frame.lastTextAt(101)=="HOMED ZERO");
  auto last=published;
  receive(R"({"cmd":"AXIS","state":1,"rev":0,"linked":1,"hz":0,"fwd":999,"back":2,"raw":5,"recovery":0,"homed":1,"home":2,"fault":0,"zeroed":1,"zero":1,"pos":0})");
  assert(published.home==last.home);  // Invalid reference/phase combination is rejected.
  published.homed=false;published.home=axis_control::Home::Seek;published.state=State::Reverse;loop();
  assert(frame.lastTextAt(229)=="U3 STOP / CANCEL" && frame.lastTextAt(101)=="SEEK HOME");
  advance(600*2000);loop();assert(frame.lastTextAt(229)=="USB LINK LOST");
  puts("PASS panel hint priority / fault recovery instructions / homed=1 / independent zero position / home progress / stale link");
}
