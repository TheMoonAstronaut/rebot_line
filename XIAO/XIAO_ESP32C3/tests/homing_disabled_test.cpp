#include "AxisTestHarness.h"
#include "../../XIAO_1.14inch_IPS_Display_ESP32-S3/panel_s3_jog/PanelControl.h"

static_assert(!axis_control::HOMING_ENABLED, "Production build must keep HOME disabled");
int main() {
  fresh();run(2100);
  Serial.rx+="{\"cmd\":\"HOME\"}\n";run(100);
  assert(!home.active() && timer.status().total==0 && !enabled);
  assert(Serial.tx.find("\"type\":\"HOME\",\"ok\":0")!=std::string::npos);
  run(3000,3);run(200);
  assert(!homePending && !home.active() && !enabled && timer.status().total==0);
  assert(std::string(panel_control::hint(State::Disarmed,true,AxisFault::None,
         axis_control::Home::Unhomed,0,7))=="HOLD USR3 TO ARM");
  run(1100,4);run(50);assert(trial.state()==State::Ready);
  run(4000,1);assert(trial.moving() && trial.targetHz(millis())==6827);
  run(5,4);assert(!enabled && !trial.moving());
  puts("PASS production HOME command / long U1+U2 cannot move; home hint hidden; normal 6827Hz jog and USR3 stop retained");
}
