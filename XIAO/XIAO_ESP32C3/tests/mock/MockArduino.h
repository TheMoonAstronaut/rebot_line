#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <memory>
#include <new>
#include <algorithm>
#include <assert.h>
constexpr int HIGH=1, LOW=0, INPUT=0, OUTPUT=1, INPUT_PULLUP=2;
inline int pins[64];
#define ARDUINO_ISR_ATTR
constexpr int FALLING=1;
inline void (*inputInterrupts[64])() = {};
inline int digitalPinToInterrupt(int p) { return p; }
inline void attachInterrupt(int p, void(*callback)(), int) { inputInterrupts[p]=callback; }
inline void setInput(int p,int v) {
  const bool falling=pins[p] && !v;
  pins[p]=v;
  if(falling && inputInterrupts[p]) inputInterrupts[p]();
}
#ifdef MOCK_PANEL
constexpr int D1=2,D2=3,D3=4,D4=5,D5=6,D6=43,D7=44,D10=9,D11=38,D12=39,D13=40,D19=11;
#else
constexpr int D1=3,D2=4,D3=5,D4=6,D5=7,D10=10;
#endif
inline uint64_t ticks=0;
using gpio_num_t=int;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
inline int GPIO=0;
inline void (*stepHook)() = nullptr;
inline void pinMode(int,int) {}
inline void digitalWrite(int p,int v) {pins[p]=v;}
inline int digitalRead(int p) {return pins[p];}
inline void gpio_set_level(int p,int v) {pins[p]=v;}
inline void gpio_ll_set_level(int*,int p,int v) {
  const bool rising=p==4 && v && !pins[p];
  pins[p]=v;
  if(rising && stepHook) stepHook();
}
inline int gpio_ll_get_level(int*,int p) {return pins[p];}
inline uint32_t millis() {return ticks/2000;}
struct MockSerial {
  std::string rx,tx;
  void begin(int) {}
  void setTxTimeoutMs(int) {}
  int available() {return rx.size();}
  int read() {if(rx.empty()) return -1; int b=rx.front();rx.erase(0,1);return b;}
  int availableForWrite() {return 4096;}
  size_t write(const uint8_t* p,size_t n) {tx.append(reinterpret_cast<const char*>(p),n);return n;}
};
inline MockSerial Serial;
constexpr int ESP_OK=0, GPTIMER_CLK_SRC_DEFAULT=0, GPTIMER_COUNT_UP=0;
inline int esp_efuse_mac_get_default(uint8_t* p) {memset(p,0,6);return ESP_OK;}
struct gptimer_alarm_event_data_t {uint64_t count_value,alarm_value;};
struct MockTimer;
using gptimer_handle_t=MockTimer*;
using AlarmCallback=bool(*)(gptimer_handle_t,const gptimer_alarm_event_data_t*,void*);
struct MockTimer {AlarmCallback callback=nullptr;void* context=nullptr;bool pending=false;uint64_t alarm=0;};
struct gptimer_config_t {int clk_src=0,direction=0,resolution_hz=0;};
struct gptimer_event_callbacks_t {AlarmCallback on_alarm=nullptr;};
struct gptimer_alarm_config_t {uint64_t alarm_count=0;};
inline std::vector<std::unique_ptr<MockTimer>> timers;
inline int gptimer_new_timer(const gptimer_config_t*,gptimer_handle_t* out) {
  timers.emplace_back(new MockTimer);*out=timers.back().get();return ESP_OK;
}
inline int gptimer_register_event_callbacks(gptimer_handle_t t,const gptimer_event_callbacks_t* c,void* p) {
  t->callback=c->on_alarm;t->context=p;return ESP_OK;
}
inline int gptimer_enable(gptimer_handle_t) {return ESP_OK;}
inline int gptimer_start(gptimer_handle_t) {return ESP_OK;}
inline int gptimer_get_raw_count(gptimer_handle_t,uint64_t* n) {*n=ticks;return ESP_OK;}
inline int gptimer_set_alarm_action(gptimer_handle_t t,const gptimer_alarm_config_t* c) {
  t->alarm=c->alarm_count;t->pending=true;return ESP_OK;
}
inline void advance(uint64_t delta) {
  const uint64_t target=ticks+delta;
  for(;;) {
    MockTimer* next=nullptr;
    for(auto& t:timers) if(t->pending && t->alarm<=target && (!next || t->alarm<next->alarm)) next=t.get();
    if(!next) break;
    assert(next->alarm>=ticks);
    ticks=next->alarm;next->pending=false;
    gptimer_alarm_event_data_t event{ticks,ticks};
    next->callback(next,&event,next->context);
  }
  ticks=target;
}
inline void delay(uint32_t ms) {advance(uint64_t(ms)*2000);}
inline void vTaskDelay(uint32_t ms) {delay(ms);}
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,int,void*,int,void*,int) {return 1;}
