#define MOCK_PANEL
#include "mock/MockArduino.h"
#include "../../XIAO_1.14inch_IPS_Display_ESP32-S3/panel_s3_jog/panel_s3_jog.ino"

int main() {
  static_assert(!axis_control::HOMING_ENABLED);
  ticks=2000000;std::fill(std::begin(pins),std::end(pins),1);setup();
  receive(R"({"cmd":"AXIS","state":1,"rev":0,"linked":1,"hz":0,"fwd":500,"back":0,"raw":5,"recovery":0,"homed":0,"home":0,"fault":0,"zeroed":1,"zero":1,"pos":0})");
  loop();assert(frame.lastTextAt(101)=="ORIGIN ZERO" && frame.lastTextAt(78)=="+0.0");
  assert(frame.lastTextAt(229)=="HOLD USR3 TO ARM");
  published.position=85;advance(200000);loop();assert(frame.lastTextAt(78)=="+1.0");
  published.state=State::Fault;published.fault=axis_control::Fault::NegativeEnd;published.raw=6;loop();
  assert(frame.lastTextAt(101)=="NEG END STOP" && frame.lastTextAt(229)=="HOLD U3 TO UNLOCK");
  published.fault=axis_control::Fault::BothLimits;published.raw=2;loop();
  assert(frame.lastTextAt(229)=="CLEAR END LIMITS");
  puts("PASS manual-origin reference displayed independently from disabled automatic home; active single end can be acknowledged");
}
