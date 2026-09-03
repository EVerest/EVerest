// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MAIN_LINK_STATE_MACHINE_HPP
#define MAIN_LINK_STATE_MACHINE_HPP

#include <memory>
#include <string>
#include <vector>

namespace module {
namespace main {

/// The states types/slac.yaml publishes. The machine's internal states are richer (see
/// \ref internal_state); this is what a consumer of the slac interface gets to see.
enum class link_state {
    unmatched,
    matching,
    matched,
};

/// The machine's own states. MATCHING/MATCHED/UNMATCHED do not distinguish "paused" from
/// "up" or "waiting out the restart guard" from "idle", but the behaviour differs, so these
/// exist separately and are only mapped to \ref link_state when published.
enum class internal_state {
    /// No data link and none being established. Published UNMATCHED.
    unmatched,
    /// EV present per basic signalling, waiting for the link. TT_EV_link_detect runs.
    matching,
    /// Link up and D-LINK_READY(true) issued. Published MATCHED.
    matched,
    /// D-LINK_PAUSE received (V2G10-041). Still published MATCHED, and carrier loss here is
    /// expected rather than a failure - the PHY is allowed to power down in B0. Carrier and
    /// liveness supervision are therefore suspended, so the machine leaves this state again on the
    /// first evidence that the session resumed (a carrier edge, or a neighbour answering).
    paused,
    /// D-LINK_ERROR received and a retry is budgeted: waiting out the >= 3 s guard of
    /// IEC 61851-23-3 CC.5.2.3.2 before asking for the restart routine. Published UNMATCHED.
    retry_wait,
};

/// The two timeouts the machine asks its owner to run for it.
enum class timer_id {
    /// TT_EV_link_detect, ISO 15118-10 Table 8 (max 4 s).
    link_detect,
    /// The >= 3 s wait between restart attempts, IEC 61851-23-3 CC.5.2.3.2.
    retry_wait,
    /// TT_sync_repetition, ISO 15118-10 Table 8 (max 4 s). The window opened at the communication
    /// initialization trigger, within which a FAILED initialization may be restarted (V2G10-055
    /// to -058).
    sync_repetition,
};

/// One thing the machine wants done. Actions never perform I/O themselves - they append effects
/// and the owner executes them after the event has been processed. That keeps the machine a pure
/// function of its inputs and makes every transition testable without a socket or a timer.
struct effect {
    enum class kind {
        /// Publish the `state` variable of the slac interface.
        publish_state,
        /// Publish `dlink_ready`.
        publish_dlink_ready,
        /// Publish `request_error_routine` (asks EvseManager for the CP/CE restart sequence).
        publish_request_error_routine,
        /// Publish `ev_mac_address`.
        publish_ev_mac,
        /// Arm \ref timer with \ref timeout_ms, single shot.
        start_timer,
        /// Disarm \ref timer.
        stop_timer,
    };

    kind what{kind::publish_state};
    /// Valid for publish_state.
    link_state state{link_state::unmatched};
    /// Valid for publish_dlink_ready.
    bool ready{false};
    /// Valid for start_timer and stop_timer.
    timer_id timer{timer_id::link_detect};
    /// Valid for start_timer.
    int timeout_ms{0};
    /// Valid for publish_ev_mac.
    std::string mac;
};

/// Tunables from the module config, copied in once at construction.
struct link_config {
    /// C_conn_retry: automatic data link restarts per EV connection. 0 disables retrying.
    int conn_retry_max{3};
    /// TT_EV_link_detect in milliseconds.
    int link_detect_timeout_ms{4000};
    /// TT_sync_repetition in milliseconds: the window from the communication initialization
    /// trigger within which a FAILED initialization is restarted. 0 disables repetition.
    int sync_repetition_ms{4000};
    /// Wait before requesting a restart after D-LINK_ERROR, in milliseconds.
    int retry_wait_ms{3000};
    /// Whether ev_mac_address may be published at all.
    bool publish_ev_mac{true};
};

/// The MCS data link lifecycle as a boost::msm state machine.
///
/// The boost headers stay inside link_state_machine.cpp: this class is the seam, so no other
/// translation unit pays the MSM compile cost and none of them can be affected by the
/// boost::mpl sizing macros the transition table needs.
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

    /// reset(enable) command: tear the data link down and return to UNMATCHED with a fresh retry
    /// budget. `enable` is accepted and logged but deliberately does not latch matching off - see
    /// the note above the transition table in link_state_machine.cpp for why.
    void reset(bool enable);
    /// enter_bcd command: EV detected by basic signalling. \p carrier_up is the current carrier
    /// level, because SPE is point to point and the PHY can be operational before plug-in - per
    /// V2G10-023 D-LINK_READY needs state B *and* link up, in either order.
    void enter_bcd(bool carrier_up);
    /// leave_bcd command: EV gone.
    void leave_bcd();
    /// The netdev carrier came up (rtnetlink IFF_LOWER_UP set). Edge, not level.
    void carrier_up();
    /// The netdev carrier went down, or the device was deleted. Edge, not level.
    void carrier_down();
    /// The link was detected as lost by a means other than carrier (neighbour liveness). Handled
    /// like carrier loss per V2G10-036.
    void link_lost();
    /// TT_EV_link_detect expired: communication initialization FAILED (V2G10-054).
    /// \p may_repeat is whether TT_sync_repetition is still open, which together with the retry
    /// budget decides whether the initialization is restarted (V2G10-056) or stopped (V2G10-058).
    void link_detect_timeout(bool may_repeat);
    /// The CC.5.2.3.2 inter-attempt wait expired.
    void retry_wait_elapsed(bool carrier_up);
    /// dlink_error command from the HLC.
    void dlink_error();
    /// dlink_terminate command from the HLC.
    void dlink_terminate();
    /// dlink_pause command from the HLC.
    void dlink_pause();
    /// A neighbour of the device became reachable, carrying \p mac.
    void neighbor_reachable(std::string mac);

    /// Effects produced since the last call, in the order they must be executed.
    std::vector<effect> take_effects();

    internal_state state() const;
    /// The published state, derived from \ref state.
    link_state published_state() const;
    /// Restart attempts consumed for the current EV connection.
    int retry_count() const;
    /// Whether D-LINK_READY(true) is currently outstanding.
    bool dlink_ready() const;
    /// Events that found no transition anywhere - neither in the link lifecycle nor among the
    /// session-wide events. Ignoring them is correct (dlink_pause while unmatched, a second
    /// enter_bcd, a carrier edge in a state that does not care); the counter exists so tests can
    /// assert that an event was ignored rather than silently mishandled.
    ///
    /// Not every event that does nothing is counted here. An event a state deliberately *consumes*
    /// without acting - currently only a repeated dlink_error while the restart guard is already
    /// running - took a transition, so it is not counted even though nothing observable happened.
    /// The counter measures "no rule matched", not "nothing happened".
    int ignored_events() const;

private:
    struct impl;
    std::unique_ptr<impl> m_impl;
};

char const* to_string(link_state value);
char const* to_string(internal_state value);
char const* to_string(timer_id value);
char const* to_string(effect::kind value);

} // namespace main
} // namespace module

#endif // MAIN_LINK_STATE_MACHINE_HPP
