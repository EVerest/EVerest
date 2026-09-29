// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "fsm/ev/msm/machine.hpp"
#include "fsm/msm_traverse.hpp"
#include <everest/slac/ev_slac_fsm.hpp>

#include <utility>

namespace everest::lib::slac {

struct ev_slac_fsm::Impl {
    msm::ev::SlacEVFSM fsm;
    explicit Impl(fsm::ev::Context& ctx) : fsm(ctx) {
    }
};

ev_slac_fsm::ev_slac_fsm(fsm::ev::Context& ctx) : impl(std::make_unique<Impl>(ctx)), ctx(ctx) {
}

ev_slac_fsm::~ev_slac_fsm() {
}

void ev_slac_fsm::reset() {
    ctx.sample_time();
    impl->fsm.process_event(msm::reset{});
    msm::settle(impl->fsm);
}

void ev_slac_fsm::trigger_matching() {
    ctx.sample_time();
    impl->fsm.process_event(msm::ev::trigger_matching{});
    msm::settle(impl->fsm);
}

void ev_slac_fsm::message(messages::HomeplugMessage msg) {
    msm::message event;
    event.payload = std::move(msg);
    ctx.sample_time();
    impl->fsm.process_event(event);
    msm::settle(impl->fsm);
}

void ev_slac_fsm::update() {
    ctx.sample_time();
    msm::settle(impl->fsm);
}

void ev_slac_fsm::restart_fsm() {
    ctx.sample_time();
    impl->fsm.start();
    msm::settle(impl->fsm);
}

std::optional<timer::tick> ev_slac_fsm::next_wakeup() const {
    return msm::next_wakeup_of(impl->fsm, ctx.current_time);
}

} // namespace everest::lib::slac
