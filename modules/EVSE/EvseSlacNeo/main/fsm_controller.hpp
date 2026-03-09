// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#ifndef EVSE_SLAC_FSM_CONTROLLER_HPP
#define EVSE_SLAC_FSM_CONTROLLER_HPP

#include "everest/slac/slac_fsm.hpp"
#include <everest/slac/fsm/evse/fsm.hpp>

#include <condition_variable>
#include <mutex>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>

using namespace everest::lib;

class FSMController : public io::event::fd_event_register_interface {
public:
    explicit FSMController(slac::fsm::evse::Context& ctx);

    void signal_new_slac_message(slac::messages::HomeplugMessage const&);
    void signal_reset();
    bool signal_enter_bcd();
    bool signal_leave_bcd();

    void run();

    void init();

    bool register_events(io::event::fd_event_handler& handler) override;
    bool unregister_events(io::event::fd_event_handler& handler) override;

private:
    using event_fd = io::event::event_fd;
    using timer_fd = io::event::timer_fd;

    void handle_retrigger();
    void handle_reset();
    void handle_enter_bcd();
    void handle_leave_bcd();

    slac::fsm::evse::Context& ctx;
    //    slac::fsm::evse::FSM fsm;
    slac::slac_fsm fsm;

    event_fd m_reset;
    event_fd m_enter_bcd;
    event_fd m_leave_bcd;
    //    event_fd m_feed;
    timer_fd m_retrigger;
};

#endif // EVSE_SLAC_FSM_CONTROLLER_HPP
