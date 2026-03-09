// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#include "fsm_controller.hpp"

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/slac/fsm/evse/states/others.hpp>
#include <everest/util/misc/bind.hpp>

FSMController::FSMController(slac::fsm::evse::Context& context) : ctx(context), fsm(ctx) {
    m_retrigger.set_timeout_ms(10);
};

void FSMController::init() {
    ctx.log_info("Starting the SLAC state machine");
    fsm.restart_fsm();
}

void FSMController::signal_new_slac_message(slac::messages::HomeplugMessage const& msg) {
    ctx.slac_message_payload = msg;
    fsm.message(msg);
}

void FSMController::signal_reset() {
    m_reset.notify();
}

bool FSMController::signal_enter_bcd() {
    m_enter_bcd.notify();
    return true;
}

bool FSMController::signal_leave_bcd() {
    m_leave_bcd.notify();
    return true;
}

void FSMController::handle_retrigger() {
    fsm.update();
}

void FSMController::handle_reset() {
    ctx.log_info("Signal reset");
    fsm.reset();
}

void FSMController::handle_enter_bcd() {
    ctx.log_info("Signal enter_bcd");
    fsm.enter_bcd();
}

void FSMController::handle_leave_bcd() {
    ctx.log_info("Signal leave_bcd");
    fsm.leave_bcd();
}

bool FSMController::register_events(everest::lib::io::event::fd_event_handler& handler) {
    using everest::lib::util::bind_obj;
    using T = FSMController;
    auto result = true;
    result &= handler.register_event_handler(&m_reset, bind_obj(&T::handle_reset, this));
    result &= handler.register_event_handler(&m_enter_bcd, bind_obj(&T::handle_enter_bcd, this));
    result &= handler.register_event_handler(&m_leave_bcd, bind_obj(&T::handle_leave_bcd, this));
    result &= handler.register_event_handler(&m_retrigger, bind_obj(&T::handle_retrigger, this));
    return result;
}

bool FSMController::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result &= handler.unregister_event_handler(&m_reset);
    result &= handler.unregister_event_handler(&m_enter_bcd);
    result &= handler.unregister_event_handler(&m_leave_bcd);
    result &= handler.unregister_event_handler(&m_retrigger);
    return result;
}

void FSMController::run() {
}
