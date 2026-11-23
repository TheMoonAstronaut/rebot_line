#pragma once

#include <stdint.h>
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "hal/gpio_ll.h"

// The control task renews permission to send rising edges. A stop lets the
// current high pulse finish, then prevents any further edges.
class StepPulseTimer {
 public:
  explicit StepPulseTimer(uint32_t pin, int negativePin = -1, int positivePin = -1)
      : pin_(pin), negativePin_(negativePin), positivePin_(positivePin) {}

  bool begin() {
    gptimer_config_t config{};
    config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    config.direction = GPTIMER_COUNT_UP;
    config.resolution_hz = TIMER_HZ;
    gptimer_event_callbacks_t callbacks{};
    callbacks.on_alarm = onAlarm;
    return gptimer_new_timer(&config, &timer_) == ESP_OK &&
           gptimer_register_event_callbacks(timer_, &callbacks, this) == ESP_OK &&
           gptimer_enable(timer_) == ESP_OK && gptimer_start(timer_) == ESP_OK;
  }

  void command(uint32_t hz, uint32_t pulseLimit, bool reverse = false,
               int inputPin = -1, bool inputLevel = false, uint8_t allowedEnd = 3) {
    portENTER_CRITICAL(&mux_);
    if (hz == 0) {
      periodTicks_ = 0;
    } else if (!fault_ && limitHits_ == 0 && !inputHit_) {
      uint64_t now = 0;
      if (gptimer_get_raw_count(timer_, &now) != ESP_OK ||
          (periodTicks_ != 0 && now >= leaseUntil_)) {
        fail();
      } else {
        if (periodTicks_ == 0) remaining_ = pulseLimit;
        else if (pulseLimit < remaining_) remaining_ = pulseLimit;
        reverse_ = reverse;
        inputPin_ = inputPin;
        inputLevel_ = inputLevel;
        allowedEnd_ = allowedEnd;
        periodTicks_ = (TIMER_HZ + hz / 2) / hz;
        leaseUntil_ = now + LEASE_TICKS;
        if (!armed_ && remaining_ != 0) armAt(now + START_DELAY_TICKS);
      }
    }
    portEXIT_CRITICAL(&mux_);
  }

  struct Status {
    uint32_t total;
    bool high;
    bool fault;
    uint8_t limitHits;
    bool inputHit;
    int64_t position, originPosition;
    uint32_t originSequence, originAt;
  };

  Status status() {
    portENTER_CRITICAL(&mux_);
    const Status result{total_, high_, fault_, limitHits_, inputHit_, position_, originPosition_, originSequence_, originAt_};
    portEXIT_CRITICAL(&mux_);
    return result;
  }

  void ARDUINO_ISR_ATTR captureOriginFromISR(uint32_t now) {
    portENTER_CRITICAL_ISR(&mux_);
    if (originArmed_) {
      originPosition_ = position_;
      originAt_ = now;
      ++originSequence_;
      originArmed_ = false;
    }
    portEXIT_CRITICAL_ISR(&mux_);
  }
  void armOrigin(int pin) {
    portENTER_CRITICAL(&mux_);
    if (gpio_ll_get_level(&GPIO, pin)) originArmed_ = true;
    portEXIT_CRITICAL(&mux_);
  }

  void acknowledgeInput() {
    portENTER_CRITICAL(&mux_);
    inputHit_ = false;
    portEXIT_CRITICAL(&mux_);
  }

  // Call only after the control task has inhibited the interrupted jog.
  void acknowledgeLimits() {
    portENTER_CRITICAL(&mux_);
    limitHits_ = 0;
    portEXIT_CRITICAL(&mux_);
  }

