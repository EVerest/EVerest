// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include "fsm_controller.hpp"

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/util/misc/bind.hpp>

FSMController::FSMController(slac_fsm::ev::Context& context) : ctx(context), fsm(context) {
}

bool FSMController::init() {
    bool was_active{false};
    if (!active.compare_exchange_strong(was_active, true)) {
        return true;
    }
    fsm.restart_fsm();
    if (not m_retrigger.set_timeout_ms(10)) {
        stop();
        return false;
    }
    return true;
}

void FSMController::stop() {
    active.store(false);
    m_retrigger.disarm();
}

void FSMController::teardown() {
    if (active.load()) {
        fsm.reset();
    }
    stop();
}

void FSMController::signal_new_slac_message(slac::messages::HomeplugMessage const& msg) {
    if (!active.load()) {
        return;
    }
    fsm.message(msg);
}

void FSMController::set_fatal_handler(FatalHandler handler) {
    m_fatal_handler = std::move(handler);
}

bool FSMController::post(char const* command, std::function<void()> task) {
    if (!active.load()) {
        return false;
    }
    auto* handler = m_handler.load();
    if (handler == nullptr) {
        return false;
    }
    handler->add_action([this, command, task = std::move(task)] { run_guarded(command, task); });
    return true;
}

void FSMController::run_guarded(char const* command, std::function<void()> const& task) {
    // fd_event_handler::run_actions swallows exceptions; a command that throws out of the state
    // machine would otherwise vanish, leaving the machine half-transitioned and nothing in the log.
    std::string failure;
    try {
        task();
        return;
    } catch (const std::exception& e) {
        failure = e.what();
    } catch (...) {
        failure = "unknown error";
    }
    auto const reason = std::string("SLAC state machine failed while handling ") + command + ": " + failure;
    // The fatal handler owns the teardown: it runs the reset path so the consumer sees UNMATCHED
    // and stops the controller. Stopping here first would make that teardown a no-op.
    if (m_fatal_handler) {
        m_fatal_handler(reason);
    } else {
        stop();
        ctx.log_error(reason);
    }
}

bool FSMController::signal_reset() {
    return post("reset", [this] { handle_reset(); });
}

bool FSMController::signal_trigger_matching() {
    return post("trigger_matching", [this] { handle_trigger_matching(); });
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
