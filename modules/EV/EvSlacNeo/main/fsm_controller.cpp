// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include "fsm_controller.hpp"

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/util/misc/bind.hpp>

FSMController::FSMController(slac_fsm::ev::Context& context) : ctx(context), fsm(context) {
}

void FSMController::init() {
    bool was_active{false};
    if (!active.compare_exchange_strong(was_active, true)) {
        return;
    }
    fsm.restart_fsm();
    m_retrigger.set_timeout_ms(10);
}

void FSMController::stop() {
    active.store(false);
    m_retrigger.disarm();
}

void FSMController::signal_new_slac_message(slac::messages::HomeplugMessage const& msg) {
    if (!active.load()) {
        return;
    }
    ctx.slac_message_payload = msg;
    fsm.message(msg);
}

void FSMController::signal_reset() {
    if (!active.load()) {
        return;
    }
    m_reset.notify();
}

bool FSMController::signal_trigger_matching() {
    if (!active.load()) {
        return false;
    }
    m_trigger_matching.notify();
    return true;
}

void FSMController::handle_retrigger() {
    if (!active.load()) {
        return;
    }
    fsm.update();
}

void FSMController::handle_reset() {
    if (!active.load()) {
        return;
    }
    fsm.reset();
}

void FSMController::handle_trigger_matching() {
    if (!active.load()) {
        return;
    }
    fsm.trigger_matching();
}

bool FSMController::register_events(everest::lib::io::event::fd_event_handler& handler) {
    using everest::lib::util::bind_obj;
    using T = FSMController;
    auto result = true;
    result &= handler.register_event_handler(&m_reset, bind_obj(&T::handle_reset, this));
    result &= handler.register_event_handler(&m_trigger_matching, bind_obj(&T::handle_trigger_matching, this));
    result &= handler.register_event_handler(&m_retrigger, bind_obj(&T::handle_retrigger, this));
    return result;
}

bool FSMController::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result &= handler.unregister_event_handler(&m_reset);
    result &= handler.unregister_event_handler(&m_trigger_matching);
    result &= handler.unregister_event_handler(&m_retrigger);
    return result;
}

void FSMController::run() {
}
