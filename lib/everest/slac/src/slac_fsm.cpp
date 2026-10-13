// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "fsm/evse/msm/machine.hpp"
#include "fsm/msm_traverse.hpp"

#include <everest/slac/slac_fsm.hpp>
#include <everest_api_types/telemetry/codec.hpp>

#include <boost/core/demangle.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace everest::lib::slac {

namespace {
namespace api_telemetry = everest::lib::API::V1_0::types::telemetry;

std::string simplify_state_name(std::string name) {
    auto sm_prefix = std::string("boost::msm::back::state_machine<");
    auto start_pos = name.find(sm_prefix);

    if (start_pos != std::string::npos) {
        // relevant type is the first template parameter
        start_pos += sm_prefix.length();
        auto end_pos = name.find(',', start_pos);
        if (end_pos != std::string::npos) {
            name = name.substr(start_pos, end_pos - start_pos);
        }
    }

    // strip namespace
    auto last_colon = name.rfind("::");
    if (last_colon != std::string::npos) {
        name = name.substr(last_colon + 2);
    }

    return name;
}

struct PrintVisitor {
    std::string& output;

    template <typename StateType> void on_active_state(StateType const&, int /*state_id*/, size_t depth) {
        std::string indent(depth * 4, ' ');
        std::string clean_name = simplify_state_name(boost::core::demangle(typeid(StateType).name()));
        output += indent + " -> " + clean_name + "\n";
    }

    void on_dynamic_sessions_size(size_t /*size*/) {
        // No action needed for printing
    }

    void on_dynamic_session_start(int index, size_t depth) {
        std::string indent(depth * 4, ' ');
        output += indent + " * [Dynamic Session " + std::to_string(index) + "]\n";
    }
    template <typename StateType> void on_submachine_start(int, size_t) {
    }
    template <typename StateType> void on_submachine_end(int, size_t) {
    }
    void on_dynamic_session_end(int, size_t) {
    }
};

struct FsmStateVisitor {
    std::vector<api_telemetry::SlacFsmState> stack;
    std::string last_visited_state;

    FsmStateVisitor() {
        stack.emplace_back();
    }

    template <typename StateType> void on_active_state(StateType const&, int /*state_id*/, size_t /*depth*/) {
        auto clean_name = simplify_state_name(boost::core::demangle(typeid(StateType).name()));
        stack.back().states.push_back(clean_name);
        last_visited_state = std::move(clean_name);
    }

    template <typename StateType> void on_submachine_start(int /*state_id*/, size_t /*depth*/) {
        stack.emplace_back();
    }

    template <typename StateType> void on_submachine_end(int /*state_id*/, size_t /*depth*/) {
        auto child = std::move(stack.back());
        stack.pop_back();
        stack.back().submachines[last_visited_state] = std::move(child);
    }

    void on_dynamic_sessions_size(size_t /*size*/) {
    }

    void on_dynamic_session_start(int /*index*/, size_t /*depth*/) {
        stack.emplace_back();
    }

    void on_dynamic_session_end(int /*index*/, size_t /*depth*/) {
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
    msm::SlacFSM fsm;
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
        auto const& msm = impl->fsm;
        auto current_signature = msm::signature_of(msm);

        if (current_signature != last_signature) {
            if (print) {
                std::string current_state_str;
                PrintVisitor print_visitor{current_state_str};
                msm::traverse_fsm(msm, 0, print_visitor);
                ctx.log_info("SLAC FSM state:\n" + current_state_str);
            }
            if (telemetry) {
                FsmStateVisitor fsm_state_visitor;
                msm::traverse_fsm(msm, 0, fsm_state_visitor);
                ctx.telemetry("FSM", "state", api_telemetry::serialize(fsm_state_visitor.get_result()));

                ctx.telemetry("generic", "status", serialize(ctx.status));
            }
            last_signature = std::move(current_signature);
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
    msm::rethrow_recorded(impl->fsm);
}

void slac_fsm::reset() {
    begin_event();
    if (not impl->started) {
        return; // never started: its sub-machines were never entered and hold no context
    }
    impl->fsm.process_event(msm::reset{});
    msm::rethrow_recorded(impl->fsm);
    settle();
}

void slac_fsm::enter_bcd() {
    begin_event();
    if (not impl->started) {
        return; // never started: its sub-machines were never entered and hold no context
    }
    impl->fsm.process_event(msm::enter_bcd{});
    msm::rethrow_recorded(impl->fsm);
    settle();
}

void slac_fsm::leave_bcd() {
    begin_event();
    if (not impl->started) {
        return; // never started: its sub-machines were never entered and hold no context
    }
    impl->fsm.process_event(msm::leave_bcd{});
    msm::rethrow_recorded(impl->fsm);
    settle();
}

void slac_fsm::message(messages::HomeplugMessage msg) {
    msm::message event;
    event.payload = std::move(msg);
    begin_event();
    if (not impl->started) {
        return; // never started: its sub-machines were never entered and hold no context
    }
    impl->fsm.process_event(event);
    msm::rethrow_recorded(impl->fsm);
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
    msm::rethrow_recorded(impl->fsm);
    settle();
}

void slac_fsm::settle() {
    msm::settle(impl->fsm);
    event_post_processing();
    // The publishers above park a throw like the ones inside the machine; surface it from this event.
    msm::rethrow_recorded(impl->fsm);
}

std::optional<timer::tick> slac_fsm::next_wakeup() const {
    return msm::next_wakeup_of(impl->fsm, ctx.current_time);
}

} // namespace everest::lib::slac
