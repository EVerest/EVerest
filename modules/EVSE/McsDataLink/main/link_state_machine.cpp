// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// boost::msm stays in this translation unit. Both tables are below the 20-row boost::mpl default;
// if one grows past it, raise BOOST_MPL_LIMIT_VECTOR_SIZE here, never in a header.

#include <boost/mpl/vector.hpp>
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/state_machine_def.hpp>

#include <optional>
#include <utility>

#include "link_state_machine.hpp"

namespace module {
namespace main {

namespace {

namespace msm = boost::msm;
namespace mpl = boost::mpl;

using msm::front::none;
using msm::front::Row;

// Events are named like the public API; the member functions emit them with an explicit main::
// qualification because class scope wins unqualified lookup.
struct reset {
    bool enable{true};
};
struct enter_bcd {
    bool carrier_up{false};
};
struct leave_bcd {};
struct carrier_up {};
struct carrier_down {};
struct link_lost {};
struct link_detect_timeout {
    /// TT_sync_repetition is still open (tracked by the owner).
    bool may_repeat{false};
};
struct retry_wait_elapsed {
    bool carrier_up{false};
};
struct dlink_error {};
struct dlink_terminate {};
struct dlink_pause {};
struct neighbor_reachable {
    std::string mac;
};

struct machine_data {
    link_config cfg{};
    std::vector<effect> effects{};
    internal_state current{internal_state::unmatched};
    std::optional<link_state> last_state{};
    bool ready{false};
    int retries{0};
    int ignored{0};
    std::string published_mac{};
    /// request_error_routine published; its reset(false) from Charger::request_error_sequence() not yet absorbed.
    bool routine_reset_pending{false};
    /// TT_sync_repetition started and not stopped by the machine. Expiry is only seen by the owner; a
    /// stop after expiry is harmless.
    bool sync_window_armed{false};

    // --- effect emitters -------------------------------------------------------------------

    void emit_state(link_state value) {
        if (last_state == value) {
            return;
        }
        last_state = value;
        effect item;
        item.what = effect::kind::publish_state;
        item.state = value;
        effects.push_back(std::move(item));
    }

    /// Unconditional: re-entering Matched after D-LINK_PAUSE re-issues D-LINK_READY (V2G10-042 wake-up).
    void emit_dlink_ready() {
        ready = true;
        effect item;
        item.what = effect::kind::publish_dlink_ready;
        item.ready = true;
        effects.push_back(std::move(item));
    }

    /// Emits dlink_ready(false) only if it is outstanding.
    void withdraw_dlink_ready() {
        if (not ready) {
            return;
        }
        ready = false;
        effect item;
        item.what = effect::kind::publish_dlink_ready;
        item.ready = false;
        effects.push_back(std::move(item));
    }

    void emit_error_routine() {
        effect item;
        item.what = effect::kind::publish_request_error_routine;
        effects.push_back(std::move(item));
    }

    void emit_ev_mac(std::string const& mac) {
        if (not cfg.publish_ev_mac or mac.empty() or mac == published_mac) {
            return;
        }
        published_mac = mac;
        effect item;
        item.what = effect::kind::publish_ev_mac;
        item.mac = mac;
        effects.push_back(std::move(item));
    }

    void forget_ev_mac() {
        published_mac.clear();
    }

    void start_timer(timer_id timer, int timeout_ms) {
        effect item;
        item.what = effect::kind::start_timer;
        item.timer = timer;
        item.timeout_ms = timeout_ms;
        effects.push_back(std::move(item));
    }

    void stop_timer(timer_id timer) {
        effect item;
        item.what = effect::kind::stop_timer;
        item.timer = timer;
        effects.push_back(std::move(item));
    }

    void open_sync_window() {
        if (cfg.sync_repetition_ms <= 0) {
            return;
        }
        sync_window_armed = true;
        start_timer(timer_id::sync_repetition, cfg.sync_repetition_ms);
    }

    /// Closed when the connection ends or a session-level restart takes over, so the window cannot
    /// leak into the next TT_EV_link_detect decision.
    void close_sync_window() {
        if (not sync_window_armed) {
            return;
        }
        sync_window_armed = false;
        stop_timer(timer_id::sync_repetition);
    }

    // --- retry budget ----------------------------------------------------------------------

