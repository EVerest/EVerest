// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MAIN_DATALINK_CONTROLLER_HPP
#define MAIN_DATALINK_CONTROLLER_HPP

#include <deque>
#include <functional>
#include <string>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/netlink/device_watcher.hpp>
#include <everest/io/netlink/peer_liveness.hpp>
#include <everest/util/async/monitor.hpp>

#include "link_state_machine.hpp"

namespace module {
namespace main {

/// Everything the module does at runtime, wired onto one fd_event_handler.
///
/// Threading: the state machine, the liveness judgement and the timers are touched by exactly one
/// thread - the one running the event loop in ev_slacImpl::ready(). Interface commands arrive on
/// framework threads and are only ever appended to \ref m_commands and signalled through an
/// event_fd; they never reach the machine directly. That is the whole synchronisation story.
///
/// The state machine performs no I/O. It returns effect requests, and \ref run_effects is the one
/// place that turns them into publishes and timer operations.
class datalink_controller : public everest::lib::io::event::fd_event_register_interface {
public:
    struct config {
        std::string device{"cb_plc"};
        int link_detect_timeout_ms{4000};
        bool neighbor_liveness{true};
        int liveness_grace_ms{1000};
        bool publish_connector_mac{true};
    };

    struct callbacks {
        std::function<void(link_state)> publish_state;
        std::function<void(bool)> publish_dlink_ready;
        std::function<void(std::string const&)> publish_connector_mac;
        /// Raise the interface's generic/CommunicationFault with this message.
        std::function<void(std::string const&)> raise_fault;
        std::function<void()> clear_fault;
    };

    datalink_controller(config settings, callbacks handlers);
    ~datalink_controller() override;

    datalink_controller(datalink_controller const&) = delete;
    datalink_controller& operator=(datalink_controller const&) = delete;

    /// Open the rtnetlink socket. A failure is reported through \ref error and is not fatal to the
    /// event loop: commands are still processed, the link simply never comes up.
    bool open();
    int error() const;

    bool register_events(everest::lib::io::event::fd_event_handler& handler) override;
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override;

    /// Loop thread only. Publishes the initial interface state.
    void start();

    /// @name Interface commands
    /// Called on framework threads. They enqueue and wake the loop; nothing else.
    /// @{
    void post_reset();
    /// \return false when the command could not be accepted at all - see
    /// ev_slacImpl::handle_trigger_matching for what the interface's boolean can and cannot mean.
    bool post_trigger_matching();
    /// @}

    /// @name Test and diagnostics accessors (loop thread)
    /// @{
    link_state state() const;
    bool carrier_up() const;
    bool device_present() const;
    /// @}

private:
    enum class command_kind {
        reset,
        trigger_matching,
    };

    /// A single FIFO rather than one event_fd per command: the framework's ordering between
    /// different commands has to survive the hop onto the loop, and EvManager relies on it - it
    /// calls reset() and then trigger_matching(), and swapping them would leave the module
    /// unmatched with nothing pending.
    bool post(command_kind kind);
    void drain_commands();
    void apply(command_kind kind);

    /// Execute what the machine asked for, in order. The only I/O the machine causes.
    void run_effects();

    void on_carrier_change(bool up);
    void on_presence_change(bool present);
    void on_neighbor(everest::lib::io::netlink::neighbor_report const& report);
    void on_initial_state();
    void on_liveness_grace();
    void on_link_detect_timeout();
    void on_watcher_error(std::string const& reason);

    void forget_neighbors();
    void arm_liveness_grace();
    void cancel_liveness_grace();
    void raise_fault(std::string const& message);
    void clear_fault();

    config m_config;
    callbacks m_callbacks;

    link_state_machine m_fsm;
    everest::lib::io::netlink::device_watcher m_watcher;
    everest::lib::io::netlink::peer_liveness m_neighbors;

    everest::lib::io::event::timer_fd m_link_detect_timer;
    everest::lib::io::event::timer_fd m_liveness_grace_timer;
    everest::lib::io::event::event_fd m_command_event;
    everest::lib::util::monitor<std::deque<command_kind>> m_commands;

    bool m_liveness_grace_armed{false};
    bool m_fault_raised{false};
    std::string m_fault_message;
};

} // namespace main
} // namespace module

#endif // MAIN_DATALINK_CONTROLLER_HPP
