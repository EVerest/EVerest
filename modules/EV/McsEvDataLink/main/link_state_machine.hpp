// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MAIN_LINK_STATE_MACHINE_HPP
#define MAIN_LINK_STATE_MACHINE_HPP

#include <memory>
#include <string>
#include <vector>

namespace module {
namespace main {

/// The states types/slac.yaml publishes; `ev_slac` has no pause/error commands, so no internal states behind them.
enum class link_state {
    unmatched,
    matching,
    matched,
};

/// An I/O request from the machine. Actions append effects; the owner executes them after the event.
struct effect {
    enum class kind {
        publish_state,
        publish_dlink_ready,
        /// Publish `ev_mac_address`; on this side the peer is the SECC, so it is the connector's MAC.
        publish_connector_mac,
        /// Arm the communication-setup deadline with \ref timeout_ms, single shot.
        start_timer,
        stop_timer,
    };

    kind what{kind::publish_state};
    /// Payload; the valid field depends on \c what.
    link_state state{link_state::unmatched};
    bool ready{false};
    int timeout_ms{0};
    std::string mac;
};

/// Module config, copied once at construction.
struct link_config {
    /// The communication-setup deadline in milliseconds: TT_EV_link_detect / T_conn_resume.
    int link_detect_timeout_ms{4000};
    /// Whether ev_mac_address may be published at all.
    bool publish_connector_mac{true};
};

/// The EV-side MCS data link lifecycle as a boost::msm state machine (boost stays in the .cpp). Methods
/// are events, do no I/O and run on one thread; call \ref take_effects afterwards and execute in order.
class link_state_machine {
public:
    explicit link_state_machine(link_config config);
    ~link_state_machine();

    link_state_machine(link_state_machine const&) = delete;
    link_state_machine& operator=(link_state_machine const&) = delete;
    link_state_machine(link_state_machine&&) = delete;
    link_state_machine& operator=(link_state_machine&&) = delete;

    /// Enter the initial state and produce the first publish_state effect.
    void start();

    /// reset command: drop any link and return to UNMATCHED.
    void reset();

    /// trigger_matching command. \p carrier_up is the current carrier level: the PHY may already be up
    /// (V2G10-028) and V2G10-030 accepts link and basic signalling in either order.
    void trigger_matching(bool carrier_up);

    /// The netdev carrier came up (rtnetlink IFF_LOWER_UP set). Edge, not level.
    void carrier_up();
    /// The netdev carrier went down, or the device was deleted. Edge, not level.
    void carrier_down();
    /// Link loss detected by neighbour liveness rather than carrier; handled like carrier loss (V2G10-036).
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
    /// Events with no matching transition (ignored by design); for test assertions.
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
