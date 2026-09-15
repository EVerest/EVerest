// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/slac/EvseSlacConfig.hpp>
#include <everest/slac/fsm/evse/context.hpp>
#include <everest/util/async/monitor.hpp>

#include "fsm_controller.hpp"
#include "lifecycle_gate.hpp"
#include "slac_io.hpp"
#include "slac_sink.hpp"

namespace module::main {

struct SlacRuntimeConfig {
    std::string device;
    std::chrono::milliseconds startup_delay{0};
    bool publish_mac_on_first_parm_req{false};
    bool publish_mac_on_match_cnf{false};
    bool telemetry_enabled{false};
    // CM_AMP_MAP transmit-power limitation: SECC-initiated direction and its operator map. An empty
    // file means the built-in no-reduction map; a file that cannot be applied exactly disables the
    // initiation and raises generic/VendorError (fail closed).
    bool initiate_amp_map{false};
    std::string amp_map_file;
    // How long init() drives the event loop waiting for the first I/O ready (or error) before
    // giving up with a CommunicationFault. Bring-up normally settles within milliseconds.
    std::chrono::milliseconds io_bring_up_timeout{10000};
    // How long shutdown() waits for the event loop in ready() to return before leaking the
    // objects it may still be using.
    std::chrono::milliseconds loop_exit_timeout{5000};
    // Everything the state machine reads. The runtime fills in the amplitude map, the interface
    // MAC, the telemetry flag and the session NMK.
    everest::lib::slac::fsm::evse::EvseSlacConfig slac{};
};

// The EvseSlac module without the framework: owns the PLC I/O, the event loop, the state machine
// controller and the lifecycle handshake between init(), ready(), shutdown() and the interface
// commands. The module is a thin adapter around it; tests drive it with a fake SlacIo and a
// recording SlacSink.
//
// Threads: init() and shutdown() run on framework threads, ready() blocks on the thread that calls
// it and runs the event loop there, the commands come from framework threads. Everything the state
// machine touches runs on the loop thread.
class SlacRuntime {
public:
    SlacRuntime(SlacRuntimeConfig config, SlacIoFactory io_factory, SlacSink& sink);
    // Calls shutdown(); idempotent with the framework's call.
    ~SlacRuntime();

    SlacRuntime(SlacRuntime const&) = delete;
    SlacRuntime& operator=(SlacRuntime const&) = delete;

    // Framework lifecycle.
    void init();
    void ready();
    void shutdown();

    // Interface commands, any thread. False when the command did not reach the state machine; the
    // runtime has logged why. A caller that cannot pass the result on discards it explicitly.
    [[nodiscard]] bool reset(bool enable);
    [[nodiscard]] bool enter_bcd();
    [[nodiscard]] bool leave_bcd();
    void count_bc(int count);
    [[nodiscard]] bool dlink_terminate();
    [[nodiscard]] bool dlink_error();
    void dlink_pause();

    // Diagnostics and tests.
    everest::lib::slac::MacAddress evse_mac() const;
    bool amp_map_initiation_enabled() const;

private:
    using LifecycleState = LifecycleStateT<FSMController>;

    bool wait_for_startup_delay_or_shutdown();
    bool initialize_slac_io();
    void configure_callbacks();
    void configure_fsm_context();
    bool create_fsm_controller();
    void configure_slac_io_callbacks();
    bool register_event_handlers();
    void unregister_event_handlers();
    void run_bring_up_loop();
    void run_event_loop();
    void handle_slac_io_ready();
    void handle_slac_io_error(bool on_error, std::string const& detail);
    void start_fsm_if_ready();
    [[nodiscard]] bool post_command(char const* command, std::function<bool(FSMController&)> const& post);
    void raise_communication_fault(std::string const& message);
    void clear_communication_fault();
    void abort_event_loop(std::string const& reason);
    void stop_and_publish_unmatched_by_hand(FSMController& target);
    void leak_loop_objects();
    void log(LogLevel level, std::string const& text);

    SlacRuntimeConfig config;
    SlacIoFactory io_factory;
    SlacSink& sink;

    std::atomic<bool> online{true};
    std::atomic<bool> bring_up_pending{false};
    everest::lib::io::event::event_fd exit_event;
    everest::lib::io::event::timer_fd bring_up_timer;
    everest::lib::io::event::fd_event_handler event_handler;
    everest::lib::util::monitor<LifecycleState> lifecycle_state;
    everest::lib::slac::fsm::evse::ContextCallbacks callbacks;
    std::unique_ptr<everest::lib::slac::fsm::evse::Context> fsm_ctx;
    std::unique_ptr<SlacIo> slac_io;
    std::unique_ptr<FSMController> fsm_ctrl;
    mutable std::mutex diagnostics_mutex;
    everest::lib::slac::MacAddress captured_mac{};
    bool amp_map_enabled{false};
    // Whether the consumer may currently believe the state is not UNMATCHED, or that the link is
    // up. Set before such a publication is attempted (a sink that throws may still have delivered),
    // cleared once UNMATCHED / dlink_ready(false) was handed over without a throw. The fatal path
    // publishes by hand whatever the machine's own reset left standing. Loop thread only.
    bool consumer_may_be_matched{false};
    bool consumer_may_have_link{false};
};

} // namespace module::main
