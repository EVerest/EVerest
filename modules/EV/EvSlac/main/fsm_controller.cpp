// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "fsm_controller.hpp"

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/util/misc/bind.hpp>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

FSMController::FSMController(slac_fsm::ev::Context& context) : ctx(context), fsm(context) {
    m_retrigger.set_single_shot(true);
}

bool FSMController::init() {
    bool was_active{false};
    if (!active.compare_exchange_strong(was_active, true)) {
        return true;
    }
    fsm.restart_fsm();
    if (not schedule()) {
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
    // The reset publishes UNMATCHED and may throw out of a publisher; the controller must end
    // stopped either way, or its timer keeps waking a machine the module has given up on.
    if (active.load()) {
        try {
            fsm.reset();
        } catch (...) {
            stop();
            throw;
        }
    }
    stop();
}

void FSMController::signal_new_slac_message(slac::messages::HomeplugMessage const& msg) {
    // Runs under the lifecycle monitor (the module dispatches frames with it held), so a failure
    // must leave by exception: the unwind releases the monitor before the loop's catch handler
    // calls abort_event_loop, which takes it again. The fatal handler is for the paths that run
    // without the monitor; reporting through it from here would deadlock the loop thread on itself.
    if (!active.load()) {
        return;
    }
    step([&] { fsm.message(msg); });
}

void FSMController::step(std::function<void()> const& task) {
    task();
    if (not schedule()) {
        auto const error = errno; // before anything below can clobber it
        throw std::runtime_error(std::string("could not arm the timer: ") + std::strerror(error));
    }
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
    if (!active.load()) {
        return;
    }
    std::string failure;
    try {
        step(task);
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

// The loop wakes the machine only for its earliest deadline; with none pending the timer stays off.
bool FSMController::schedule() {
    auto const wait = fsm.next_wakeup();
    return wait ? m_retrigger.set_timeout(*wait) : m_retrigger.disarm();
}

bool FSMController::signal_reset() {
    return post("reset", [this] { fsm.reset(); });
}

bool FSMController::signal_trigger_matching() {
    return post("trigger_matching", [this] { fsm.trigger_matching(); });
}

void FSMController::handle_retrigger() {
    run_guarded("update", [this] { fsm.update(); });
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
