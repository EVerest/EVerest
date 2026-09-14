// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MAIN_LINK_STATE_MACHINE_HPP
#define MAIN_LINK_STATE_MACHINE_HPP

#include <memory>
#include <string>
#include <vector>

namespace module {
namespace main {

/// The states published on the slac interface. \ref internal_state is the finer internal set.
enum class link_state {
    unmatched,
    matching,
    matched,
};

/// Internal states; mapped to \ref link_state when published.
enum class internal_state {
    /// No data link and none being established. Published UNMATCHED.
    unmatched,
    /// EV present per basic signalling, waiting for the link. TT_EV_link_detect runs.
    matching,
    /// Link up and D-LINK_READY(true) issued. Published MATCHED.
    matched,
    /// D-LINK_PAUSE received (V2G10-041). Published MATCHED. Carrier and liveness supervision are
    /// suspended because the PHY may power down in B0; left on a carrier edge or a reachable neighbour.
    paused,
    /// D-LINK_ERROR or final initialization FAILED with a retry budgeted: waiting out the >= 3 s guard
    /// of IEC 61851-23-3 CC.5.2.3.2 before requesting the restart routine. Published UNMATCHED.
    retry_wait,
};

/// Timers the owner runs for the machine.
enum class timer_id {
    /// TT_EV_link_detect, ISO 15118-10 Table 8 (max 4 s).
    link_detect,
    /// The >= 3 s wait between restart attempts, IEC 61851-23-3 CC.5.2.3.2.
    retry_wait,
    /// TT_sync_repetition, ISO 15118-10 Table 8 (max 4 s): window from the communication initialization
    /// trigger within which a FAILED initialization may be restarted (V2G10-055 to -058).
    sync_repetition,
};

/// One requested side effect. The machine does no I/O; the owner executes the effects after the event.
struct effect {
    enum class kind {
        publish_state,
        publish_dlink_ready,
        /// Publish `request_error_routine` (asks EvseManager for the CP/CE restart sequence).
        publish_request_error_routine,
        publish_ev_mac,
        /// Arm \ref timer with \ref timeout_ms, single shot.
        start_timer,
        stop_timer,
    };

    kind what{kind::publish_state};
    link_state state{link_state::unmatched};
    bool ready{false};
    timer_id timer{timer_id::link_detect};
    int timeout_ms{0};
    std::string mac;
};

/// Module config tunables, copied at construction.
struct link_config {
    /// C_conn_retry: automatic data link restarts per EV connection. 0 disables retrying.
    int conn_retry_max{3};
    /// TT_EV_link_detect in milliseconds.
    int link_detect_timeout_ms{4000};
    /// TT_sync_repetition in milliseconds. 0 disables repetition.
    int sync_repetition_ms{4000};
    /// CC.5.2.3.2 wait before requesting a restart, in milliseconds.
    int retry_wait_ms{3000};
    /// Whether ev_mac_address is published.
    bool publish_ev_mac{true};
};

/// The MCS data link lifecycle as a boost::msm state machine; the boost headers stay in the .cpp.
///
/// Every method is an event and does no I/O: call \ref take_effects afterwards and execute the
/// effects in order. Single threaded (the event loop).
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

    /// Tear the link down, return to UNMATCHED with a fresh retry budget. `enable` is ignored (see the
    /// outer transition table). The first reset after a request_error_routine is absorbed.
    void reset(bool enable);
    /// enter_bcd command: EV detected by basic signalling. \p carrier_up is the current carrier level;
    /// the PHY may be up before plug-in, and V2G10-023 needs state B and link up in either order.
    void enter_bcd(bool carrier_up);
    /// leave_bcd command: EV gone.
    void leave_bcd();
    /// The netdev carrier came up (rtnetlink IFF_LOWER_UP set). Edge, not level.
    void carrier_up();
    /// The netdev carrier went down, or the device was deleted. Edge, not level.
    void carrier_down();
    /// Link lost by neighbour liveness rather than carrier. Handled like carrier loss (V2G10-036).
    void link_lost();
    /// TT_EV_link_detect expired: communication initialization FAILED (V2G10-054). \p may_repeat is
    /// whether TT_sync_repetition is open: repeat (V2G10-056), else CC.5.2.3.2 restart (V2G10-058)
    /// with budget left, else stop.
    void link_detect_timeout(bool may_repeat);
    /// The CC.5.2.3.2 inter-attempt wait expired.
    void retry_wait_elapsed(bool carrier_up);
    void dlink_error();
    void dlink_terminate();
    void dlink_pause();
    /// A neighbour of the device became reachable, carrying \p mac.
    void neighbor_reachable(std::string mac);

    /// Effects produced since the last call, in the order they must be executed.
    std::vector<effect> take_effects();

    internal_state state() const;
    link_state published_state() const;
    /// Restart attempts consumed for the current EV connection.
    int retry_count() const;
    /// Whether D-LINK_READY(true) is currently outstanding.
    bool dlink_ready() const;
    /// Events that matched no row in either machine (for tests). Events consumed by a row without
    /// effect (repeated dlink_error in retry_wait, the absorbed routine reset) are not counted.
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