 private:
  static constexpr uint32_t TICKS_PER_US = 2;
  static constexpr uint32_t TIMER_HZ = 1000000 * TICKS_PER_US;
  static constexpr uint32_t PULSE_HIGH_TICKS = 60 * TICKS_PER_US;
  static constexpr uint32_t MIN_LOW_TICKS = 60 * TICKS_PER_US;
  static constexpr uint32_t START_DELAY_TICKS = 50 * TICKS_PER_US;
  static constexpr uint32_t LEASE_TICKS = 10000 * TICKS_PER_US;
  static constexpr uint32_t MAX_ANCHORED_DELAY_TICKS = 10 * TICKS_PER_US;

  static bool onAlarm(gptimer_handle_t, const gptimer_alarm_event_data_t* event,
                      void* context) {
    auto& output = *static_cast<StepPulseTimer*>(context);
    portENTER_CRITICAL_ISR(&output.mux_);
    output.handleAlarm(event->count_value, event->alarm_value);
    portEXIT_CRITICAL_ISR(&output.mux_);
    return false;
  }

  void handleAlarm(uint64_t now, uint64_t scheduledAt) {
    armed_ = false;
    if (high_) {
      gpio_ll_set_level(&GPIO, pin_, 0);
      high_ = false;
      if (periodTicks_ != 0 && remaining_ != 0 && !fault_) {
        // Keep the nominal cadence while enforcing the minimum low interval.
        const uint64_t next = riseAt_ + periodTicks_;
        const uint64_t earliest = now + MIN_LOW_TICKS;
        armAt(next > earliest ? next : earliest);
      }
      return;
    }
    if (periodTicks_ == 0 || remaining_ == 0 || fault_) return;
    const uint8_t active =
        (negativePin_ >= 0 && !gpio_ll_get_level(&GPIO, negativePin_) ? 1 : 0) |
        (positivePin_ >= 0 && !gpio_ll_get_level(&GPIO, positivePin_) ? 2 : 0);
    if (active == 3 || (active & (reverse_ ? 1 : 2)) || (active & ~allowedEnd_)) {
      // Inspect the physical input before every STEP rising edge, even if
      // USB/control processing is delayed. Finish an existing high normally.
      limitHits_ |= active;
      periodTicks_ = 0;
      return;
    }
    if (now >= leaseUntil_) {
      fail();
      return;
    }
    if (inputPin_ >= 0 && bool(gpio_ll_get_level(&GPIO, inputPin_)) == inputLevel_) {
      inputHit_ = true;
      periodTicks_ = 0;
      return;
    }
    // Avoid adding ISR dispatch time to every period. If an interrupt is
    // significantly late, rebase the schedule instead of catching up.
    riseAt_ = now - scheduledAt <= MAX_ANCHORED_DELAY_TICKS ? scheduledAt : now;
    ++total_;
    position_ += reverse_ ? -1 : 1;
    gpio_ll_set_level(&GPIO, pin_, 1);
    high_ = true;
    if (remaining_ != UINT32_MAX) --remaining_;
    armAt(now + PULSE_HIGH_TICKS);
  }

  void armAt(uint64_t time) {
    gptimer_alarm_config_t alarm{};
    alarm.alarm_count = time;
    if (gptimer_set_alarm_action(timer_, &alarm) == ESP_OK) {
      armed_ = true;
    } else {
      fail();
    }
  }

  void fail() {
    periodTicks_ = 0;
    fault_ = true;
    gpio_ll_set_level(&GPIO, pin_, 0);
    high_ = false;
  }

  const uint32_t pin_;
  const int negativePin_, positivePin_;
  gptimer_handle_t timer_ = nullptr;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  uint64_t leaseUntil_ = 0, riseAt_ = 0;
  uint32_t periodTicks_ = 0, remaining_ = 0, total_ = 0;
  bool armed_ = false, high_ = false, fault_ = false;
  bool reverse_ = false;
  uint8_t limitHits_ = 0;
  int inputPin_ = -1;
  bool inputLevel_ = false, inputHit_ = false;
  uint8_t allowedEnd_ = 3;
  int64_t position_ = 0, originPosition_ = 0;
  uint32_t originSequence_ = 0, originAt_ = 0;
  bool originArmed_ = true;
};