    // C_conn_retry counts per EV connection; MATCHED does not refund attempts or a flapping link would
    // retry forever. Refilled by leave_bcd, reset and dlink_terminate.
    // OPEN: V2G10-052 read literally asks for that refund; Table 8 scopes C_conn_retry to the comm-init
    // phase. If conformance insists, refill on entry to Matched.
    bool retries_left() const {
        return retries < cfg.conn_retry_max;
    }

    void take_retry() {
        ++retries;
    }

    void refill_retries() {
        retries = 0;
    }
};

// ==========================================================================================
// Inner machine: the link lifecycle
// ==========================================================================================
//
// Session-wide events (reset, leave_bcd, dlink_terminate, dlink_error) are handled by the outer
// machine. Rows sharing source and event have mutually exclusive guards: msm does not resolve such
// conflicts in declaration order.
struct SessionDef : public msm::front::state_machine_def<SessionDef> {
    machine_data* d{nullptr};

    /// No link and none being established. Also reached when the retry budget is exhausted; nothing
    /// restarts on its own, EvseManager decides (V2G10-038).
    struct Unmatched : public msm::front::state<> {
        template <class Event, class FSM> void on_entry(Event const&, FSM& fsm) {
            fsm.d->current = internal_state::unmatched;
            fsm.d->forget_ev_mac();
            fsm.d->emit_state(link_state::unmatched);
        }
    };

    /// EV present per basic signalling, waiting for the link. TT_EV_link_detect runs.
    struct Matching : public msm::front::state<> {
        template <class Event, class FSM> void on_entry(Event const&, FSM& fsm) {
            fsm.d->current = internal_state::matching;
            fsm.d->forget_ev_mac();
            fsm.d->emit_state(link_state::matching);
            fsm.d->start_timer(timer_id::link_detect, fsm.d->cfg.link_detect_timeout_ms);
        }
        template <class Event, class FSM> void on_exit(Event const&, FSM& fsm) {
            fsm.d->stop_timer(timer_id::link_detect);
        }
    };

    /// Link up, D-LINK_READY outstanding.
    struct Matched : public msm::front::state<> {
        template <class Event, class FSM> void on_entry(Event const&, FSM& fsm) {
            fsm.d->current = internal_state::matched;
            fsm.d->emit_state(link_state::matched);
            fsm.d->emit_dlink_ready();
        }
    };

    /// D-LINK_PAUSE received. Nothing published: the link stays logically up (V2G10-041). Carrier loss
    /// is expected here. Left on carrier_up or neighbor_reachable; the latter is the realistic path
    /// since the LAN8650 low-power mode is unimplemented and the carrier stays up during a pause.
    struct Paused : public msm::front::state<> {
        template <class Event, class FSM> void on_entry(Event const&, FSM& fsm) {
            fsm.d->current = internal_state::paused;
        }
    };

    /// Waiting out the >= 3 s guard of IEC 61851-23-3 CC.5.2.3.2 before requesting the restart routine.
    /// Entered from Matching on a final FAILED initialization and from the outer machine on
    /// dlink_error (hence the explicit entry). No carrier row: the link returns via the B0-to-B restart.
    struct RestartWait : public msm::front::state<>, public msm::front::explicit_entry<0> {
        template <class Event, class FSM> void on_entry(Event const&, FSM& fsm) {
            fsm.d->current = internal_state::retry_wait;
            fsm.d->forget_ev_mac();
            fsm.d->emit_state(link_state::unmatched);
            fsm.d->start_timer(timer_id::retry_wait, fsm.d->cfg.retry_wait_ms);
        }
        template <class Event, class FSM> void on_exit(Event const&, FSM& fsm) {
            fsm.d->stop_timer(timer_id::retry_wait);
        }
    };

    using initial_state = Unmatched;

    // --- guards ----------------------------------------------------------------------------

    struct carrier {
        template <class EVT, class FSM, class Source, class Target>
        bool operator()(EVT const& evt, FSM&, Source&, Target&) {
            return evt.carrier_up;
        }
    };

    struct no_carrier {
        template <class EVT, class FSM, class Source, class Target>
        bool operator()(EVT const& evt, FSM&, Source&, Target&) {
            return not evt.carrier_up;
        }
    };

    struct retries_left {
        template <class EVT, class FSM, class Source, class Target>
        bool operator()(EVT const&, FSM& fsm, Source&, Target&) {
            return fsm.d->retries_left();
        }
    };

