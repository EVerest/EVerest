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

bool FSMController::post(std::function<void()> task) {
    if (!active.load()) {
        return false;
    }
    auto* handler = m_handler.load();
    if (handler == nullptr) {
        return false;
    }
    handler->add_action(std::move(task));
    return true;
}

void FSMController::signal_reset() {
    post([this] { handle_reset(); });
}

bool FSMController::signal_trigger_matching() {
    return post([this] { handle_trigger_matching(); });
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
    if (!handler.register_event_handler(&m_retrigger, bind_obj(&FSMController::handle_retrigger, this))) {
        return false;
    }
    m_handler.store(&handler);
    return true;
}

bool FSMController::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    m_handler.store(nullptr);
    return handler.unregister_event_handler(&m_retrigger);
}
