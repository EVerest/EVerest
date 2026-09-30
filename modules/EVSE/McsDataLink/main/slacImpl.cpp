// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "slacImpl.hpp"

#include <chrono>
#include <cstring>

#include <everest/io/event/fd_event_handler.hpp>
#include <fmt/core.h>

#include "everest/logging.hpp"

namespace module {
namespace main {

namespace {

/// Bound on shutdown()'s wait for the event loop; a wedged loop logs instead of hanging EVerest shutdown.
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

slacImpl::~slacImpl() {
    shutdown();
}

datalink_controller::config slacImpl::controller_config() const {
    datalink_controller::config settings;
    settings.device = config.device;
    settings.link_detect_timeout_ms = config.link_detect_timeout_ms;
    settings.sync_repetition_ms = config.sync_repetition_ms;
    settings.conn_retry_max = config.conn_retry_max;
    settings.retry_wait_ms = config.retry_wait_ms;
    settings.neighbor_liveness = config.neighbor_liveness;
    settings.liveness_grace_ms = config.liveness_grace_ms;
    settings.publish_ev_mac = config.publish_ev_mac;
    return settings;
}

datalink_controller::callbacks slacImpl::controller_callbacks() {
    datalink_controller::callbacks handlers;
    handlers.publish_state = [this](link_state value) { publish_state(to_interface_state(value)); };
    handlers.publish_dlink_ready = [this](bool value) { publish_dlink_ready(value); };
    handlers.publish_request_error_routine = [this]() { publish_request_error_routine(nullptr); };
    if (config.publish_ev_mac) {
        handlers.publish_ev_mac = [this](std::string const& mac) { publish_ev_mac_address(mac); };
    }
    handlers.raise_fault = [this](std::string const& message) { raise_communication_fault(message); };
    handlers.clear_fault = [this]() { clear_communication_fault(); };
    return handlers;
}

void slacImpl::init() {
    // Built in init(): EvseManager may issue commands as soon as the global ready signal goes out,
    // so the command queue must exist before the event loop.
    controller = std::make_unique<datalink_controller>(controller_config(), controller_callbacks());

    if (not controller->open()) {
        // Not fatal for the module: commands are still answered, the link never comes up. The fault
        // makes EvseManager set the connector inoperative.
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

void slacImpl::ready() {
    // The event loop blocks this thread: the framework runs ready() on a dedicated thread and joins it last.
    {
        auto lifecycle = lifecycle_state.handle();
        if (not lifecycle->may_enter_loop()) {
            EVLOG_info << "McsDataLink: not starting the event loop (shutdown already requested)";
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

void slacImpl::run_event_loop() {
    everest::lib::io::event::fd_event_handler handler;

    auto registrations_ok = true;
    if (not handler.register_event_handler(controller.get())) {
        EVLOG_error << "McsDataLink: failed to register the data link controller";
        registrations_ok = false;
    }
    // Only wakes poll() so `online` is observed; an eventfd counts, so an earlier notify is not lost.
    if (not handler.register_event_handler(&exit_event, []() {})) {
        EVLOG_error << "McsDataLink: failed to register the exit event";
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
        EVLOG_error << "McsDataLink: the event loop stopped unexpectedly: " << e.what();
    } catch (...) {
        EVLOG_error << "McsDataLink: the event loop stopped unexpectedly: unknown error";
    }

    // Unregister before the handler goes out of scope.
    (void)handler.unregister_event_handler(controller.get());
    (void)handler.unregister_event_handler(&exit_event);
}

void slacImpl::shutdown() {
    {
        auto lifecycle = lifecycle_state.handle();
        // Idempotent; a repeat call only has work if a previous one timed out waiting for the loop.
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

    // Returning before the loop exits would let ~Everest tear the module down under a running loop.
    auto const result = everest::lib::util::wait_for_loop_exit(lifecycle_state, loop_exit_timeout);
    if (result == everest::lib::util::LoopExitResult::TimedOut) {
        EVLOG_error << "McsDataLink: the event loop did not stop within " << loop_exit_timeout.count()
                    << " ms; leaving the controller alive because the loop may still be using it";
        return;
    }

    controller.reset();
}

void slacImpl::post_command(char const* command, std::function<void(datalink_controller&)> const& post) {
    // INVARIANT: the lifecycle monitor is held across the post. shutdown() waits for the loop, not for
    // in-flight handlers, then destroys the controller; holding the monitor keeps it out until the
    // post returns. No deadlock: the queue lock is never held while taking the monitor, and
    // wait_for_loop_exit() releases the monitor while waiting.
    auto lifecycle = lifecycle_state.handle();
    auto* target = lifecycle->live_worker();
    if (target == nullptr) {
        EVLOG_warning << "McsDataLink: dropping " << command << "; the data link controller is not available";
        return;
    }
    post(*target);
}

void slacImpl::raise_communication_fault(std::string const& message) {
    // Checked before the flag is set so a fault that could not be raised is not marked as raised.
    if (not error_factory or not error_manager) {
        // TODO: the fault is dropped, not replayed once the error machinery is up.
        EVLOG_error << "McsDataLink: cannot raise generic/CommunicationFault yet: " << message;
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

void slacImpl::clear_communication_fault() {
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

void slacImpl::handle_reset(bool& enable) {
    auto const value = enable;
    post_command("reset", [value](datalink_controller& target) { target.post_reset(value); });
}

void slacImpl::handle_enter_bcd() {
    post_command("enter_bcd", [](datalink_controller& target) { target.post_enter_bcd(); });
}

void slacImpl::handle_leave_bcd() {
    post_command("leave_bcd", [](datalink_controller& target) { target.post_leave_bcd(); });
}

void slacImpl::handle_count_bc(int& count) {
    // The B/C transition count exists for the HomePlug CM_VALIDATE BCB-toggle exchange, which
    // matches a vehicle by counting pilot toggles when the modems cannot tell each other apart.
    // MCS matches over the SPE link instead and has no CM_VALIDATE, so the count has no consumer
    // here. Accepted and ignored: EvseManager pushes it on every edge for whichever slac provider
    // is configured, and refusing it would make an ordinary session log errors.
    (void)count;
}

void slacImpl::handle_dlink_terminate() {
    // ISO 15118-3 / -10: become UNMATCHED. On SPE there is no logical network to leave.
    post_command("dlink_terminate", [](datalink_controller& target) { target.post_dlink_terminate(); });
}

void slacImpl::handle_dlink_error() {
    // IEC 61851-23-3 CC.5.2.3.2: restart via the B0 to B transition, at most C_conn_retry times with
    // >= 3 s in between. The controller counts and waits, then publishes request_error_routine.
    post_command("dlink_error", [](datalink_controller& target) { target.post_dlink_error(); });
}

void slacImpl::handle_dlink_pause() {
    // V2G10-041: stays MATCHED, dlink_ready not withdrawn; carrier loss from the PHY powering down is expected.
    post_command("dlink_pause", [](datalink_controller& target) { target.post_dlink_pause(); });
}

} // namespace main
} // namespace module
