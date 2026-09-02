// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <functional>

#include <everest/slac/ev_slac_fsm.hpp>
#include <everest/slac/fsm/ev/context.hpp>

#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>

using namespace everest::lib;
namespace slac_fsm = everest::lib::slac::fsm;

class FSMController : public io::event::fd_event_register_interface {
public:
    explicit FSMController(slac_fsm::ev::Context& ctx);

    // Loop thread only: the SLAC I/O receive callback already runs there.
    void signal_new_slac_message(slac::messages::HomeplugMessage const& msg);
    // Any thread. The FSM is not thread-safe and the event loop drives it from socket receives and
    // the retrigger timer, so these hand the event to the loop through fd_event_handler::add_action,
    // the one member of the handler that is safe to call from another thread; its task queue is
    // FIFO. Dropped (false) while the controller is stopped or not registered with a handler.
    void signal_reset();
    bool signal_trigger_matching();
    void init();
    void stop();

    bool register_events(io::event::fd_event_handler& handler) override;
    bool unregister_events(io::event::fd_event_handler& handler) override;

private:
    using timer_fd = io::event::timer_fd;

    // Queue \p task on the loop thread; false if not registered with a handler or not active.
    bool post(std::function<void()> task);

    void handle_retrigger();
    void handle_reset();
    void handle_trigger_matching();

    slac_fsm::ev::Context& ctx;
    slac::ev_slac_fsm fsm;
    std::atomic_bool active{false};

    // The handler this controller is registered with; set in register_events, cleared in
    // unregister_events, both of which run on the loop thread. Guarded for the cross-thread readers
    // in signal_*.
    std::atomic<io::event::fd_event_handler*> m_handler{nullptr};
    timer_fd m_retrigger;
};
