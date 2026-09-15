// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2026 Pionix GmbH and Contributors to EVerest
#ifndef EVSE_SLAC_FSM_CONTROLLER_HPP
#define EVSE_SLAC_FSM_CONTROLLER_HPP

#include "everest/slac/slac_fsm.hpp"
#include <atomic>
#include <functional>
#include <string>

#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>

using namespace everest::lib;

class FSMController : public io::event::fd_event_register_interface {
public:
    using FatalHandler = std::function<void(std::string const& reason)>;

    explicit FSMController(slac::fsm::evse::Context& ctx);

    // Loop thread only: the SLAC I/O receive callback already runs there.
    void signal_new_slac_message(slac::messages::HomeplugMessage const&);

    // Any thread. The FSM is not thread-safe and the event loop drives it from socket receives and
    // the retrigger timer, so these hand the event to the loop through fd_event_handler::add_action,
    // the one member of the handler that is safe to call from another thread. Its task queue is FIFO,
    // so an enter_bcd followed by a leave_bcd between two polls is seen in that order. Dropped (false)
    // while the controller is stopped or not registered with a handler.
    [[nodiscard]] bool signal_reset();
    [[nodiscard]] bool signal_enter_bcd();
    [[nodiscard]] bool signal_leave_bcd();

    // Push the running Control-Pilot B/C transition count (from EvseManager's count_bc command) into the
    // FSM context for CM_VALIDATE BCB-toggle detection. Thread-safe atomic write, no event-loop hop.
    void signal_count_bc(int count);

    // Called on the loop thread when a posted command threw out of the state machine. The handler
    // swallows exceptions from posted actions, so without this the failure would be invisible. The
    // controller is still active when the handler runs: the handler owns the teardown (reset path
    // for the consumer, then stop()). Without a handler the controller stops itself and logs.
    void set_fatal_handler(FatalHandler handler);

    // Starts the machine and arms the 10 ms tick that drives every timeout in it. False if the tick
    // timer could not be armed; the machine must not be considered running then.
    [[nodiscard]] bool init();
    void stop();
    void teardown();

    bool register_events(io::event::fd_event_handler& handler) override;
    bool unregister_events(io::event::fd_event_handler& handler) override;

private:
    using timer_fd = io::event::timer_fd;

    // Queue \p task on the loop thread; false if not registered with a handler or not active. The
    // task runs guarded: an exception stops the controller and reports through the fatal handler,
    // naming \p command.
    bool post(char const* command, std::function<void()> task);
    void run_guarded(char const* command, std::function<void()> const& task);
    void handle_retrigger();
    void handle_reset();
    void handle_enter_bcd();
    void handle_leave_bcd();

    slac::fsm::evse::Context& ctx;
    slac::slac_fsm fsm;
    std::atomic_bool active{false};
    // The handler this controller is registered with; set in register_events, cleared in
    // unregister_events, both of which run on the loop thread. Guarded for the cross-thread readers
    // in signal_*.
    std::atomic<io::event::fd_event_handler*> m_handler{nullptr};
    timer_fd m_retrigger;
    FatalHandler m_fatal_handler;
};

#endif // EVSE_SLAC_FSM_CONTROLLER_HPP
