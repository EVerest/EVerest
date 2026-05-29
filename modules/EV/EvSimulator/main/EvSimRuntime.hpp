// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../EvSimulator.hpp"
#include "Events.hpp"
#include "FsmContext.hpp"
#include "StateBase.hpp"

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/util/fsm/fsm.hpp>
#include <everest/util/queue/thread_safe_queue.hpp>

#include <atomic>
#include <chrono>
#include <memory>

namespace module {

// Runs the single epoll loop that owns the FSM. Four fds drive the loop:
//   - wake_fd: external producers enqueue events and poke wake_fd
//   - state_timer_fd: per-state deadline (e.g. BcbToggling step timer)
//   - tick_fd: periodic SoC integrator while Charging
//   - scenario_timer_fd: scenario-end one-shot, armed by ScenarioDispatcher
class EvSimRuntime {
public:
    explicit EvSimRuntime(EvSimulator& mod);
    ~EvSimRuntime();

    EvSimRuntime(const EvSimRuntime&) = delete;
    EvSimRuntime& operator=(const EvSimRuntime&) = delete;

    void run(std::atomic_bool& online);
    void wake();

    // External MQTT command intake + peer-module event passthrough.
    void register_m2e_subscriptions();
    void register_peer_subscriptions();

    void enqueue(Event ev);

    FsmContext* ctx_ptr() {
        return ctx.get();
    }

    void arm_tick(int interval_ms);
    void disarm_tick();
    void arm_scenario_timer(std::chrono::seconds s);
    void disarm_scenario_timer();

private:
    void on_wake();
    void on_state_timer();
    void on_tick();
    void on_scenario_timer();

    void apply_passthrough_vars(const Event& ev);
    void publish_passthrough_external(const Event& ev);

    PeerActions build_peer_actions();

    EvSimulator& mod;
    std::unique_ptr<FsmContext> ctx;
    std::unique_ptr<fsm::v2::FSM<StateBase>> fsm;

    everest::lib::io::event::fd_event_handler loop;
    everest::lib::io::event::event_fd wake_fd;
    everest::lib::io::event::timer_fd state_timer_fd;
    everest::lib::io::event::timer_fd tick_fd;
    everest::lib::io::event::timer_fd scenario_timer_fd;
    everest::lib::util::thread_safe_queue<Event> queue;
};

} // namespace module
