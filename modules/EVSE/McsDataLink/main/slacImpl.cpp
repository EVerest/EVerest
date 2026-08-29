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

/// How long shutdown() waits for the event loop in ready() to return. It only has to cover one
/// poll wake-up, so anything measured in seconds is generous; the bound exists so that a wedged
/// loop degrades into a loud log instead of hanging the whole EVerest shutdown.
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
    // The controller is built here, not in ready(): EvseManager may issue commands as soon as the
    // global ready signal goes out, and the command queue has to exist before the event loop does
    // so that nothing is lost in between. It is only touched by the loop after ready() starts it.
    controller = std::make_unique<datalink_controller>(controller_config(), controller_callbacks());

    if (not controller->open()) {
        // Fatal for supervision, not for the module: with the socket down the interface still
        // answers commands, the link simply never comes up (TT_EV_link_detect then fails
        // communication initialization honestly). The fault is what makes EvseManager set the
        // connector inoperative rather than letting sessions fail mysteriously.
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
    // The event loop runs on this thread. The framework spawns a dedicated thread for the global
    // ready message and joins it last during teardown, so blocking here is what that thread is
    // for - no worker thread of our own is needed.
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
    // Registered so that notifying it wakes poll(); the flag is `online`, the event is just the
    // knock on the door. An eventfd counts, so a notify that lands before this registration is
    // not lost - it fires on the first poll.
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

    // The handler is about to go out of scope; drop the registrations while it is still alive.
    (void)handler.unregister_event_handler(controller.get());
    (void)handler.unregister_event_handler(&exit_event);
}

void slacImpl::shutdown() {
    {
        auto lifecycle = lifecycle_state.handle();
        // Idempotent: the framework hook and the destructor may both get here. A repeat call has
        // work to do only if a previous one gave up waiting on a loop that is still running.
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

    // Wait for the loop before returning: ~Everest joins the thread running ready(), and
    // everything the loop touches lives in this object. Returning early would let the framework
    // tear the module down underneath a running loop.
    auto const result = everest::lib::util::wait_for_loop_exit(lifecycle_state, loop_exit_timeout);
    if (result == everest::lib::util::LoopExitResult::TimedOut) {
        EVLOG_error << "McsDataLink: the event loop did not stop within " << loop_exit_timeout.count()
                    << " ms; leaving the controller alive because the loop may still be using it";
        return;
    }

    controller.reset();
}

void slacImpl::post_command(char const* command, std::function<void(datalink_controller&)> const& post) {
    // INVARIANT: the lifecycle monitor is held across the post, not just across the lookup.
    //
    // shutdown() waits for the event LOOP to exit, not for in-flight command handlers, and it
    // destroys the controller afterwards. A framework thread that read the pointer, released the
    // monitor and was then preempted could therefore come back and call into a destroyed
    // controller. Holding the monitor for the whole call closes that window: shutdown() cannot get
    // past its own handle() to clear the pointer and reset the controller while we are in here.
    //
    // This cannot deadlock. post() takes the command queue's own lock, and nothing ever takes the
    // lifecycle monitor while holding that one; and wait_for_loop_exit() releases the monitor while
    // it waits, so shutdown() blocking there does not keep us out.
    auto lifecycle = lifecycle_state.handle();
    auto* target = lifecycle->live_worker();
    if (target == nullptr) {
        EVLOG_warning << "McsDataLink: dropping " << command << "; the data link controller is not available";
        return;
    }
    post(*target);
}

void slacImpl::raise_communication_fault(std::string const& message) {
    // Checked before the flag is set, not after: marking the fault as raised when it could not be
    // would make the matching clear_error() below refer to an error that never existed.
    if (not error_factory or not error_manager) {
        // TODO: this fault is dropped rather than replayed. If the error machinery can ever be
        // unavailable here in practice, the message should be stashed and raised once it is up.
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
// All of these run on framework threads. They only hand the command to the controller's queue,
// which signals the event loop; the state machine is never touched from here.

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

void slacImpl::handle_dlink_terminate() {
    // ISO 15118-3 / -10: leave the logical network and become UNMATCHED. On SPE there is no
    // network to leave, so this is a teardown of the reported link state.
    post_command("dlink_terminate", [](datalink_controller& target) { target.post_dlink_terminate(); });
}

void slacImpl::handle_dlink_error() {
    // Unlike SLAC on PLC, this is not just a reset: per IEC 61851-23-3 CC.5.2.3.2 the EVSE restarts
    // the communication session by re-running the B0 to B transition, at most C_conn_retry times
    // and with at least 3 s in between. The controller counts and waits, then publishes
    // request_error_routine so EvseManager's error sequence produces the transition.
    post_command("dlink_error", [](datalink_controller& target) { target.post_dlink_error(); });
}

void slacImpl::handle_dlink_pause() {
    // V2G10-041: the link stays logically up. The state variable remains MATCHED and dlink_ready
    // is not withdrawn, and the carrier loss that follows the PHY powering down in B0 is expected
    // rather than a failure.
    post_command("dlink_pause", [](datalink_controller& target) { target.post_dlink_pause(); });
}

} // namespace main
} // namespace module
