// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../main/Events.hpp"

#include <chrono>
#include <vector>

namespace module::test {

// Records both state_timer (arm/cancel) AND tick_fd (arm_tick/disarm_tick)
// callbacks issued by the FSM. Test fixtures hand `arm` / `cancel` /
// `arm_tick` / `disarm_tick` as `std::function` wrappers around the member
// methods to FsmContext's constructor.
//
// advance_state_timer() and advance_tick() are deferred to test-fixture
// code that owns both the TimerSink and a FsmContext reference; they need
// SocIntegrator wiring that's cleaner to construct at the call site.
class TimerSink {
public:
    void arm(std::chrono::milliseconds ms) {
        state_timer_arms.push_back(ms);
    }
    void cancel() {
        state_timer_cancels++;
    }
    void arm_tick(int ms) {
        tick_arms.push_back(ms);
    }
    void disarm_tick() {
        tick_disarms++;
    }
    void arm_scenario(std::chrono::milliseconds ms) {
        scenario_timer_arms.push_back(ms);
    }
    void record_enqueue(Event ev) {
        enqueued_events.push_back(std::move(ev));
    }

    std::vector<std::chrono::milliseconds> state_timer_arms;
    std::vector<int> tick_arms;
    std::vector<std::chrono::milliseconds> scenario_timer_arms;
    std::vector<Event> enqueued_events;
    int state_timer_cancels{0};
    int tick_disarms{0};

    void clear() {
        state_timer_arms.clear();
        tick_arms.clear();
        scenario_timer_arms.clear();
        enqueued_events.clear();
        state_timer_cancels = 0;
        tick_disarms = 0;
    }
};

} // namespace module::test
