#include <cassert>
#include <cstdio>
#include "../xiao_pulse_trial/JogTrial.h"
using namespace pulse_trial;
const Keys clear{}, forward{true, false, false}, reverse{false, true, false};
const Keys stop{false, false, true}, both{true, true, false};

// Feed synthetic STEP events into the policy. The real output is timer driven.
class SimulatedJog : public JogTrial {
 public:
  using JogTrial::JogTrial;

  void reset(uint32_t now) {
    JogTrial::reset(now);
    hasPulse_ = false;
    due_ = false;
  }

  void tick(Keys stable, Keys raw, uint32_t now) {
    JogTrial::tick(stable, raw, now);
    if (!moving()) {
      hasPulse_ = false;
      due_ = false;
      return;
    }
    const uint32_t hz = targetHz(now);
    const uint32_t periodMs = (1000 + hz - 1) / hz;
    due_ = !hasPulse_ || now - lastPulseAt_ >= periodMs;
  }

  void pulseQueued(uint32_t now) {
    if (!pulseDue()) return;
    hasPulse_ = true;
    due_ = false;
    lastPulseAt_ = now;
    recordPulses(1, now);
  }

  bool pulseDue() const { return moving() && due_; }

 private:
  bool hasPulse_ = false, due_ = false;
  uint32_t lastPulseAt_ = 0;
};

void tick(SimulatedJog& trial, Keys keys, uint32_t now) { trial.tick(keys, keys, now); }
void arm(SimulatedJog& trial, uint32_t base = 0) {
  trial.reset(base);
  tick(trial, clear, base + 2000);
  tick(trial, stop, base + 2001);
  tick(trial, stop, base + 3001);
  assert(trial.state() == State::ArmRelease && !trial.enabled());
  tick(trial, clear, base + 3002);
  assert(trial.state() == State::Ready && trial.enabled());
}
void beginForward(SimulatedJog& trial, uint32_t now = 3100) {
  tick(trial, forward, now);
  assert(trial.state() == State::Settling && !trial.pulseDue());
  tick(trial, forward, now + 99);
  assert(!trial.pulseDue());
  tick(trial, forward, now + 100);
  assert(trial.pulseDue());
}