    struct no_retries {
        template <class EVT, class FSM, class Source, class Target>
        bool operator()(EVT const&, FSM& fsm, Source&, Target&) {
            return not fsm.d->retries_left();
        }
    };

    /// V2G10-056: window open and an attempt left. may_repeat, restart_after_failure and no_retries
    /// are exhaustive and mutually exclusive.
    struct may_repeat {
        template <class EVT, class FSM, class Source, class Target>
        bool operator()(EVT const& evt, FSM& fsm, Source&, Target&) {
            return evt.may_repeat and fsm.d->retries_left();
        }
    };

    /// V2G10-058 with budget left: the wake-up by basic signalling (C_conn_retry, Table 8) takes over.
    struct restart_after_failure {
        template <class EVT, class FSM, class Source, class Target>
        bool operator()(EVT const& evt, FSM& fsm, Source&, Target&) {
            return not evt.may_repeat and fsm.d->retries_left();
        }
    };

    // --- actions ---------------------------------------------------------------------------

    /// Final FAILED initialization (V2G10-058) with budget left: spend an attempt on the CC.5.2.3.2 restart.
    struct spend_init_retry {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->take_retry();
        }
    };

    /// Communication initialization trigger (V2G10-055): open the TT_sync_repetition window. Only on
    /// enter_bcd; a restart after link loss is governed by C_conn_retry, not a repetition.
    struct begin_comm_init {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->open_sync_window();
        }
    };

    /// V2G10-056: repeat the initialization within the window. Costs an attempt so it cannot loop forever.
    struct repeat_comm_init {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->take_retry();
        }
    };

    /// Link lost while up, budget left: D-LINK_READY(no link) (V2G10-036), then UNMATCHED is published
    /// before MATCHING so the consumer sees the link go down.
    struct restart_matching {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->withdraw_dlink_ready();
            fsm.d->emit_state(link_state::unmatched);
            fsm.d->take_retry();
        }
    };

    /// Budget exhausted: withdraw D-LINK_READY; EvseManager decides what follows (V2G10-038).
    struct give_up {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->withdraw_dlink_ready();
        }
    };

    struct request_error_routine {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->routine_reset_pending = true;
            fsm.d->emit_error_routine();
        }
    };

    struct publish_ev_mac {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const& evt, FSM& fsm, Source&, Target&) {
            fsm.d->emit_ev_mac(evt.mac);
        }
    };

    // --- transition table ------------------------------------------------------------------
    //
    // Publishing happens in the state entry actions. The RestartWait/dlink_error row consumes a
    // repeated D-LINK_ERROR so the outer machine's rows do not restart the wait or spend an attempt.
    //
    // clang-format off
    using transition_table = mpl::vector<
        //    +-------------+---------------------+-------------+-----------------------+-----------------------+
        //    | Source      | Event               | Target      | Action                | Guard                 |
        //    +-------------+---------------------+-------------+-----------------------+-----------------------+
        Row   < Unmatched   , enter_bcd           , Matched     , none                  , carrier               >,
        Row   < Unmatched   , enter_bcd           , Matching    , begin_comm_init       , no_carrier            >,
        Row   < Matching    , carrier_up          , Matched     , none                  , none                  >,
        Row   < Matching    , link_detect_timeout , Matching    , repeat_comm_init      , may_repeat            >,
        Row   < Matching    , link_detect_timeout , RestartWait , spend_init_retry      , restart_after_failure >,
        Row   < Matching    , link_detect_timeout , Unmatched   , none                  , no_retries            >,
        Row   < Matched     , carrier_down        , Matching    , restart_matching      , retries_left          >,
        Row   < Matched     , carrier_down        , Unmatched   , give_up               , no_retries            >,
        Row   < Matched     , link_lost           , Matching    , restart_matching      , retries_left          >,
        Row   < Matched     , link_lost           , Unmatched   , give_up               , no_retries            >,
        Row   < Matched     , dlink_pause         , Paused      , none                  , none                  >,
        Row   < Matched     , neighbor_reachable  , none        , publish_ev_mac        , none                  >,
        Row   < Paused      , carrier_up          , Matched     , none                  , none                  >,
        Row   < Paused      , neighbor_reachable  , Matched     , publish_ev_mac        , none                  >,
        Row   < RestartWait , retry_wait_elapsed  , Matched     , request_error_routine , carrier               >,
        Row   < RestartWait , retry_wait_elapsed  , Matching    , request_error_routine , no_carrier            >,
        Row   < RestartWait , enter_bcd           , Matched     , none                  , carrier               >,
        Row   < RestartWait , enter_bcd           , Matching    , begin_comm_init       , no_carrier            >,
        Row   < RestartWait , dlink_error         , none        , none                  , none                  >
        //    +-------------+---------------------+-------------+-----------------------+-----------------------+
        >;
    // clang-format on

    /// Not counted: an unhandled event is offered to the outer machine next, whose no_transition
    /// counts. Holds only while there is exactly one submachine and no orthogonal region.
    template <class FSM, class Event> void no_transition(Event const&, FSM&, int) {
    }
};

