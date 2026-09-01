// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#ifndef EVSE_SLAC_FSM_CONTROLLER_HPP
#define EVSE_SLAC_FSM_CONTROLLER_HPP

#include "slac_backend.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>

using namespace everest::lib;

class FSMController : public io::event::fd_event_register_interface {
public:
    explicit FSMController(slac_backend::Context& ctx);

    void signal_new_slac_message(slac_backend::HomeplugMessage const&);
    void signal_reset();
    bool signal_enter_bcd();
    bool signal_leave_bcd();
    // Thread safe, and needs no event-loop hop: the backend either stores it atomically or hands it
    // back through a callback when it is needed.
    void signal_count_bc(int count);

    void run();

    void init();
    void stop();
    void teardown();

    bool register_events(io::event::fd_event_handler& handler) override;
    bool unregister_events(io::event::fd_event_handler& handler) override;

private:
    using event_fd = io::event::event_fd;
    using timer_fd = io::event::timer_fd;

    void handle_retrigger();
    void handle_reset();
    void handle_enter_bcd();
    void handle_leave_bcd();

    slac_backend::Context& ctx;
    slac_backend::Fsm fsm;
    std::atomic_bool active{false};

    event_fd m_reset;
    event_fd m_enter_bcd;
    event_fd m_leave_bcd;
    timer_fd m_retrigger;
};

#endif // EVSE_SLAC_FSM_CONTROLLER_HPP
