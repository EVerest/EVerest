// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <functional>
#include <string>

#include <everest/slac/ev_slac_fsm.hpp>
#include <everest/slac/fsm/ev/context.hpp>

#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>

using namespace everest::lib;
namespace slac_fsm = everest::lib::slac::fsm;

class FSMController : public io::event::fd_event_register_interface {
public:
    using FatalHandler = std::function<void(std::string const& reason)>;

    explicit FSMController(slac_fsm::ev::Context& ctx);

    // Loop thread only: the SLAC I/O receive callback already runs there, with the lifecycle monitor
    // held. Unlike the posted commands this does not report through the fatal handler, which takes
    // that monitor: a throw out of the machine's publishers or a timer that cannot be armed
    // propagates to the caller, and the loop's catch handler aborts the loop once the dispatch has
    // released the monitor.
    void signal_new_slac_message(slac::messages::HomeplugMessage const& msg);

    // Any thread. The FSM is not thread-safe and the event loop drives it from socket receives and
    // its deadline timer, so these hand the event to the loop through fd_event_handler::add_action,
    // the one member of the handler that is safe to call from another thread; its task queue is
    // FIFO. Dropped (false) while the controller is stopped or not registered with a handler.
    [[nodiscard]] bool signal_reset();
    [[nodiscard]] bool signal_trigger_matching();

    // Called on the loop thread when a posted command or the deadline timer threw out of the machine
    // or the timer could not be re-armed afterwards. (Any callback the machine calls that throws,
    // publisher, send or logger, is parked by the machine's context and rethrown by its wrapper once
    // the event is done, so it arrives here as the event's failure.) The event handler swallows
    // exceptions from posted actions, so without this the failure would be invisible. The
    // receive path is not covered: it runs under the lifecycle monitor and propagates instead.
    // The controller is still active when the handler runs: the handler owns the teardown (reset path
    // for the consumer, then stop()). Without a handler the controller stops itself and logs.
    void set_fatal_handler(FatalHandler handler);

    // Starts the machine and arms the timer for its first deadline. False if the timer could not be
    // armed. Idempotent: a second call returns true without restarting.
    [[nodiscard]] bool init();
    void stop();
    // Loop thread only: run the reset path (publishes UNMATCHED / dlink_ready(false)) then stop.
    void teardown();

    bool register_events(io::event::fd_event_handler& handler) override;
    bool unregister_events(io::event::fd_event_handler& handler) override;

private:
    using timer_fd = io::event::timer_fd;

    // Queue \p task on the loop thread; false if not registered with a handler or not active.
    bool post(char const* command, std::function<void()> task);
    // Loop thread: run \p task on the machine, then arm the timer for the machine's next deadline.
    // Throws if either fails.
    void step(std::function<void()> const& task);
    // Loop thread, no lifecycle monitor held: step(), with a throw or a timer failure reported
    // through the fatal handler, naming \p command.
    void run_guarded(char const* command, std::function<void()> const& task);
    bool schedule();
    void handle_retrigger();

    slac_fsm::ev::Context& ctx;
    slac::ev_slac_fsm fsm;
    std::atomic_bool active{false};
    // The handler this controller is registered with; set in register_events, cleared in
    // unregister_events, both of which run on the loop thread. Guarded for the cross-thread readers
    // in signal_*.
    std::atomic<io::event::fd_event_handler*> m_handler{nullptr};
    timer_fd m_retrigger;
    FatalHandler m_fatal_handler;
};
