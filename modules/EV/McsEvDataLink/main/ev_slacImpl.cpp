// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ev_slacImpl.hpp"

#include <chrono>
#include <cstring>

#include <everest/io/event/fd_event_handler.hpp>
#include <fmt/core.h>

#include "everest/logging.hpp"

namespace module {
namespace main {

namespace {

/// Bound on shutdown()'s wait for the event loop; a wedged loop logs an error instead of hanging shutdown.
constexpr std::chrono::milliseconds loop_exit_timeout{5000};

types::slac::State to_interface_state(link_state value) {
    switch (value) {
    case link_state::matching:
        return types::slac::State::MATCHING;
    case link_state::matched:
        return types::slac::State::MATCHED;
    case link_state::unmatched:
        break;
    }
    return types::slac::State::UNMATCHED;
}

} // namespace

ev_slacImpl::~ev_slacImpl() {
    shutdown();
}

datalink_controller::config ev_slacImpl::controller_config() const {
    datalink_controller::config settings;
    settings.device = config.device;
    settings.link_detect_timeout_ms = config.link_detect_timeout_ms;
    settings.neighbor_liveness = config.neighbor_liveness;
    settings.liveness_grace_ms = config.liveness_grace_ms;
    settings.publish_connector_mac = config.publish_connector_mac;
    return settings;
}

datalink_controller::callbacks ev_slacImpl::controller_callbacks() {
    datalink_controller::callbacks handlers;
    handlers.publish_state = [this](link_state value) { publish_state(to_interface_state(value)); };
    handlers.publish_dlink_ready = [this](bool value) { publish_dlink_ready(value); };
    if (config.publish_connector_mac) {
        // ev_mac_address carries the peer's address, on this side the charging connector's.
        handlers.publish_connector_mac = [this](std::string const& mac) { publish_ev_mac_address(mac); };
    }
    handlers.raise_fault = [this](std::string const& message) { raise_communication_fault(message); };
    handlers.clear_fault = [this]() { clear_communication_fault(); };
    return handlers;
}

void ev_slacImpl::init() {
    // Built here, not in ready(): commands may arrive as soon as the global ready signal goes out.
    controller = std::make_unique<datalink_controller>(controller_config(), controller_callbacks());

    if (not controller->open()) {
        // Not fatal: commands still work, the link never comes up and the setup deadline fails initialization.
        raise_communication_fault(fmt::format("Failed to open the rtnetlink socket for MCS data link "
                                              "supervision on device '{}': {}",
                                              config.device, std::strerror(controller->error())));
    }

    {
        auto lifecycle = lifecycle_state.handle();
        lifecycle->worker = controller.get();
    }
    lifecycle_state.notify_all();
}

void ev_slacImpl::ready() {
    // The event loop runs on this thread; the framework joins the ready() thread last during teardown.
    {
        auto lifecycle = lifecycle_state.handle();
        if (not lifecycle->may_enter_loop()) {
            EVLOG_info << "McsEvDataLink: not starting the event loop (shutdown already requested)";
            return;
        }
        lifecycle->ready_entered = true;
    }
    lifecycle_state.notify_all();

    run_event_loop();

    {
        auto lifecycle = lifecycle_state.handle();
        lifecycle->loop_exited = true;
    }
    // Wakes shutdown(), which blocks until this flag is set.
    lifecycle_state.notify_all();
}

void ev_slacImpl::run_event_loop() {
    everest::lib::io::event::fd_event_handler handler;

    auto registrations_ok = true;
    if (not handler.register_event_handler(controller.get())) {
        EVLOG_error << "McsEvDataLink: failed to register the data link controller";
        registrations_ok = false;
    }
    // Wakes poll() so the loop observes `online`; an eventfd counts, so an early notify fires on the first poll.
    if (not handler.register_event_handler(&exit_event, []() {})) {
        EVLOG_error << "McsEvDataLink: failed to register the exit event";
        registrations_ok = false;
    }
    if (not registrations_ok) {
        raise_communication_fault("Failed to set up the MCS data link event loop; link supervision is unavailable");
        return;
    }

    controller->start();

    try {
        handler.run(online);
    } catch (std::exception const& e) {
        EVLOG_error << "McsEvDataLink: the event loop stopped unexpectedly: " << e.what();
    } catch (...) {
        EVLOG_error << "McsEvDataLink: the event loop stopped unexpectedly: unknown error";
    }

    // Unregister while the handler is alive; the watcher's registration holds a pointer to it.
    (void)handler.unregister_event_handler(controller.get());
    (void)handler.unregister_event_handler(&exit_event);
}

void ev_slacImpl::shutdown() {
    {
        auto lifecycle = lifecycle_state.handle();
        // Idempotent; a repeat call only has work if a previous one timed out on a running loop.
        if (lifecycle->shutting_down and lifecycle->loop_settled()) {
            return;
        }
        lifecycle->shutting_down = true;
        // From here on command handlers drop instead of touching the controller.
        lifecycle->worker = nullptr;
    }
    lifecycle_state.notify_all();

    online.store(false);
    exit_event.notify();

    // Wait for the loop: ~Everest joins the ready() thread and everything the loop touches lives here.
    auto const result = everest::lib::util::wait_for_loop_exit(lifecycle_state, loop_exit_timeout);
    if (result == everest::lib::util::LoopExitResult::TimedOut) {
        EVLOG_error << "McsEvDataLink: the event loop did not stop within " << loop_exit_timeout.count()
                    << " ms; leaving the controller alive because the loop may still be using it";
        return;
    }

    controller.reset();
}

bool ev_slacImpl::post_command(char const* command, std::function<bool(datalink_controller&)> const& post) {
    // INVARIANT: the monitor is held across the post. shutdown() waits only for the loop, then destroys the
    // controller; the held monitor blocks it while we are in here. No deadlock: post() takes only the queue
    // lock, never held while taking the monitor, and wait_for_loop_exit() releases the monitor while waiting.
    auto lifecycle = lifecycle_state.handle();
    auto* target = lifecycle->live_worker();
    if (target == nullptr) {
        EVLOG_warning << "McsEvDataLink: dropping " << command << "; the data link controller is not available";
        return false;
    }
    return post(*target);
}

void ev_slacImpl::raise_communication_fault(std::string const& message) {
    // Checked before the flag is set, so a fault that could not be raised is never marked raised.
    if (not error_factory or not error_manager) {
        // TODO: dropped, not replayed; stash and raise later if this can happen in practice.
        EVLOG_error << "McsEvDataLink: cannot raise generic/CommunicationFault yet: " << message;
        return;
    }

    bool should_raise = false;
    bool should_replace = false;
    {
        auto lifecycle = lifecycle_state.handle();
        if (not lifecycle->communication_fault_raised) {
            lifecycle->communication_fault_raised = true;
            should_raise = true;
        } else if (lifecycle->communication_fault_message != message) {
            should_replace = true;
        }
        lifecycle->communication_fault_message = message;
    }

    if (should_replace) {
        clear_error("generic/CommunicationFault");
    }
    if (should_raise or should_replace) {
        raise_error(error_factory->create_error("generic/CommunicationFault", "", message));
    }
}

void ev_slacImpl::clear_communication_fault() {
    bool should_clear = false;
    {
        auto lifecycle = lifecycle_state.handle();
        if (lifecycle->communication_fault_raised) {
            lifecycle->communication_fault_raised = false;
            lifecycle->communication_fault_message.clear();
            should_clear = true;
        }
    }

    if (should_clear and error_manager) {
        clear_error("generic/CommunicationFault");
    }
}

// --- interface commands -----------------------------------------------------------------------
//
// Framework threads. They only enqueue to the controller; the state machine is never touched here.

void ev_slacImpl::handle_reset() {
    // EvManager calls this right before every trigger_matching: a plain teardown, never a latch.
    (void)post_command("reset", [](datalink_controller& target) {
        target.post_reset();
        return true;
    });
}

bool ev_slacImpl::handle_trigger_matching() {
    // true: accepted for processing; false: shutdown in progress or queue backed up. Whether the machine
    // accepts the transition cannot be answered from this thread without a round trip that could deadlock
    // on the fault path's monitor; the caller's feedback is the `state` variable. See docs/index.rst,
    // "Interface gaps".
    return post_command("trigger_matching", [](datalink_controller& target) { return target.post_trigger_matching(); });
}

} // namespace main
} // namespace module