int main() {
  {
    SimulatedJog t;
    for (uint32_t ms = 0; ms < 5000; ms += 10) {
      tick(t, forward, ms);
      assert(!t.pulseDue() && !t.enabled());
    }
    tick(t, clear, 5000);
    assert(t.state() == State::Disarmed);
    tick(t, forward, 5001);
    assert(!t.pulseDue() && t.state() == State::Disarmed);
  }
  {
    SimulatedJog t; arm(t); beginForward(t);
    for (uint32_t n = 0; n < 20; ++n) {
      const uint32_t now = 3200 + n * 50;
      tick(t, forward, now);
      assert(t.pulseDue());
      t.pulseQueued(now);
    }
    assert(t.count() == 20 && t.state() == State::Release);
    tick(t, forward, 9000);
    assert(!t.pulseDue() && t.total() == 20);
    tick(t, clear, 9001);
    assert(t.state() == State::Ready);
  }
  {
    SimulatedJog t; arm(t); beginForward(t); t.pulseQueued(3200);
    // The debounced key still looks down; raw release must win immediately.
    t.tick(forward, clear, 3250);
    assert(!t.pulseDue() && t.state() == State::Release && t.count() == 1);
    tick(t, forward, 3300); assert(!t.pulseDue());
    tick(t, clear, 3310); tick(t, reverse, 3320);
    tick(t, reverse, 3749); assert(!t.pulseDue());
    tick(t, reverse, 3750); assert(t.pulseDue() && t.reverse());
  }
  {
    SimulatedJog t; arm(t); beginForward(t);
    t.tick(forward, both, 3200);
    assert(!t.pulseDue() && t.state() == State::Release);
    tick(t, reverse, 3300); assert(!t.pulseDue());
    tick(t, clear, 3400); assert(t.state() == State::Ready);
  }
  {
    SimulatedJog t; arm(t); beginForward(t);
    t.tick(forward, stop, 3200);
    assert(!t.enabled() && !t.pulseDue());
    tick(t, stop, 10000); assert(!t.enabled());  // Held STOP cannot re-arm.
    tick(t, clear, 10001); assert(t.state() == State::Disarmed);
  }
  {
    SimulatedJog t; arm(t); beginForward(t); t.pulseQueued(3200);
    tick(t, forward, 4000); assert(t.pulseDue());
    t.pulseQueued(4000);
    tick(t, forward, 4001); assert(!t.pulseDue()); // No catch-up burst.
    tick(t, forward, 4200); assert(!t.pulseDue()); // One-second timeout.
    assert(t.state() == State::Release && t.count() == 2);
  }
  {
    SimulatedJog t; arm(t); beginForward(t);
    t.fail(3200); tick(t, forward, 9999);
    assert(t.state() == State::Fault && !t.pulseDue() && !t.enabled());
    tick(t, clear, 10000); tick(t, stop, 11000); tick(t, stop, 12000);
    assert(t.state() == State::Fault);
  }
  {
    SimulatedJog t;
    const uint32_t base = UINT32_MAX - 2500;
    arm(t, base); beginForward(t, base + 3100);
    t.pulseQueued(base + 3200);
    tick(t, forward, base + 3250); assert(t.pulseDue());
    t.reset(0); assert(!t.enabled() && t.total() == 0);
  }
  {
    SimulatedJog t{TrialConfig{20, 100, 500, 5000, 400}};
    arm(t); beginForward(t);
    assert(t.targetHz(3200) == 20);
    assert(t.targetHz(3450) == 60);
    assert(t.targetHz(3700) == 100);
    uint32_t last = 0, sent = 0;
    for (uint32_t now = 3200; now < 9000; ++now) {
      tick(t, forward, now);
      if (t.pulseDue()) {
        if (sent) assert(now - last >= 10);  // At most 100 Hz, no bursts.
        last = now;
        t.pulseQueued(now);
        ++sent;
      }
    }
    assert(sent == 400 && t.count() == 400 && t.state() == State::Release);
    tick(t, clear, 9000);
    tick(t, forward, 9001); tick(t, forward, 9101);
    assert(t.targetHz(9101) == 20); // Every hold starts slowly again.
    t.pulseQueued(9101);
    t.tick(forward, clear, 9102);
    assert(!t.pulseDue() && t.state() == State::Release);
  }
  {
    SimulatedJog t{TrialConfig{20, 100, 500, 5000, 400}};
    arm(t); beginForward(t); t.pulseQueued(3200);
    // Sparse scheduling cannot extend a hold forever or catch up missed pulses.
    for (uint32_t now = 4200; now < 8200; now += 1000) {
      tick(t, forward, now); assert(t.pulseDue()); t.pulseQueued(now);
    }
    tick(t, forward, 8200);
    assert(!t.pulseDue() && t.state() == State::Release && t.count() == 5);
  }
  {
    SimulatedJog t{TrialConfig{20, 100, 500, 5000, 400}};
    const uint32_t base = UINT32_MAX - 3300;
    arm(t, base); beginForward(t, base + 3100);
    assert(t.targetHz(base + 3450) == 60);
    assert(t.targetHz(base + 3700) == 100);
    t.reset(0); arm(t); beginForward(t);
    assert(t.targetHz(3700) == 100); // Reset preserves the chosen profile.
  }
  {
    SimulatedJog bad{TrialConfig{0, 100, 500, 5000, 400}};
    bad.reset(0);
    tick(bad, clear, 2000); tick(bad, stop, 2001); tick(bad, stop, 4000);
    tick(bad, clear, 4001);
    assert(bad.state() == State::Fault && !bad.enabled() && !bad.pulseDue());
  }
  {
    SimulatedJog t{TrialConfig{20, 100, 500, 0, 0, RunMode::HoldToRun}};
    arm(t); beginForward(t);
    uint32_t last = 0, sent = 0;
    for (uint32_t now = 3200; now < 23200; ++now) {
      tick(t, forward, now);
      assert(t.state() == State::Forward);
      if (t.pulseDue()) {
        if (sent) assert(now - last >= 10);
        last = now;
        t.pulseQueued(now);
        ++sent;
      }
    }
    assert(sent > 1500 && t.count() == sent && t.targetHz(23200) == 100);
    t.tick(forward, clear, 23200);
    assert(!t.pulseDue() && t.state() == State::Release);
    tick(t, clear, 23201); tick(t, reverse, 23202);
    tick(t, reverse, 23699); assert(!t.pulseDue());
    tick(t, reverse, 23700);
    assert(t.pulseDue() && t.reverse() && t.targetHz(23700) == 20);
    t.pulseQueued(23700);
    t.tick(reverse, stop, 23701);
    assert(!t.enabled() && !t.pulseDue());
    tick(t, stop, 30000); assert(!t.enabled());
  }
  {
    SimulatedJog t{TrialConfig{20, 100, 500, 0, 0, RunMode::HoldToRun}};
    arm(t); beginForward(t); t.pulseQueued(3200);
    t.tick(forward, both, 3250);
    assert(t.state() == State::Release && !t.pulseDue());
    tick(t, forward, 4000); assert(!t.pulseDue());
    tick(t, clear, 4001); assert(t.state() == State::Ready);
    t.reset(0);
    for (uint32_t now = 0; now < 10000; now += 100) {
      tick(t, forward, now); assert(!t.enabled() && !t.pulseDue());
    }
    tick(t, clear, 10000); assert(t.state() == State::Disarmed);
  }
  {
    SimulatedJog t{TrialConfig{20, 100, 500, 0, 0, RunMode::HoldToRun}};
    arm(t); beginForward(t); t.pulseQueued(3200);
    tick(t, forward, 3700); t.pulseQueued(3700);
    // A very long hold crossing millis() wrap must not restart the ramp.
    tick(t, forward, 3199); t.pulseQueued(3199);
    tick(t, forward, 3200);
    assert(t.state() == State::Forward && t.targetHz(3200) == 100 && !t.pulseDue());
    t.fail(3201); tick(t, forward, 4000);
    assert(t.state() == State::Fault && !t.enabled() && !t.pulseDue());
  }
  {
    SimulatedJog mixed{TrialConfig{20, 100, 500, 5000, 400, RunMode::HoldToRun}};
    assert(mixed.state() == State::Fault); // Reject contradictory limit policy.
    SimulatedJog missingLimits{TrialConfig{20, 100, 500, 0, 0}};
    assert(missingLimits.state() == State::Fault); // No accidental unlimited mode.
  }
  {
    SimulatedJog t{TrialConfig{20, 500, 1000, 0, 0, RunMode::HoldToRun}};
    arm(t); beginForward(t);
    assert(t.targetHz(3200) == 20);
    assert(t.targetHz(3700) == 260);
    assert(t.targetHz(4200) == 500);
    uint32_t sent = 0, last = 0;
    for (uint32_t now = 3200; now < 13200; ++now) {
      tick(t, forward, now);
      assert(t.state() == State::Forward && t.targetHz(now) <= 500);
      if (t.pulseDue()) {
        if (sent) assert(now - last >= 2);
        last = now;
        t.pulseQueued(now);
        ++sent;
      }
    }
    assert(sent > 4000 && sent <= 5000 && t.count() == sent);
    t.tick(forward, clear, 13200);
    assert(t.state() == State::Release && !t.pulseDue());
    tick(t, clear, 13201); tick(t, reverse, 13202);
    tick(t, reverse, 13699); assert(!t.pulseDue());
    tick(t, reverse, 13700);
    assert(t.pulseDue() && t.reverse() && t.targetHz(13700) == 20);
    t.pulseQueued(13700);
    t.tick(reverse, both, 13701);
    assert(t.state() == State::Release && !t.pulseDue());
    tick(t, reverse, 14000); assert(!t.pulseDue());
    tick(t, clear, 14001); tick(t, forward, 14002);
    t.tick(forward, stop, 14003);
    assert(!t.enabled() && !t.pulseDue());
  }
  {
    SimulatedJog t{TrialConfig{20, 1000, 2000, 0, 0, RunMode::HoldToRun}};
    arm(t); beginForward(t);
    assert(t.targetHz(3200) == 20);
    assert(t.targetHz(4200) == 510);
    assert(t.targetHz(5200) == 1000);
    uint32_t sent = 0, last = 0;
    for (uint32_t now = 3200; now < 13200; ++now) {
      tick(t, forward, now);
      assert(t.state() == State::Forward && t.targetHz(now) <= 1000);
      if (t.pulseDue()) {
        if (sent) assert(now - last >= 1);
        last = now;
        t.pulseQueued(now);
        ++sent;
      }
    }
    assert(sent > 8000 && sent <= 10000 && t.count() == sent);
    t.tick(forward, clear, 13200);
    assert(t.state() == State::Release && !t.pulseDue());
    tick(t, clear, 13201); tick(t, reverse, 13202);
    tick(t, reverse, 13699); assert(!t.pulseDue());
    tick(t, reverse, 13700);
    assert(t.pulseDue() && t.reverse() && t.targetHz(13700) == 20);
    t.pulseQueued(13700);
    tick(t, reverse, 15700); assert(t.targetHz(15700) == 1000);
    t.tick(reverse, stop, 15701);
    assert(!t.enabled() && !t.pulseDue());
  }
  {
    SimulatedJog t{TrialConfig{20, 6827, 2000, 0, 0, RunMode::HoldToRun}};
    arm(t); beginForward(t);
    assert(t.targetHz(3200) == 20);
    tick(t, forward, 5199);
    assert(t.targetHz(5199) < 6827);
    tick(t, forward, 5200);
    assert(t.targetHz(5200) == 6827);
    t.recordPulses(7, 5201);  // A control tick can observe several timer pulses.
    assert(t.count() == 7 && t.total() == 7);
    t.tick(forward, clear, 5202);
    assert(t.state() == State::Release && t.targetHz(5202) == 0);
  }
  std::puts("PASS: boot/arm/stop/reversal/fault/rollover guards, baseline caps, "
            "slow ramp, bounded caps, 20-second hold mode, immediate release/STOP, "
            "held-key boot lock, mode validation, 500Hz/1s, 1000Hz/2s and 6827Hz/2s ramp/stop policy");
}
