// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/slac/ev_slac_fsm.hpp>
#include <everest/slac/fsm/slac_ev_fsm.hpp>

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
    impl->fsm.process_event(msm::reset{});
}

void ev_slac_fsm::trigger_matching() {
    impl->fsm.process_event(msm::ev::trigger_matching{});
}

void ev_slac_fsm::message(messages::HomeplugMessage msg) {
    msm::message event;
    event.payload = std::move(msg);
    impl->fsm.process_event(event);
}

void ev_slac_fsm::update() {
    impl->fsm.process_event(msm::update{});
}

void ev_slac_fsm::restart_fsm() {
    impl->fsm.start();
}

} // namespace everest::lib::slac
