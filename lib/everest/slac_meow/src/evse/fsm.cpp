// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/fsm.hpp>

#include <iostream>
#include <optional>
#include <string>

#include <everest/util/fsm/fsm.hpp>

#include <everest/slac/evse/event.hpp>
#include <everest/slac/evse/state/init.hpp>
#include <everest/slac/evse/states.hpp>
#include <everest/slac/status.hpp>
#include <everest_api_types/telemetry/codec.hpp>

namespace everest::slac::evse {

namespace {

namespace api_telemetry = everest::lib::API::V1_0::types::telemetry;

api_telemetry::SlacFsmState to_api_state(StateTree const& tree) {
    api_telemetry::SlacFsmState out;
    out.states.push_back(tree.name);
    for (auto const& child : tree.children) {
        out.submachines[tree.name] = to_api_state(child);
    }
    for (auto const& session : tree.sessions) {
        out.sessions.push_back(to_api_state(session));
    }
    return out;
}

void print_tree(StateTree const& tree, std::size_t depth, std::string& out) {
    out += std::string(depth * 4, ' ');
    out += " -> ";
    out += tree.name;
    out += '\n';
    for (auto const& child : tree.children) {
        print_tree(child, depth + 1, out);
    }
    std::size_t index = 0;
    for (auto const& session : tree.sessions) {
        out += std::string(depth * 4, ' ');
        out += " * [Dynamic Session " + std::to_string(index++) + "]\n";
        print_tree(session, depth + 1, out);
    }
}

} // namespace

struct FSM::Impl {
    explicit Impl(Context& context) : ctx(context) {
    }

    /// Stamp the instant this event happened, then hand it to the machine. Every public entry
    /// point goes through here, so nothing is ever handled without a current_time.
    void feed(SlacEvent const& event) {
        ctx.current_time = ctx.now();
        if (machine.has_value()) {
            machine->feed(event);
        }
        post_process();
    }

    void post_process() {
        ctx.publish_slac_state();

        if (not ctx.slac_config.print_state_transitions and not ctx.slac_config.provide_telemetry) {
            return;
        }
        if (not machine.has_value()) {
            return;
        }

        std::string current_signature;
        machine->get_current_state().signature(current_signature);
        if (current_signature == last_signature) {
            return;
        }
        last_signature = std::move(current_signature);

        StateTree tree;
        machine->get_current_state().describe(tree);

        if (ctx.slac_config.print_state_transitions) {
            std::string rendered;
            print_tree(tree, 0, rendered);
            std::cout << rendered;
            std::cout << "-----------------------\n" << std::endl;
        }
        if (ctx.slac_config.provide_telemetry) {
            ctx.telemetry("FSM", "state", api_telemetry::serialize(to_api_state(tree)));
            ctx.telemetry("generic", "status", serialize(ctx.status));
        }
    }

    Context& ctx;
    std::optional<fsm::v2::FSM<StateBase>> machine{};
    std::string last_signature{};
};

FSM::FSM(Context& ctx) : impl(std::make_unique<Impl>(ctx)) {
}

FSM::~FSM() = default;

void FSM::restart_fsm() {
    impl->ctx.current_time = impl->ctx.now();
    impl->machine.reset();
    impl->last_signature.clear();
    impl->machine.emplace(impl->ctx.create_state<state::Init>());
    impl->post_process();
}

void FSM::reset() {
    impl->feed(event::Reset{});
}

void FSM::enter_bcd() {
    impl->feed(event::EnterBcd{});
}

void FSM::leave_bcd() {
    impl->feed(event::LeaveBcd{});
}

void FSM::message(messages::HomeplugMessage msg) {
    impl->feed(event::Message{msg});
}

void FSM::update() {
    impl->feed(event::Update{});
}

} // namespace everest::slac::evse
