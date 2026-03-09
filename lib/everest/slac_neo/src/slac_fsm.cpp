// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/slac/msm/slac_evse_fsm.hpp>
#include <everest/slac/slac_fsm.hpp>

namespace everest::lib::slac {

namespace {
msm::SlacFSM* to_msm(void* ptr) {
    return reinterpret_cast<msm::SlacFSM*>(ptr);
}

} // namespace

slac_fsm::slac_fsm(fsm::evse::Context& ctx) {
    auto ptr = new msm::SlacFSM(ctx);
    fsm = ptr;
}

slac_fsm::~slac_fsm() {
    auto ptr = to_msm(fsm);
    delete ptr;
}

void slac_fsm::reset() {
    auto ptr = to_msm(fsm);
    ptr->process_event(msm::reset{});
}

void slac_fsm::enter_bcd() {
    auto ptr = to_msm(fsm);
    ptr->process_event(msm::enter_bcd{});
}

void slac_fsm::leave_bcd() {
    auto ptr = to_msm(fsm);
    ptr->process_event(msm::leave_bcd{});
}

void slac_fsm::message(messages::HomeplugMessage msg) {
    msm::message event;
    event.payload = std::move(msg);
    auto ptr = to_msm(fsm);
    ptr->process_event(event);
}

void slac_fsm::update() {
    auto ptr = to_msm(fsm);
    ptr->process_event(msm::update{});
}

void slac_fsm::restart_fsm() {
    auto ptr = to_msm(fsm);
    ptr->start();
}

struct state_logger {
    template <typename T> void operator()(T const& s) const {
        std::cout << " -> Active: " << boost::core::demangle(typeid(s).name()) << std::endl;
    }
};

} // namespace everest::lib::slac