using Session = msm::back::state_machine<SessionDef>;

// ==========================================================================================
// Outer machine: the session
// ==========================================================================================
//
// Handles the session-wide events once. The teardown rows are self-transitions on Session: the
// active substate's exit action stops its timer and re-entry starts at Unmatched.
struct link_def : public msm::front::state_machine_def<link_def> {
    machine_data* d{nullptr};

    // --- guards ----------------------------------------------------------------------------

    struct retries_left {
        template <class EVT, class FSM, class Source, class Target>
        bool operator()(EVT const&, FSM& fsm, Source&, Target&) {
            return fsm.d->retries_left();
        }
    };

    struct no_retries {
        template <class EVT, class FSM, class Source, class Target>
        bool operator()(EVT const&, FSM& fsm, Source&, Target&) {
            return not fsm.d->retries_left();
        }
    };

    struct routine_reset_pending {
        template <class EVT, class FSM, class Source, class Target>
        bool operator()(EVT const&, FSM& fsm, Source&, Target&) {
            return fsm.d->routine_reset_pending;
        }
    };

    struct no_routine_reset_pending {
        template <class EVT, class FSM, class Source, class Target>
        bool operator()(EVT const&, FSM& fsm, Source&, Target&) {
            return not fsm.d->routine_reset_pending;
        }
    };

    // --- actions ---------------------------------------------------------------------------

    /// Connection ends or restarts from the top: withdraw D-LINK_READY, refill the retry budget.
    struct end_connection {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->routine_reset_pending = false;
            fsm.d->close_sync_window();
            fsm.d->withdraw_dlink_ready();
            fsm.d->refill_retries();
        }
    };

    /// The reset EvseManager's error sequence sends after request_error_routine. Absorbed: it would
    /// land in Unmatched with no enter_bcd to follow (the CP state stays B) and refill the budget.
    struct absorb_routine_reset {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->routine_reset_pending = false;
        }
    };

    /// D-LINK_ERROR with budget left: withdraw D-LINK_READY and spend an attempt. The restart is
    /// requested when the CC.5.2.3.2 wait elapses.
    struct spend_retry {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->close_sync_window();
            fsm.d->withdraw_dlink_ready();
            fsm.d->take_retry();
        }
    };

    /// Budget exhausted: withdraw D-LINK_READY and fall back to Unmatched.
    struct give_up {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->close_sync_window();
            fsm.d->withdraw_dlink_ready();
        }
    };

    using initial_state = Session;

    /// Direct entry into the inner RestartWait state.
    using RestartWait = Session::direct<SessionDef::RestartWait>;

    // --- transition table ------------------------------------------------------------------
    //
    // `reset` ignores `enable`: EvseManager only ever calls reset(false), as the session-end teardown
    // (EvseManager.cpp:409, :1088, :1097); latching matching off would block every further session.
    // routine_reset_pending is cleared by that reset, leave_bcd or dlink_terminate, never by time.
    // The reset and dlink_error guard pairs are mutually exclusive.
    //
    // clang-format off
    using transition_table = mpl::vector<
        //    +---------+-----------------+-------------+----------------------+--------------------------+
        //    | Source  | Event           | Target      | Action               | Guard                    |
        //    +---------+-----------------+-------------+----------------------+--------------------------+
        Row   < Session , reset           , none        , absorb_routine_reset , routine_reset_pending    >,
        Row   < Session , reset           , Session     , end_connection       , no_routine_reset_pending >,
        Row   < Session , leave_bcd       , Session     , end_connection       , none                     >,
        Row   < Session , dlink_terminate , Session     , end_connection       , none                     >,
        Row   < Session , dlink_error     , RestartWait , spend_retry          , retries_left             >,
        Row   < Session , dlink_error     , Session     , give_up              , no_retries               >
        //    +---------+-----------------+-------------+----------------------+--------------------------+
        >;
    // clang-format on

    template <class FSM, class Event> void no_transition(Event const&, FSM&, int) {
        ++d->ignored;
    }
};

