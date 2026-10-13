// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "fsm/evse/machine.hpp"
#include "fsm/fsm_helpers.hpp"

#include <everest/slac/slac_fsm.hpp>
#include <everest_api_types/telemetry/codec.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace everest::lib::slac {

namespace {
namespace api_telemetry = everest::lib::API::V1_0::types::telemetry;

struct PrintVisitor : sm::Visitor {
    explicit PrintVisitor(std::string& output_) : output(output_) {
    }
    std::string& output;

    void on_active_state(sm::Node const& state, std::size_t depth) override {
        std::string indent(depth * 4, ' ');
        output += indent + " -> " + state.name() + "\n";
    }

    void on_dynamic_session_start(int index, std::size_t depth) override {
        std::string indent(depth * 4, ' ');
        output += indent + " * [Dynamic Session " + std::to_string(index) + "]\n";
    }
};

struct FsmStateVisitor : sm::Visitor {
    std::vector<api_telemetry::SlacFsmState> stack;
    std::string last_visited_state;

    FsmStateVisitor() {
        stack.emplace_back();
    }

    void on_active_state(sm::Node const& state, std::size_t /*depth*/) override {
        std::string name = state.name();
        stack.back().states.push_back(name);
        last_visited_state = std::move(name);
    }

    void on_submachine_start(std::size_t /*depth*/) override {
        stack.emplace_back();
    }

    void on_submachine_end(std::size_t /*depth*/) override {
        auto child = std::move(stack.back());
        stack.pop_back();
        stack.back().submachines[last_visited_state] = std::move(child);
    }

    void on_dynamic_session_start(int /*index*/, std::size_t /*depth*/) override {
        stack.emplace_back();
    }

    void on_dynamic_session_end(int /*index*/, std::size_t /*depth*/) override {
        auto child = std::move(stack.back());
        stack.pop_back();
        stack.back().sessions.push_back(std::move(child));
    }

    api_telemetry::SlacFsmState const& get_result() const {
        return stack.front();
    }
};

} // namespace

struct slac_fsm::Impl {
    sm::evse::SlacMachine fsm;
    bool started{false};
    explicit Impl(fsm::evse::Context& ctx) : fsm(ctx) {
    }
};

void slac_fsm::event_post_processing() {
    // Publish the public SLAC state after every processed event. The state machine keeps
    // ctx.status.d3_state current in each on_entry; publish_slac_state() deduplicates, so this
    // emits exactly one signal per logical transition regardless of telemetry configuration.
    ctx.publish_slac_state();

    auto print = ctx.slac_config.print_state_transitions;
    auto telemetry = ctx.slac_config.provide_telemetry;

    if (print or telemetry) {
        auto const& machine = impl->fsm;
        if (sm::update_signature(machine, last_signature)) {
            if (print) {
                std::string current_state_str;
                PrintVisitor print_visitor{current_state_str};
                machine.traverse(print_visitor);
                ctx.log_info("SLAC FSM state:\n" + current_state_str);
            }
            if (telemetry) {
                FsmStateVisitor fsm_state_visitor;
                machine.traverse(fsm_state_visitor);
                ctx.telemetry("FSM", "state", api_telemetry::serialize(fsm_state_visitor.get_result()));

                ctx.telemetry("generic", "status", serialize(ctx.status));
            }
        }
    }
}

slac_fsm::slac_fsm(fsm::evse::Context& ctx) : impl(std::make_unique<Impl>(ctx)), ctx(ctx) {
}

slac_fsm::~slac_fsm() {
}

// Every event starts here: the time is sampled once for all its deadlines, and a callback failure
// parked outside an event (the controllers log through the context between events) surfaces before
// this event runs instead of being blamed on it afterwards. Each event then checks that the machine
// was started: a failure surfacing from restart_fsm() before start() leaves a machine whose
// sub-machines were never entered, and feeding it events would settle them without a context.
void slac_fsm::begin_event() {
    ctx.sample_time();
    sm::rethrow_recorded(ctx);
}

void slac_fsm::reset() {
    begin_event();
    if (not impl->started) {
        return; // never started: its sub-machines were never entered and hold no context
    }
    impl->fsm.process(sm::Event{sm::EventId::reset});
    sm::rethrow_recorded(ctx);
    settle();
}

void slac_fsm::enter_bcd() {
    begin_event();
    if (not impl->started) {
        return; // never started: its sub-machines were never entered and hold no context
    }
    impl->fsm.process(sm::Event{sm::EventId::enter_bcd});
    sm::rethrow_recorded(ctx);
    settle();
}

void slac_fsm::leave_bcd() {
    begin_event();
    if (not impl->started) {
        return; // never started: its sub-machines were never entered and hold no context
    }
    impl->fsm.process(sm::Event{sm::EventId::leave_bcd});
    sm::rethrow_recorded(ctx);
    settle();
}

void slac_fsm::message(messages::HomeplugMessage msg) {
    begin_event();
    if (not impl->started) {
        return; // never started: its sub-machines were never entered and hold no context
    }
    impl->fsm.process(sm::Event{sm::EventId::message, &msg});
    sm::rethrow_recorded(ctx);
    settle();
}

void slac_fsm::update() {
    begin_event();
    if (not impl->started) {
        return; // never started: its sub-machines were never entered and hold no context
    }
    settle();
}

void slac_fsm::restart_fsm() {
    begin_event();
    if (impl->started) {
        impl->fsm.stop();
    }
    impl->started = true;
    impl->fsm.start();
    sm::rethrow_recorded(ctx);
    settle();
}

void slac_fsm::settle() {
    sm::settle(impl->fsm);
    event_post_processing();
    // The publishers above park a throw like the ones inside the machine; surface it from this event.
    sm::rethrow_recorded(ctx);
}

std::optional<timer::tick> slac_fsm::next_wakeup() const {
    return sm::next_wakeup_of(impl->fsm, ctx.current_time);
}

} // namespace everest::lib::slac
