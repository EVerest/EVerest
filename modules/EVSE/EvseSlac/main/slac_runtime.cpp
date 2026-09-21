// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#include "slac_runtime.hpp"

#include <algorithm>
#include <exception>
#include <optional>

#include <everest/slac/slac_defs.hpp>
#include <fmt/core.h>

#include "amp_map_file.hpp"

namespace module::main {

namespace {
constexpr char const* COMMUNICATION_FAULT = "generic/CommunicationFault";
constexpr char const* VENDOR_ERROR = "generic/VendorError";
} // namespace

SlacRuntime::SlacRuntime(SlacRuntimeConfig config_, SlacIoFactory io_factory_, SlacSink& sink_) :
    config(std::move(config_)), io_factory(std::move(io_factory_)), sink(sink_) {
}

SlacRuntime::~SlacRuntime() {
    shutdown();
}

void SlacRuntime::log(LogLevel level, std::string const& text) {
    sink.log(level, text);
}

void SlacRuntime::init() {
    // Do not report readiness (init() returning -> signal_ready -> global ready) before the PLC
    // I/O is usable or its bring-up has failed: EvseManager starts issuing enter_bcd/reset right
    // after global ready and commands are dropped while !slac_io_ready, so a car already plugged
    // in at boot would silently lose its first SLAC session. The startup delay and the socket open
    // therefore run inside init().
    //
    // The I/O ready event is delivered through the event loop, and there is no worker thread of
    // our own: the loop runs on the framework's ready thread inside ready(). So init() drives the
    // same loop itself, on the init thread, just until bring-up settled (see run_bring_up_loop).
    // On failure or timeout init() still returns: the raised generic/CommunicationFault makes
    // EvseManager set the connector inoperative instead of the whole EVerest stack stalling.
    if (!wait_for_startup_delay_or_shutdown()) {
        return;
    }
    if (!initialize_slac_io()) {
        return;
    }
    configure_callbacks();
    configure_fsm_context();
    if (!create_fsm_controller()) {
        return;
    }
    configure_slac_io_callbacks();
    if (!register_event_handlers()) {
        return;
    }
    run_bring_up_loop();
}

void SlacRuntime::ready() {
    // The event loop runs on this thread. The framework spawns a dedicated thread for the global
    // ready message and joins it last during teardown, so blocking here is what that thread is
    // for. shutdown() arrives on another framework thread and makes the loop return.
    {
        auto lifecycle = lifecycle_state.handle();
        if (not lifecycle->may_enter_loop()) {
            log(LogLevel::Info, "EvseSlac: not starting the event loop (shutdown already requested)");
            return;
        }
        lifecycle->ready_entered = true;
    }
    lifecycle_state.notify_all();

    // Global ready may be the second of the two events the FSM start waits for.
    start_fsm_if_ready();

    if (online.load()) {
        run_event_loop();
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

void SlacRuntime::shutdown() {
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
    }
    lifecycle_state.notify_all();

    if (local_fsm_ctrl) {
        // Cross-thread: freeze the FSM. The loop-only teardown() through a reset event is not
        // available from here; the objects are destroyed below anyway.
        local_fsm_ctrl->stop();
    }
    online.store(false);
    bring_up_pending.store(false);
    exit_event.notify();

    // Wait for the loop before returning: the framework joins the thread running ready() and
    // destroys the module afterwards, and everything the loop touches lives in this object.
    auto const result = everest::lib::util::wait_for_loop_exit(lifecycle_state, config.loop_exit_timeout);
    if (result == everest::lib::util::LoopExitResult::TimedOut) {
        log(LogLevel::Error, fmt::format("EvseSlac: the event loop did not stop within {} ms; leaking the SLAC "
                                         "controller, I/O and context because the loop may still use them",
                                         config.loop_exit_timeout.count()));
        leak_loop_objects();
        return;
    }

    // NotRunning also covers "init() is still in its bring-up loop on the init thread": that loop
    // observes the flags above and returns, but it may be mid-iteration right now, so wait for it
    // as well before destroying what it polls. ready() is not entered after a shutdown request.
    {
        auto lifecycle = lifecycle_state.handle();
        if (!lifecycle.wait_for([&] { return !lifecycle->bring_up_running; }, config.loop_exit_timeout)) {
            log(LogLevel::Error, fmt::format("EvseSlac: the bring-up loop in init() did not stop within {} ms; leaking "
                                             "the SLAC controller, I/O and context because the loop may still use them",
                                             config.loop_exit_timeout.count()));
            leak_loop_objects();
            return;
        }
    }
    unregister_event_handlers();
    fsm_ctrl.reset();
    slac_io.reset();
    fsm_ctx.reset();
}

void SlacRuntime::leak_loop_objects() {
    // The framework destroys the module right after shutdown() returns. Everything the loop touches
    // lives here, so the objects it may still be using are leaked deliberately rather than freed
    // under it; the members that cannot be released (event handler, monitor, callbacks) still go
    // down with the module.
    (void)fsm_ctrl.release();
    (void)slac_io.release();
    (void)fsm_ctx.release();
}

bool SlacRuntime::wait_for_startup_delay_or_shutdown() {
    if (config.startup_delay.count() > 0) {
        log(LogLevel::Info, fmt::format("Delaying SLAC startup by {} ms", config.startup_delay.count()));
        {
            auto lifecycle = lifecycle_state.handle();
            if (lifecycle.wait_for([&] { return lifecycle->shutting_down; }, config.startup_delay)) {
                return false;
            }
        }
        log(LogLevel::Info, "Continuing with SLAC initialization");
    }
    auto lifecycle = lifecycle_state.handle();
    return !lifecycle->shutting_down;
}

bool SlacRuntime::initialize_slac_io() {
    try {
        slac_io = io_factory(config.device);
    } catch (const std::exception& e) {
        abort_event_loop(fmt::format("Failed to initialize SLAC I/O on device '{}': {}", config.device, e.what()));
        return false;
    } catch (...) {
        abort_event_loop(fmt::format("Failed to initialize SLAC I/O on device '{}': unknown error", config.device));
        return false;
    }
    if (not slac_io) {
        abort_event_loop(fmt::format("Failed to initialize SLAC I/O on device '{}': no I/O object", config.device));
        return false;
    }
    return true;
}

void SlacRuntime::configure_callbacks() {
    callbacks.send_raw_slac = [this](SlacIo::Frame& msg) -> bool {
        // Loop thread only, called from inside the state machine while a dispatcher holds the
        // lifecycle monitor: must not take it. Readiness is the link's business (send() rejects a
        // frame it cannot take), and shutdown() destroys slac_io only after this loop has exited.
        if (not slac_io) {
            log(LogLevel::Warning, "SLAC I/O is unavailable. Dropping outgoing message.");
            return false;
        }
        if (not slac_io->send(msg)) {
            log(LogLevel::Warning, "SLAC I/O failed to send Homeplug frame.");
            return false;
        }
        return true;
    };
    callbacks.signal_dlink_ready = [this](bool value) {
        if (value) {
            consumer_may_have_link = true;
        }
        sink.publish_dlink_ready(value);
        if (not value) {
            consumer_may_have_link = false;
        }
    };
    callbacks.signal_state = [this](everest::lib::slac::D3State value) {
        if (value != everest::lib::slac::D3State::Unmatched) {
            consumer_may_be_matched = true;
        }
        sink.publish_state(value);
        if (value == everest::lib::slac::D3State::Unmatched) {
            consumer_may_be_matched = false;
        }
    };
    callbacks.signal_error_routine_request = [this]() { sink.request_error_routine(); };
    callbacks.log_debug = [this](const std::string& text) { log(LogLevel::Debug, text); };
    callbacks.log_info = [this](const std::string& text) { log(LogLevel::Info, text); };
    callbacks.log_warn = [this](const std::string& text) { log(LogLevel::Warning, text); };
    callbacks.log_error = [this](const std::string& text) { log(LogLevel::Error, text); };
    callbacks.pub_telemetry = [this](const std::string& block, const std::string& key, const std::string& value) {
        if (!config.telemetry_enabled) {
            return;
        }
        // Runs inside the state machine's event processing. A telemetry failure is a telemetry
        // defect, not a PLC fault, and must not take matching down with it.
        try {
            sink.publish_telemetry(block, key, value);
        } catch (const std::exception& e) {
            log(LogLevel::Warning, fmt::format("SLAC telemetry {}/{} not published: {}", block, key, e.what()));
        }
    };
    if (config.publish_mac_on_first_parm_req) {
        callbacks.signal_ev_mac_address_parm_req = [this](const std::string& mac) { sink.publish_ev_mac_address(mac); };
    }
    if (config.publish_mac_on_match_cnf) {
        callbacks.signal_ev_mac_address_match_cnf = [this](const std::string& mac) {
            sink.publish_ev_mac_address(mac);
        };
    }
}

void SlacRuntime::configure_fsm_context() {
    fsm_ctx = std::make_unique<everest::lib::slac::fsm::evse::Context>(callbacks);
    fsm_ctx->slac_config = config.slac;
    if (config.slac.max_matching_sessions > 16) {
        log(LogLevel::Warning, fmt::format("High max_matching_sessions value '{}' configured; this can create "
                                           "excessive SLAC processing load",
                                           config.slac.max_matching_sessions));
    }

    // CM_AMP_MAP transmit-power limitation (ISO 15118-3 A.9.6). The SECC always responds to an
    // incoming CM_AMP_MAP.REQ; only the SECC-initiated direction is gated by initiate_amp_map and
    // needs the amplitude map.
    fsm_ctx->slac_config.initiate_amp_map = config.initiate_amp_map;
    bool amp_enabled = false;
    if (config.initiate_amp_map) {
        std::string error;
        auto const amp_map = config.amp_map_file.empty() ? std::optional<AmpMap>{default_amp_map()}
                                                         : load_amp_map_file(config.amp_map_file, error);
        if (amp_map) {
            fsm_ctx->slac_config.amp_map_len = amp_map->len;
            fsm_ctx->slac_config.amp_map_data = amp_map->data;
            amp_enabled = true;
            log(LogLevel::Info, fmt::format("CM_AMP_MAP initiation enabled with {} carriers{}", amp_map->len,
                                            config.amp_map_file.empty() ? " (built-in all-maximum-TX map, no reduction)"
                                                                        : " from '" + config.amp_map_file + "'"));
        } else {
            // Fail closed: the map limits transmit power. Without the operator's map the SECC must
            // not initiate the exchange at all, and certainly not with a full-power map while the
            // log claims the file was applied.
            fsm_ctx->slac_config.initiate_amp_map = false;
            auto const message = fmt::format("amp_map_file '{}' rejected: {}. CM_AMP_MAP initiation disabled.",
                                             config.amp_map_file, error);
            log(LogLevel::Error, message);
            sink.raise_fault(VENDOR_ERROR, "amp_map_file", message);
        }
    }
    {
        std::lock_guard<std::mutex> guard(diagnostics_mutex);
        amp_map_enabled = amp_enabled;
    }

    fsm_ctx->slac_config.provide_telemetry = config.telemetry_enabled;
    fsm_ctx->slac_config.generate_nmk();
    std::copy_n(slac_io->mac_address(), fsm_ctx->evse_mac.size(), fsm_ctx->evse_mac.begin());
}

bool SlacRuntime::create_fsm_controller() {
    fsm_ctrl = std::make_unique<FSMController>(*fsm_ctx);
    // A posted command that throws out of the state machine ends the loop the same way a throw on
    // the receive path does; the controller has stopped itself before this runs (loop thread).
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

void SlacRuntime::configure_slac_io_callbacks() {
    slac_io->set_rx_handler([this](SlacIo::Frame const& msg) {
        // Qualcomm PLC chips emit VS_ATTENUATION_CHARACTERISTICS (vendor MMTYPE 0xA14E) as
        // unsolicited broadcasts during sounding. The FSM does not handle it and would log every
        // frame; drop it before the dispatch.
        if (msg.get_mmtype() == everest::lib::slac::defs::qualcomm::MMTYPE_QCA_VS_ATTENUATION_CHARACTERISTICS) {
            return;
        }
        // Loop thread; runs the state machine in place under the lifecycle monitor, like every
        // other dispatch (see dispatch_to_controller for the invariant that makes that safe).
        if (not dispatch_to_controller(lifecycle_state,
                                       [&msg](FSMController& target) { target.signal_new_slac_message(msg); })) {
            log(LogLevel::Warning, "Ignoring SLAC message because SLAC controller or PLC I/O is not available.");
        }
    });
    slac_io->set_error_handler(
        [this](bool on_error, std::string const& detail) { handle_slac_io_error(on_error, detail); });
    // The ready action also runs through the handler's swallowing action queue, and it starts the
    // state machine; a throw there must end the loop like every other one.
    slac_io->set_ready_handler([this]() {
        try {
            handle_slac_io_ready();
        } catch (const std::exception& e) {
            abort_event_loop(fmt::format("SLAC I/O ready handling failed: {}", e.what()));
        } catch (...) {
            abort_event_loop("SLAC I/O ready handling failed: unknown error");
        }
    });
}

bool SlacRuntime::register_event_handlers() {
    auto registrations_ok = true;
    if (!event_handler.register_event_handler(slac_io.get())) {
        log(LogLevel::Error, "Failed to register SLAC IO event handler.");
        registrations_ok = false;
    }
    if (!event_handler.register_event_handler(fsm_ctrl.get())) {
        log(LogLevel::Error, "Failed to register FSM controller event handler.");
        registrations_ok = false;
    }
    // Registered so that notifying it wakes poll(); the flags are `online` and `bring_up_pending`,
    // the event is just the knock on the door. An eventfd counts, so a notify that lands before
    // this registration is not lost - it fires on the first poll.
    if (!event_handler.register_event_handler(&exit_event, [](auto&) {})) {
        log(LogLevel::Error, "Failed to register exit event handler.");
        registrations_ok = false;
    }
    if (!registrations_ok) {
        abort_event_loop("Aborting SLAC startup due to event handler registration failure.");
        unregister_event_handlers();
        return false;
    }
    return true;
}

void SlacRuntime::unregister_event_handlers() {
    // Tolerates objects that were never registered; drop the registrations while the registered
    // objects are still alive.
    if (slac_io) {
        (void)event_handler.unregister_event_handler(slac_io.get());
    }
    if (fsm_ctrl) {
        (void)event_handler.unregister_event_handler(fsm_ctrl.get());
    }
    (void)event_handler.unregister_event_handler(&exit_event);
    (void)event_handler.unregister_event_handler(&bring_up_timer);
}

void SlacRuntime::run_bring_up_loop() {
    // Same handler, same registrations as ready() will use; only the exit condition differs. The
    // ready callback, the error callback, the timer below and shutdown() all clear
    // bring_up_pending, and the two callbacks also wake poll() by being fd events themselves.
    bring_up_timer.set_single_shot(true);
    if (!bring_up_timer.set_timeout(config.io_bring_up_timeout) ||
        !event_handler.register_event_handler(&bring_up_timer, [this]() {
            log(LogLevel::Warning, "SLAC I/O bring-up timer expired.");
            bring_up_pending.store(false);
        })) {
        abort_event_loop("Failed to arm the SLAC I/O bring-up timer.");
        return;
    }

    bring_up_pending.store(true);
    {
        auto lifecycle = lifecycle_state.handle();
        // Tells shutdown() to wait for this loop before destroying anything it polls.
        lifecycle->bring_up_running = true;
        // Settled already (e.g. shutdown() raced ahead)? Then there is nothing to wait for.
        if (lifecycle->io_bring_up_settled()) {
            bring_up_pending.store(false);
        }
    }
    lifecycle_state.notify_all();

    try {
        while (online.load() && bring_up_pending.load()) {
            event_handler.poll();
            event_handler.run_actions();
        }
    } catch (const std::exception& e) {
        abort_event_loop(fmt::format("SLAC event loop stopped unexpectedly during bring-up: {}", e.what()));
    } catch (...) {
        abort_event_loop("SLAC event loop stopped unexpectedly during bring-up: unknown error");
    }

    (void)bring_up_timer.disarm();
    (void)event_handler.unregister_event_handler(&bring_up_timer);

    IoBringUpResult result{IoBringUpResult::TimedOut};
    {
        auto lifecycle = lifecycle_state.handle();
        result = bring_up_result(*lifecycle);
    }
    if (result == IoBringUpResult::TimedOut) {
        raise_communication_fault(fmt::format("SLAC I/O on device {} reported neither ready nor an error within {} ms; "
                                              "continuing startup with SLAC unavailable",
                                              config.device, config.io_bring_up_timeout.count()));
    }

    {
        auto lifecycle = lifecycle_state.handle();
        lifecycle->bring_up_running = false;
    }
    // Wakes a shutdown() that is waiting for the bring-up loop.
    lifecycle_state.notify_all();
}

void SlacRuntime::run_event_loop() {
    try {
        event_handler.run(online);
    } catch (const std::exception& e) {
        abort_event_loop(fmt::format("SLAC event loop stopped unexpectedly: {}", e.what()));
    } catch (...) {
        abort_event_loop("SLAC event loop stopped unexpectedly: unknown error");
    }
}

void SlacRuntime::handle_slac_io_ready() {
    {
        auto lifecycle = lifecycle_state.handle();
        if (lifecycle->shutting_down) {
            return;
        }
        lifecycle->slac_io_ready = true;
    }
    // Ends the bring-up loop in init() if that is where we are.
    bring_up_pending.store(false);

    if (slac_io && fsm_ctx) {
        // The link is up, so the interface MAC is readable now even if it was not at construction.
        std::copy_n(slac_io->mac_address(), fsm_ctx->evse_mac.size(), fsm_ctx->evse_mac.begin());
        std::lock_guard<std::mutex> guard(diagnostics_mutex);
        captured_mac = fsm_ctx->evse_mac;
    }

    clear_communication_fault();
    start_fsm_if_ready();
}

void SlacRuntime::start_fsm_if_ready() {
    // Called from both handle_slac_io_ready() and ready(): the FSM starts on whichever of
    // {PLC I/O ready, global ready} happens second (see LifecycleStateT::fsm_start_allowed).
    FSMController* local_fsm_ctrl{nullptr};
    {
        auto lifecycle = lifecycle_state.handle();
        if (!lifecycle->fsm_start_allowed()) {
            return;
        }
        local_fsm_ctrl = lifecycle->worker;
    }
    if (local_fsm_ctrl) {
        log(LogLevel::Info, "SLAC I/O is ready. Starting the SLAC state machine.");
        // Starting runs the machine into Reset and publishes UNMATCHED. From ready() this is not
        // inside any loop catch handler, and the framework's ready thread has none either: a
        // publisher that throws here must end in a fault, not in std::terminate.
        try {
            if (!local_fsm_ctrl->init()) {
                abort_event_loop("Failed to arm the SLAC state machine tick timer.");
            }
        } catch (const std::exception& e) {
            abort_event_loop(fmt::format("SLAC state machine start failed: {}", e.what()));
        } catch (...) {
            abort_event_loop("SLAC state machine start failed: unknown error");
        }
    } else {
        log(LogLevel::Warning, "SLAC state machine start requested without an active controller. Start dropped.");
    }
}

void SlacRuntime::handle_slac_io_error(bool on_error, std::string const& detail) {
    if (on_error) {
        // Loop thread: teardown() runs the FSM's reset path in place instead of freezing it, so the
        // consumer sees UNMATCHED / dlink_ready(false) right away.
        (void)dispatch_to_controller(lifecycle_state, [](FSMController& target) { target.teardown(); });
        auto const detail_message = detail.empty() ? std::string("unknown error") : detail;
        log(LogLevel::Error, "SLAC I/O is in error. Waiting for hardware recovery: " + detail_message);
        raise_communication_fault(
            fmt::format("SLAC PLC communication unavailable on device {}: {}", config.device, detail_message));
    } else {
        log(LogLevel::Info, "SLAC I/O error cleared.");
        clear_communication_fault();
    }
}

bool SlacRuntime::post_command(char const* command, std::function<bool(FSMController&)> const& post) {
    // The lifecycle monitor is held across the call, not just across the lookup: shutdown() waits
    // for the event loop, not for in-flight command handlers, and destroys the controller
    // afterwards under this monitor, so a framework thread cannot come back from a preemption into
    // a destroyed controller. Nothing reachable from the state machine takes the monitor (see
    // dispatch_to_controller), so this is also how frames and the I/O error teardown are dispatched.
    auto lifecycle = lifecycle_state.handle();
    auto* const target = lifecycle->dispatch_target();
    if (target == nullptr) {
        log(LogLevel::Warning,
            fmt::format("Ignoring {} because SLAC controller or PLC I/O is not available.", command));
        return false;
    }
    if (!post(*target)) {
        // The controller exists and the PLC I/O is up, but the state machine has not been started
        // (global ready not seen yet) or was stopped after a fault. The command would otherwise be
        // lost without a trace; EvseManager's first reset/enter_bcd after boot can land here.
        log(LogLevel::Warning, fmt::format("Dropped {} because the SLAC state machine is not running.", command));
        return false;
    }
    return true;
}

void SlacRuntime::raise_communication_fault(std::string const& message) {
    bool should_raise{false};
    bool should_replace{false};
    {
        auto lifecycle = lifecycle_state.handle();
        lifecycle->slac_io_ready = false;
        if (!lifecycle->communication_fault_raised) {
            lifecycle->communication_fault_raised = true;
            should_raise = true;
        } else if (lifecycle->communication_fault_message != message) {
            should_replace = true;
        }
        lifecycle->communication_fault_message = message;
    }
    if (should_replace) {
        sink.clear_fault(COMMUNICATION_FAULT);
    }
    if (should_raise || should_replace) {
        sink.raise_fault(COMMUNICATION_FAULT, "", message);
    }
    // A failed bring-up is a settled bring-up: ends the loop in init() if that is where we are.
    bring_up_pending.store(false);
    lifecycle_state.notify_all();
}

void SlacRuntime::clear_communication_fault() {
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
    if (should_clear) {
        sink.clear_fault(COMMUNICATION_FAULT);
    }
    lifecycle_state.notify_all();
}

void SlacRuntime::abort_event_loop(std::string const& reason) {
    log(LogLevel::Error, reason);
    // Makes the loop (bring-up or ready) return and keeps it from being entered again.
    online.store(false);
    bring_up_pending.store(false);
    FSMController* local_fsm_ctrl{nullptr};
    bool should_raise_fault{false};
    {
        auto lifecycle = lifecycle_state.handle();
        should_raise_fault = !lifecycle->shutting_down;
        local_fsm_ctrl = lifecycle->worker;
        lifecycle->slac_io_ready = false;
        lifecycle->worker = nullptr;
    }
    if (local_fsm_ctrl) {
        // Loop thread only (all callers run on the thread driving the loop). The FSM is torn down
        // through a reset event so the consumer sees UNMATCHED / dlink_ready(false) the normal way.
        // This runs from inside the loop's catch handler or a command guard, after a publisher may
        // just have thrown: the reset may throw again, or its publications may not reach the
        // consumer. Whatever it managed, the consumer must end up with UNMATCHED and no link, so
        // the rest is published by hand.
        try {
            local_fsm_ctrl->teardown();
        } catch (const std::exception& e) {
            log(LogLevel::Error, fmt::format("SLAC state machine teardown failed: {}", e.what()));
        } catch (...) {
            log(LogLevel::Error, "SLAC state machine teardown failed: unknown error");
        }
        stop_and_publish_unmatched_by_hand(*local_fsm_ctrl);
    }
    if (should_raise_fault) {
        raise_communication_fault(reason);
    }
}

void SlacRuntime::stop_and_publish_unmatched_by_hand(FSMController& target) {
    target.stop();
    // Each publication on its own: a sink that threw once may throw again, and a second throw here
    // would leave the loop's catch handler and terminate the process.
    auto publish = [this](char const* what, auto&& fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            log(LogLevel::Error, fmt::format("SLAC could not publish {} after the fault: {}", what, e.what()));
        } catch (...) {
            log(LogLevel::Error, fmt::format("SLAC could not publish {} after the fault: unknown error", what));
        }
    };
    if (consumer_may_have_link) {
        publish("dlink_ready(false)", [this] {
            sink.publish_dlink_ready(false);
            consumer_may_have_link = false;
        });
    }
    if (consumer_may_be_matched) {
        publish("UNMATCHED", [this] {
            sink.publish_state(everest::lib::slac::D3State::Unmatched);
            consumer_may_be_matched = false;
        });
    }
}

// --- interface commands -----------------------------------------------------------------------
//
// Framework threads. They only queue work for the event loop; the state machine is never touched
// from here.

bool SlacRuntime::reset(bool enable) {
    // CC: as power saving is not implemented, we actually don't need to reset at beginning of session (enable=true): At
    // start of everest it is being reset once and then it is enough to reset at the end of each session. This saves
    // some hundreds of msecs at the beginning of the charging session as we do not need to set up keys. Then
    // EvseManager can switch on 5% PWM basically immediately as SLAC is already ready.
    if (enable) {
        return true;
    }
    return post_command("reset", [](FSMController& target) { return target.signal_reset(); });
}

bool SlacRuntime::enter_bcd() {
    return post_command("enter_bcd", [](FSMController& target) { return target.signal_enter_bcd(); });
}

bool SlacRuntime::leave_bcd() {
    return post_command("leave_bcd", [](FSMController& target) { return target.signal_leave_bcd(); });
}

void SlacRuntime::count_bc(int count) {
    // EvseManager pushes the running count of Control-Pilot B/C transitions here on every edge. Forward
    // it into the FSM context so the CM_VALIDATE handler can detect the number of BCB toggles the EV
    // performed. Dropping a sample is harmless: the handler reads the latest value on the next request.
    auto lifecycle = lifecycle_state.handle();
    if (auto* const target = lifecycle->dispatch_target()) {
        target->signal_count_bc(count);
    }
}

bool SlacRuntime::dlink_terminate() {
    // With receiving a D-LINK_TERMINATE.request from HLE, the communication node shall leave the
    // logical network within TP_match_leave. All parameters related to the current link shall be
    // set to the default value and shall change to the status "Unmatched".
    log(LogLevel::Info, "D-LINK_TERMINATE.request received, leaving network.");
    return post_command("dlink_terminate", [](FSMController& target) { return target.signal_reset(); });
}

bool SlacRuntime::dlink_error() {
    // The D-LINK_ERROR.request requests lower layers to terminate the data link and restart the
    // matching process by a control pilot transition through state E (on EVSE side this should be
    // state F though). CP signal is handled by EvseManager, so we just need to reset the SLAC state
    // machine here. DLINK_ERROR will be sent from HLC layers when they detect that the connection is dead.
    log(LogLevel::Warning, "D-LINK_ERROR.request received");
    return post_command("dlink_error", [](FSMController& target) { return target.signal_reset(); });
}

void SlacRuntime::dlink_pause() {
    // The D-LINK_PAUSE.request requests lower layers to enter a power saving mode. While being in this
    // mode, the state will be kept to "Matched". Nothing to do: the PLC modem is not powered down;
    // low power mode is optional in ISO 15118-3.
    log(LogLevel::Info, "D-LINK_PAUSE.request received. Staying in MATCHED, PLC chip stays powered on (low power mode "
                        "optional in -3)");
}

everest::lib::slac::MacAddress SlacRuntime::evse_mac() const {
    std::lock_guard<std::mutex> guard(diagnostics_mutex);
    return captured_mac;
}

bool SlacRuntime::amp_map_initiation_enabled() const {
    std::lock_guard<std::mutex> guard(diagnostics_mutex);
    return amp_map_enabled;
}

} // namespace module::main
