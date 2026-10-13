// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Top-level EVSE SLAC machine: event processing, the exit point queue, and the two top-level
// states that are no sub-machine, Idle and Failed.

#include "machine.hpp"

#include <everest/slac/telemetry.hpp>

#include <cstdio>
#include <string>
#include <utility>

namespace everest::lib::slac::sm::evse {

namespace {

struct Idle : TopLevelState<StateId::Idle> {
    explicit Idle(SlacMachine& m) : TopLevelState(m, "Idle") {
    }
    void enter() override {
        ctx.enter_state(SlacState::Idle, D3State::Unmatched, "Entered Idle state");
        ctx.clear_match_confirm_cache();
        ctx.status.modem_PIB = true;
    }
    bool reacts_to(EventId e) const override {
        return reacts_to_any(e, {EventId::enter_bcd, EventId::reset});
    }
    Result feed(Event const& e) override {
        if (e.is(EventId::enter_bcd)) {
            return transition(make_matching(machine));
        }
        if (e.is(EventId::reset)) {
            return transition(make_reset(machine));
        }
        return unhandled();
    }
};

struct Failed : TopLevelState<StateId::Failed> {
    explicit Failed(SlacMachine& m) : TopLevelState(m, "Failed") {
    }
    void enter() override {
        ctx.enter_state(SlacState::Failed, D3State::Unmatched, "Entered Failed state");
        if (ctx.slac_config.ac_mode_five_percent) {
            ctx.signal_error_routine_request();
        }
        ctx.clear_match_confirm_cache();
    }
    bool reacts_to(EventId e) const override {
        return reacts_to_any(e, {EventId::reset, EventId::leave_bcd});
    }
    Result feed(Event const& e) override {
        if (e.is(EventId::reset) or e.is(EventId::leave_bcd)) {
            return transition(make_reset(machine));
        }
        return unhandled();
    }
};

} // namespace

TopStatePtr make_idle(SlacMachine& machine) {
    return std::make_unique<Idle>(machine);
}

TopStatePtr make_failed(SlacMachine& machine) {
    return std::make_unique<Failed>(machine);
}

SlacMachine::SlacMachine(fsm::evse::Context& ctx_) : ctx(ctx_) {
}

SlacMachine::~SlacMachine() {
    destroying = true;
}

void SlacMachine::start() {
    top.start(make_init(*this));
    top.complete();
    while (not queue.empty()) {
        auto queued = std::move(queue.front());
        queue.pop_front();
        process_one(queued.view());
    }
}

void SlacMachine::stop() {
    top.stop();
}

void SlacMachine::forward(QueuedEvent e) {
    queue.push_back(std::move(e));
}

void SlacMachine::process(Event const& e) {
    process_one(e);
    // Exit points reached meanwhile queued their event; it is processed now, in whatever state
    // the machine is in by then.
    while (not queue.empty()) {
        auto queued = std::move(queue.front());
        queue.pop_front();
        process_one(queued.view());
    }
}

void SlacMachine::process_one(Event const& e) {
    auto const reacts = top.current().reacts_to(e.id);
    if (top.feed(e)) {
        top.complete();
        return;
    }
    if (not reacts) {
        log_no_transition(e);
    }
}

// An event no row handles in the current state is dropped. Frames and commands that arrive in the
// wrong state are worth a debug line; an idle update or a completion is not.
void SlacMachine::log_no_transition(Event const& e) {
    auto const state = std::string(top.current().name());
    switch (e.id) {
    case EventId::message: {
        char mmtype[8];
        std::snprintf(mmtype, sizeof(mmtype), "%x", static_cast<unsigned>(e.payload->get_mmtype()));
        ctx.log_debug(std::string("Ignoring SLAC message 0x") + mmtype + " in state " + state);
        break;
    }
    case EventId::reset:
        ctx.log_debug("Ignoring reset in state " + state);
        break;
    case EventId::enter_bcd:
        ctx.log_debug("Ignoring enter_bcd in state " + state);
        break;
    case EventId::leave_bcd:
        ctx.log_debug("Ignoring leave_bcd in state " + state);
        break;
    default:
        break;
    }
}

void SlacMachine::traverse(Visitor& visitor) const {
    if (top.active()) {
        top.traverse(visitor, 0);
    }
}

} // namespace everest::lib::slac::sm::evse
