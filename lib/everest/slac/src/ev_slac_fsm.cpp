// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "fsm/ev/machine.hpp"
#include "fsm/fsm_helpers.hpp"
#include <everest/slac/ev_slac_fsm.hpp>

#include <utility>

namespace everest::lib::slac {

struct ev_slac_fsm::Impl {
    sm::ev::SlacEvMachine fsm;
    bool started{false};
    explicit Impl(fsm::ev::Context& ctx) : fsm(ctx) {
    }
};

ev_slac_fsm::ev_slac_fsm(fsm::ev::Context& ctx) : impl(std::make_unique<Impl>(ctx)), ctx(ctx) {
}

ev_slac_fsm::~ev_slac_fsm() {
}

// Every event starts here: the time is sampled once for all its deadlines, and a callback failure
// parked outside an event surfaces before this event runs instead of being blamed on it afterwards.
// Each event then checks that the machine was started, as the EVSE wrapper does; a failure
// surfacing from restart_fsm() before start() must not be followed by events on a machine that was
// never entered.
void ev_slac_fsm::begin_event() {
    ctx.sample_time();
    sm::rethrow_recorded(ctx);
}

void ev_slac_fsm::reset() {
    begin_event();
    if (not impl->started) {
        return; // never started: its sub-machines were never entered and hold no context
    }
    impl->fsm.process(sm::Event{sm::EventId::reset});
    sm::rethrow_recorded(ctx);
    sm::settle(impl->fsm);
}

void ev_slac_fsm::trigger_matching() {
    begin_event();
    if (not impl->started) {
        return; // never started: its sub-machines were never entered and hold no context
    }
    impl->fsm.process(sm::Event{sm::EventId::trigger_matching});
    sm::rethrow_recorded(ctx);
    sm::settle(impl->fsm);
}

void ev_slac_fsm::message(messages::HomeplugMessage msg) {
    begin_event();
    if (not impl->started) {
        return; // never started: its sub-machines were never entered and hold no context
    }
    impl->fsm.process(sm::Event{sm::EventId::message, &msg});
    sm::rethrow_recorded(ctx);
    sm::settle(impl->fsm);
}

void ev_slac_fsm::update() {
    begin_event();
    if (not impl->started) {
        return; // never started: its sub-machines were never entered and hold no context
    }
    sm::settle(impl->fsm);
}

void ev_slac_fsm::restart_fsm() {
    begin_event();
    impl->started = true;
    impl->fsm.start();
    sm::rethrow_recorded(ctx);
    sm::settle(impl->fsm);
}

std::optional<timer::tick> ev_slac_fsm::next_wakeup() const {
    return sm::next_wakeup_of(impl->fsm, ctx.current_time);
}

} // namespace everest::lib::slac
