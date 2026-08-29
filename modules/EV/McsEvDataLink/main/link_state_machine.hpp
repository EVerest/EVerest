// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MAIN_LINK_STATE_MACHINE_HPP
#define MAIN_LINK_STATE_MACHINE_HPP

#include <memory>
#include <string>
#include <vector>

namespace module {
namespace main {

/// The states types/slac.yaml publishes, which on the EV side are also the machine's only states.
///
/// The EVSE-side module needs richer internal states behind these three - a paused state that is
/// still published MATCHED, a restart-guard state that is published UNMATCHED - because the `slac`
/// interface gives it dlink_pause and dlink_error to react to. `ev_slac` has neither, so there is
/// nothing here that behaves differently from the state it is published as, and no second enum.
enum class link_state {
    unmatched,
    matching,
    matched,
};

/// One thing the machine wants done. Actions never perform I/O themselves - they append effects
/// and the owner executes them after the event has been processed. That keeps the machine a pure
/// function of its inputs and makes every transition testable without a socket or a timer.
struct effect {
    enum class kind {
        /// Publish the `state` variable of the ev_slac interface.
        publish_state,
        /// Publish `dlink_ready`.
        publish_dlink_ready,
        /// Publish `ev_mac_address` - on this side the peer is the SECC, so it carries the
        /// charging connector's MAC address.
        publish_connector_mac,
        /// Arm the communication-setup deadline with \ref timeout_ms, single shot.
        start_timer,
        /// Disarm it.
        stop_timer,
    };

    kind what{kind::publish_state};
    /// Valid for publish_state.
    link_state state{link_state::unmatched};
    /// Valid for publish_dlink_ready.
    bool ready{false};
    /// Valid for start_timer.
    int timeout_ms{0};
    /// Valid for publish_connector_mac.
    std::string mac;
};

/// Tunables from the module config, copied in once at construction.
struct link_config {
    /// The communication-setup deadline in milliseconds: TT_EV_link_detect / T_conn_resume.
    int link_detect_timeout_ms{4000};
    /// Whether ev_mac_address may be published at all.
    bool publish_connector_mac{true};
};

/// The EV-side MCS data link lifecycle as a boost::msm state machine.
///
/// The boost headers stay inside link_state_machine.cpp: this class is the seam, so no other
/// translation unit pays the MSM compile cost.
///
/// Every method below is an event. None of them does I/O; call \ref take_effects afterwards and
/// execute what comes back, in order. All of it must run on one thread (the event loop).
class link_state_machine {
public:
    explicit link_state_machine(link_config config);
    ~link_state_machine();

    link_state_machine(link_state_machine const&) = delete;
    link_state_machine& operator=(link_state_machine const&) = delete;
    link_state_machine(link_state_machine&&) = delete;
    link_state_machine& operator=(link_state_machine&&) = delete;

    /// Enter the initial state. Produces the first publish_state effect, so the interface has a
    /// defined value before any command arrives.
    void start();

    /// reset command: drop any link and return to UNMATCHED, ready to be triggered again.
    void reset();

    /// trigger_matching command: the EV stack wants the data link now. \p carrier_up is the
    /// current carrier level, because on a point-to-point SPE link the PHY is often already
    /// operational by the time the stack asks - V2G10-028 has the EV establish the link right
    /// after plug-in detection, before state B. Per V2G10-030, D-LINK_READY needs the link *and*
    /// the basic-signalling condition, in either order, and this command is the latter.
    void trigger_matching(bool carrier_up);

    /// The netdev carrier came up (rtnetlink IFF_LOWER_UP set). Edge, not level.
    void carrier_up();
    /// The netdev carrier went down, or the device was deleted. Edge, not level.
    void carrier_down();
    /// The link was detected as lost by a means other than carrier (neighbour liveness). Handled
    /// like carrier loss per V2G10-036.
    void link_lost();
    /// The communication-setup deadline expired: initialization FAILED (V2G10-054).
    void link_detect_timeout();
    /// A neighbour of the device became reachable, carrying \p mac.
    void neighbor_reachable(std::string mac);

    /// Effects produced since the last call, in the order they must be executed.
    std::vector<effect> take_effects();

    link_state state() const;
    /// Whether D-LINK_READY(true) is currently outstanding.
    bool dlink_ready() const;
    /// Events that found no transition in the state they were delivered to. Ignoring them is
    /// correct (a second trigger_matching, a carrier edge in a state that does not care); the
    /// counter exists so tests can assert that an event was ignored rather than silently
    /// mishandled. It measures "no rule matched", not "nothing happened".
    int ignored_events() const;

private:
    struct impl;
    std::unique_ptr<impl> m_impl;
};

char const* to_string(link_state value);
char const* to_string(effect::kind value);

} // namespace main
} // namespace module

#endif // MAIN_LINK_STATE_MACHINE_HPP