using link = msm::back::state_machine<link_def>;

} // namespace

struct link_state_machine::impl {
    machine_data data;
    link machine;

    explicit impl(link_config config) {
        data.cfg = config;
        machine.d = &data;
        machine.get_state<Session&>().d = &data;
    }
};

link_state_machine::link_state_machine(link_config config) : m_impl(std::make_unique<impl>(config)) {
}

link_state_machine::~link_state_machine() = default;

void link_state_machine::start() {
    m_impl->machine.start();
}

void link_state_machine::reset(bool enable) {
    m_impl->machine.process_event(main::reset{enable});
}

void link_state_machine::enter_bcd(bool carrier_up) {
    m_impl->machine.process_event(main::enter_bcd{carrier_up});
}

void link_state_machine::leave_bcd() {
    m_impl->machine.process_event(main::leave_bcd{});
}

void link_state_machine::carrier_up() {
    m_impl->machine.process_event(main::carrier_up{});
}

void link_state_machine::carrier_down() {
    m_impl->machine.process_event(main::carrier_down{});
}

void link_state_machine::link_lost() {
    m_impl->machine.process_event(main::link_lost{});
}

void link_state_machine::link_detect_timeout(bool may_repeat) {
    m_impl->machine.process_event(main::link_detect_timeout{may_repeat});
}

void link_state_machine::retry_wait_elapsed(bool carrier_up) {
    m_impl->machine.process_event(main::retry_wait_elapsed{carrier_up});
}

void link_state_machine::dlink_error() {
    m_impl->machine.process_event(main::dlink_error{});
}

void link_state_machine::dlink_terminate() {
    m_impl->machine.process_event(main::dlink_terminate{});
}

void link_state_machine::dlink_pause() {
    m_impl->machine.process_event(main::dlink_pause{});
}

void link_state_machine::neighbor_reachable(std::string mac) {
    m_impl->machine.process_event(main::neighbor_reachable{std::move(mac)});
}

std::vector<effect> link_state_machine::take_effects() {
    auto taken = std::move(m_impl->data.effects);
    m_impl->data.effects.clear();
    return taken;
}

internal_state link_state_machine::state() const {
    return m_impl->data.current;
}

link_state link_state_machine::published_state() const {
    switch (m_impl->data.current) {
    case internal_state::matching:
        return link_state::matching;
    case internal_state::matched:
    case internal_state::paused:
        return link_state::matched;
    case internal_state::unmatched:
    case internal_state::retry_wait:
        break;
    }
    return link_state::unmatched;
}

int link_state_machine::retry_count() const {
    return m_impl->data.retries;
}

bool link_state_machine::dlink_ready() const {
    return m_impl->data.ready;
}

int link_state_machine::ignored_events() const {
    return m_impl->data.ignored;
}

char const* to_string(link_state value) {
    switch (value) {
    case link_state::unmatched:
        return "UNMATCHED";
    case link_state::matching:
        return "MATCHING";
    case link_state::matched:
        return "MATCHED";
    }
    return "UNKNOWN";
}

char const* to_string(internal_state value) {
    switch (value) {
    case internal_state::unmatched:
        return "unmatched";
    case internal_state::matching:
        return "matching";
    case internal_state::matched:
        return "matched";
    case internal_state::paused:
        return "paused";
    case internal_state::retry_wait:
        return "retry_wait";
    }
    return "unknown";
}

char const* to_string(timer_id value) {
    switch (value) {
    case timer_id::link_detect:
        return "link_detect";
    case timer_id::retry_wait:
        return "retry_wait";
    case timer_id::sync_repetition:
        return "sync_repetition";
    }
    return "unknown";
}

char const* to_string(effect::kind value) {
    switch (value) {
    case effect::kind::publish_state:
        return "publish_state";
    case effect::kind::publish_dlink_ready:
        return "publish_dlink_ready";
    case effect::kind::publish_request_error_routine:
        return "publish_request_error_routine";
    case effect::kind::publish_ev_mac:
        return "publish_ev_mac";
    case effect::kind::start_timer:
        return "start_timer";
    case effect::kind::stop_timer:
        return "stop_timer";
    }
    return "unknown";
}

} // namespace main
} // namespace module
