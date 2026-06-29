// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>

#include <everest/slac/ev_slac_fsm.hpp>
#include <everest/slac/fsm/ev/context.hpp>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>

using namespace everest::lib;
namespace slac_fsm = everest::lib::slac::fsm;

class FSMController : public io::event::fd_event_register_interface {
public:
    explicit FSMController(slac_fsm::ev::Context& ctx);

    void signal_new_slac_message(slac::messages::HomeplugMessage const& msg);
    void signal_reset();
    bool signal_trigger_matching();
    void init();
    void stop();
    void run();

    bool register_events(io::event::fd_event_handler& handler) override;
    bool unregister_events(io::event::fd_event_handler& handler) override;

private:
    using event_fd = io::event::event_fd;
    using timer_fd = io::event::timer_fd;

    void handle_retrigger();
    void handle_reset();
    void handle_trigger_matching();

    slac_fsm::ev::Context& ctx;
    slac::ev_slac_fsm fsm;
    std::atomic_bool active{false};

    event_fd m_reset;
    event_fd m_trigger_matching;
    timer_fd m_retrigger;
};
