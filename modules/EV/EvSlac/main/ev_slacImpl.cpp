// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include "ev_slacImpl.hpp"

#include <algorithm>
#include <chrono>
#include <memory>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/logging.hpp>
#include <everest/slac/fsm/ev/context.hpp>
#include <everest/slac/slac_event.hpp>
#include <fmt/core.h>

#include "fsm_controller.hpp"

namespace module {
namespace main {

namespace {
constexpr char kModuleLogPrefix[] = "EvSlac: ";

// How long shutdown() waits for the event loop running in ready() to return. It only has to
// cover one poll wake-up, so anything measured in seconds is generous; the bound exists so that
// a wedged loop degrades into a loud log instead of hanging the whole EVerest shutdown.
constexpr std::chrono::milliseconds LOOP_EXIT_TIMEOUT{5000};
} // namespace

ev_slacImpl::~ev_slacImpl() {
    shutdown();
}

void ev_slacImpl::init() {
    // Nothing to do yet: the EV side opens the PLC socket only after global ready (see ready()),
    // so that no SLAC traffic is generated before the rest of the stack is up.
}

void ev_slacImpl::ready() {
    // The event loop runs on this thread. The framework spawns a dedicated thread for the global
    // ready message and joins it last during teardown, so blocking here is what that thread is
    // for - no worker thread of our own is needed. shutdown() arrives on another framework thread
    // and makes the loop return.
    {
        auto lifecycle = lifecycle_state.handle();
        if (not lifecycle->may_enter_loop()) {
            EVLOG_info << kModuleLogPrefix << "not starting the event loop (shutdown already requested)";
            return;
        }
        lifecycle->ready_entered = true;
    }
    lifecycle_state.notify_all();

    if (initialize_slac_io()) {
        configure_callbacks();
        configure_fsm_context();
        if (create_fsm_controller()) {
            configure_slac_io_callbacks();
            run_event_loop();
        }
    }

    {
        auto lifecycle = lifecycle_state.handle();
        // The loop is gone: handlers must not reach the controller any more.
        lifecycle->worker = nullptr;
        lifecycle->slac_io_ready = false;
        lifecycle->loop_exited = true;
    }
    // Wakes shutdown(), which blocks until loop_exited is set.
    lifecycle_state.notify_all();
}

void ev_slacImpl::shutdown() {
    FSMController* local_fsm_ctrl{nullptr};
    {
        auto lifecycle = lifecycle_state.handle();
        // Idempotent: the framework hook and the destructor may both get here. A repeat call has
        // work to do only if a previous one gave up waiting on a loop that is still running.
        if (lifecycle->shutting_down and lifecycle->loop_settled()) {
            return;
        }
        lifecycle->shutting_down = true;
        // From here on command handlers drop instead of touching the controller.
        local_fsm_ctrl = lifecycle->worker;
        lifecycle->worker = nullptr;
        lifecycle->slac_io_ready = false;
        lifecycle->slac_fsm_started = false;
    }
    lifecycle_state.notify_all();

    if (local_fsm_ctrl) {
        local_fsm_ctrl->stop();
    }
    online.store(false);
    exit_event.notify();

    // Wait for the loop before returning: the framework joins the thread running ready() and
    // destroys the module afterwards, and everything the loop touches lives in this object.
    auto const result = everest::lib::util::wait_for_loop_exit(lifecycle_state, LOOP_EXIT_TIMEOUT);
    if (result == everest::lib::util::LoopExitResult::TimedOut) {
        EVLOG_error << kModuleLogPrefix << "the event loop did not stop within " << LOOP_EXIT_TIMEOUT.count()
                    << " ms; leaking the SLAC controller, I/O and context because the loop may still use them";
        // The framework destroys this module right after shutdown() returns; leak what the loop may
        // still be using rather than free it under it.
        (void)fsm_ctrl.release();
        (void)slac_io.release();
        (void)fsm_ctx.release();
        return;
    }

    fsm_ctrl.reset();
    slac_io.reset();
    fsm_ctx.reset();
}

bool ev_slacImpl::initialize_slac_io() {
    try {
        slac_io = std::make_unique<everest::lib::slac::SlacEvent>(config.device);
    } catch (const std::exception& e) {
        abort_event_loop(fmt::format("Failed to initialize SLAC I/O on device '{}': {}", config.device, e.what()));
        return false;
    } catch (...) {
        abort_event_loop(fmt::format("Failed to initialize SLAC I/O on device '{}': unknown error", config.device));
        return false;
    }
    return true;
}

void ev_slacImpl::configure_callbacks() {
    callbacks.send_raw_slac = [this](slac::messages::HomeplugMessage& msg) -> bool {
        // Loop thread only, called from inside the state machine while a dispatcher holds the
        // lifecycle monitor: must not take it. Readiness is the socket's business, and shutdown()
        // destroys slac_io only after this loop has exited.
        if (not slac_io) {
            EVLOG_warning << kModuleLogPrefix << "SLAC I/O is unavailable. Dropping outgoing message.";
            return false;
        }
        if (not slac_io->send(msg)) {
            EVLOG_warning << kModuleLogPrefix << "SLAC I/O failed to send Homeplug frame.";
            return false;
        }
        return true;
    };

    callbacks.signal_dlink_ready = [this](bool value) { publish_dlink_ready(value); };

    callbacks.signal_state = [this](const std::string& value) {
        try {
            publish_state(types::slac::string_to_state(value));
        } catch (const std::exception& e) {
            EVLOG_error << kModuleLogPrefix
                        << fmt::format("Tried to publish unknown SLAC state '{}'. Error: {}", value, e.what());
        }
    };

    callbacks.log_debug = [](const std::string& text) { EVLOG_debug << kModuleLogPrefix << text; };
    callbacks.log_info = [](const std::string& text) { EVLOG_info << kModuleLogPrefix << text; };
    callbacks.log_warn = [](const std::string& text) { EVLOG_warning << kModuleLogPrefix << text; };
    callbacks.log_error = [](const std::string& text) { EVLOG_error << kModuleLogPrefix << text; };
}

void ev_slacImpl::configure_fsm_context() {
    fsm_ctx = std::make_unique<slac_fsm::ev::Context>(callbacks, slac_io->get_mac_addr());

    // Ranges are validated against the manifest by the framework before ready() runs.
    fsm_ctx->slac_config.set_key_timeout = std::chrono::milliseconds{config.set_key_timeout_ms};
    fsm_ctx->slac_config.parm_req_attempts = config.parm_req_attempts;
}

bool ev_slacImpl::create_fsm_controller() {
    fsm_ctrl = std::make_unique<FSMController>(*fsm_ctx);
    fsm_ctrl->set_fatal_handler([this](std::string const& reason) { abort_event_loop(reason); });
    {
        auto lifecycle = lifecycle_state.handle();
        if (lifecycle->shutting_down) {
            return false;
        }
        lifecycle->worker = fsm_ctrl.get();
    }
    return true;
}

void ev_slacImpl::configure_slac_io_callbacks() {
    slac_io->set_callback([this](slac::messages::HomeplugMessage const& msg) {
        // Loop thread; runs the state machine in place under the lifecycle monitor like every other
        // dispatch. Nothing reachable from the machine takes the monitor (send_raw_slac reads only
        // the I/O object), which is what makes holding it safe.
        // A dropped frame is logged by post_command; there is no one to hand the result to.
        (void)post_command("SLAC frame", [&msg](FSMController& target) {
            target.signal_new_slac_message(msg);
            return true;
        });
    });
    slac_io->set_error_callback([this](auto on_error, auto const& detail) { handle_slac_io_error(on_error, detail); });
    slac_io->set_ready_callback([this]() {
        try {
            handle_slac_io_ready();
        } catch (const std::exception& e) {
            abort_event_loop(fmt::format("SLAC I/O ready handling failed: {}", e.what()));
        } catch (...) {
            abort_event_loop("SLAC I/O ready handling failed: unknown error");
        }
    });
}

void ev_slacImpl::run_event_loop() {
    auto registrations_ok = true;
    if (!event_handler.register_event_handler(slac_io.get())) {
        EVLOG_error << kModuleLogPrefix << "Failed to register SLAC I/O event handler.";
        registrations_ok = false;
    }
    if (!event_handler.register_event_handler(fsm_ctrl.get())) {
        EVLOG_error << kModuleLogPrefix << "Failed to register SLAC FSM event handler.";
        registrations_ok = false;
    }
    // Registered so that notifying it wakes poll(); the flag is `online`, the event is just the
    // knock on the door. An eventfd counts, so a notify that lands before this registration is
    // not lost - it fires on the first poll.
    if (!event_handler.register_event_handler(&exit_event, [](auto&) {})) {
        EVLOG_error << kModuleLogPrefix << "Failed to register exit event handler.";
        registrations_ok = false;
    }
    if (registrations_ok) {
        try {
            event_handler.run(online);
        } catch (const std::exception& e) {
            abort_event_loop(fmt::format("SLAC event loop stopped unexpectedly: {}", e.what()));
        } catch (...) {
            abort_event_loop("SLAC event loop stopped unexpectedly: unknown error");
        }
    } else {
        abort_event_loop("Aborting SLAC startup due to event handler registration failure.");
    }

    // Drop the registrations while the registered objects are still alive; shutdown() destroys
    // them once this returns.
    (void)event_handler.unregister_event_handler(slac_io.get());
    (void)event_handler.unregister_event_handler(fsm_ctrl.get());
    (void)event_handler.unregister_event_handler(&exit_event);
}

void ev_slacImpl::handle_slac_io_ready() {
    FSMController* local_fsm_ctrl{nullptr};
    bool should_start_fsm{false};
    {
        auto lifecycle = lifecycle_state.handle();
        if (lifecycle->shutting_down) {
            return;
        }
        lifecycle->slac_io_ready = true;
        local_fsm_ctrl = lifecycle->worker;
        if (local_fsm_ctrl && !lifecycle->slac_fsm_started) {
            lifecycle->slac_fsm_started = true;
            should_start_fsm = true;
        }
    }

    // The interface MAC may not have been readable when SlacEvent was constructed (device
    // enumerating late -> all-zero MAC), and SlacEvent refreshes its own copy when the socket
    // recovers. Re-capture it on every ready event -- ahead of the FSM start below -- so
    // CM_SLAC_PARM.REQ / MNBC sounds / CM_SLAC_MATCH.REQ never carry a stale or zero EV MAC.
    if (slac_io && fsm_ctx) {
        std::copy_n(slac_io->get_mac_addr(), fsm_ctx->ev_host_mac.size(), fsm_ctx->ev_host_mac.begin());
    }

    clear_communication_fault();

    if (should_start_fsm) {
        EVLOG_info << kModuleLogPrefix << "SLAC I/O is ready. Starting the SLAC state machine.";
        if (!local_fsm_ctrl->init()) {
            abort_event_loop("Failed to arm the SLAC state machine tick timer.");
        }
    } else if (!local_fsm_ctrl) {
        EVLOG_warning << kModuleLogPrefix << "SLAC I/O ready callback received without an active controller.";
    }
}

void ev_slacImpl::handle_slac_io_error(bool on_error, const std::string& detail) {
    if (on_error) {
        // Loop thread. Run the reset path so the consumer sees UNMATCHED / dlink_ready(false)
        // instead of a frozen MATCHED until the socket recovers.
        // Without a live controller there is nothing to tear down; the fault below still goes out.
        (void)post_command("I/O error teardown", [](FSMController& target) {
            target.teardown();
            return true;
        });
        auto const detail_message = detail.empty() ? "unknown error" : detail;
        auto const fault_message =
            fmt::format("SLAC PLC communication unavailable on device {}: {}", config.device, detail_message);
        EVLOG_error << kModuleLogPrefix << "SLAC I/O is in error. Waiting for hardware recovery: " << detail_message;
        raise_communication_fault(fault_message);
    } else {
        EVLOG_info << kModuleLogPrefix << "SLAC I/O error cleared.";
        clear_communication_fault();
    }
}

bool ev_slacImpl::post_command(char const* command, std::function<bool(FSMController&)> const& post) {
    // The lifecycle monitor is held across the call, not just across the lookup: shutdown() waits
    // for the event loop, not for in-flight command handlers, and destroys the controller
    // afterwards under this monitor. Nothing reachable from the state machine takes the monitor, so
    // frames and the I/O error teardown go through here as well.
    auto lifecycle = lifecycle_state.handle();
    auto* const target = lifecycle->dispatch_target();
    if (target == nullptr) {
        EVLOG_warning << kModuleLogPrefix << "Ignoring " << command
                      << " because SLAC controller or PLC I/O is not available.";
        return false;
    }
    if (!post(*target)) {
        EVLOG_warning << kModuleLogPrefix << "Dropped " << command << " because the SLAC state machine is not running.";
        return false;
    }
    return true;
}

void ev_slacImpl::raise_communication_fault(const std::string& message) {
    bool should_raise{false};
    bool should_replace{false};
    {
        auto lifecycle = lifecycle_state.handle();
        lifecycle->slac_io_ready = false;
        lifecycle->slac_fsm_started = false;
        if (!lifecycle->communication_fault_raised) {
            lifecycle->communication_fault_raised = true;
            should_raise = true;
        } else if (lifecycle->communication_fault_message != message) {
            should_replace = true;
        }
        lifecycle->communication_fault_message = message;
    }

    if (should_replace && error_manager) {
        clear_error("generic/CommunicationFault");
    }

    if ((should_raise || should_replace) && error_factory && error_manager) {
        raise_error(error_factory->create_error("generic/CommunicationFault", "", message));
    }
}

void ev_slacImpl::clear_communication_fault() {
    bool should_clear{false};
    {
        auto lifecycle = lifecycle_state.handle();
        lifecycle->slac_io_ready = true;
        if (lifecycle->communication_fault_raised) {
            lifecycle->communication_fault_raised = false;
            lifecycle->communication_fault_message.clear();
            should_clear = true;
        }
    }

    if (should_clear && error_manager) {
        clear_error("generic/CommunicationFault");
    }
}

void ev_slacImpl::abort_event_loop(const std::string& reason) {
    EVLOG_error << kModuleLogPrefix << reason;
    // Makes the loop in ready() return (if it is running) and keeps it from being entered.
    online.store(false);
    FSMController* local_fsm_ctrl{nullptr};
    bool should_raise_fault{false};
    {
        auto lifecycle = lifecycle_state.handle();
        should_raise_fault = !lifecycle->shutting_down;
        local_fsm_ctrl = lifecycle->worker;
        lifecycle->slac_io_ready = false;
        lifecycle->slac_fsm_started = false;
        lifecycle->worker = nullptr;
    }

    if (local_fsm_ctrl) {
        // Loop thread only (all callers run on the thread driving the loop). Tear down through the
        // machine's reset path so the consumer sees the terminal state, as the I/O error path does;
        // a publisher may just have thrown, so a second throw must not leave the loop's catch
        // handler. Either way the controller ends stopped.
        try {
            local_fsm_ctrl->teardown();
        } catch (const std::exception& e) {
            EVLOG_error << kModuleLogPrefix << "SLAC state machine teardown failed: " << e.what();
            local_fsm_ctrl->stop();
        } catch (...) {
            EVLOG_error << kModuleLogPrefix << "SLAC state machine teardown failed: unknown error";
            local_fsm_ctrl->stop();
        }
    }

    if (should_raise_fault) {
        raise_communication_fault(reason);
    }
}

// --- interface commands -----------------------------------------------------------------------
//
// These run on framework threads. They only signal the controller's event_fds, which wake the
// event loop; the state machine is never touched from here.

void ev_slacImpl::handle_reset() {
    // The interface command has no result; a drop is logged by post_command.
    (void)post_command("handle_reset", [](FSMController& target) { return target.signal_reset(); });
}

bool ev_slacImpl::handle_trigger_matching() {
    // False when the command did not reach the state machine, as the interface documents.
    return post_command("handle_trigger_matching",
                        [](FSMController& target) { return target.signal_trigger_matching(); });
}

} // namespace main
} // namespace module
