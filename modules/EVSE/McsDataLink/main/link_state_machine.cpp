// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// The machine lives in its own translation unit behind link_state_machine.hpp so that no other
// translation unit pays msm's instantiation cost, and so that none of them can be affected by
// anything this one has to configure for boost.
//
// It no longer has to configure anything: the flat 39-row table used to exceed the 20 entries
// boost::mpl ships preprocessed headers for and needed BOOST_MPL_LIMIT_VECTOR_SIZE raised, but
// neither table is anywhere near that since the hierarchy split them, and raising the limits also
// forced BOOST_MPL_CFG_NO_PREPROCESSED_HEADERS, which cost about a third of this file's compile
// time. If a table ever grows past 20 rows again, raise the limits here - never in a header, where
// it would silently change the mpl configuration of every other consumer.

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

// The events mirror the snake_case public API 1:1. That costs one thing at the emission sites:
// inside link_state_machine::carrier_up() the class scope wins unqualified lookup and finds the
// member function, so the emissions reach these with an explicit main:: qualification.
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
    /// TT_sync_repetition is still open (tracked by the owner, which holds the timer).
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

    /// Unconditional on purpose: re-entering the matched state after D-LINK_PAUSE has to re-issue
    /// D-LINK_READY even though it was never withdrawn (V2G10-042 wake-up).
    void emit_dlink_ready() {
        ready = true;
        effect item;
        item.what = effect::kind::publish_dlink_ready;
        item.ready = true;
        effects.push_back(std::move(item));
    }

    /// Only when it was actually outstanding - "dlink_ready(false) if it was true".
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

    // --- retry budget ----------------------------------------------------------------------

    // C_conn_retry counts per EV connection, not per successful match: reaching MATCHED
    // deliberately does not refund attempts, otherwise a link that flaps between "up" and "lost"
    // would retry forever and conn_retry_max would bound nothing. The budget is refilled when the
    // connection ends or is explicitly restarted: leave_bcd, reset, dlink_terminate.
    //
    // OPEN (for review): V2G10-052 says "a successful communication setup shall reset all the
    // timeout timers and reset the retry_counters", which read literally asks for the refund this
    // deliberately withholds. The counter is kept unrefunded because the alternative is an
    // unbounded restart loop on flapping hardware, and because Table 8 scopes C_conn_retry to
    // "communication setup retries by wakeup trigger by basic signalling" - the comm-init phase,
    // which TT_sync_repetition already bounds in time. If conformance testing insists on the
    // literal reading, refill on entry to the matched state and rely on TT_sync_repetition plus
    // the HLC timeouts above to bound the loop.
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
// Everything that is specific to *where* in a session the link currently is. The events that mean
// the same thing wherever we are - reset, leave_bcd, dlink_terminate, dlink_error - are not here;
// they are handled once by the outer machine below.
//
// Wherever two rows share a source and an event their guards are mutually exclusive, so nothing
// depends on the order msm evaluates them in. That discipline is deliberate: msm does not resolve
// such a conflict in declaration order, so a guarded row paired with an unguarded fallback silently
// picks the fallback.
struct SessionDef : public msm::front::state_machine_def<SessionDef> {
    machine_data* d{nullptr};

    /// No link, and none being established. Reached at the start of a session, after a
    /// communication initialization failure, and after the retry budget ran out - in the last two
    /// cases the EV is still plugged in, which is why nothing here restarts on its own.
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

    /// D-LINK_PAUSE received. No publishes at all: the link stays logically up (V2G10-041), so the
    /// state variable stays MATCHED and dlink_ready stays outstanding. Carrier loss here is
    /// expected rather than a failure, and the state leaves again on the first evidence that the
    /// session resumed: the carrier returning if the PHY did power down, or a neighbour answering.
    /// The second path carries the realistic case today - the LAN8650 low-power mode is
    /// unimplemented, so a real pause keeps the carrier up and produces no wake-up edge at all,
    /// and staying paused would leave supervision disarmed for the whole resumed session.
    struct Paused : public msm::front::state<> {
        template <class Event, class FSM> void on_entry(Event const&, FSM& fsm) {
            fsm.d->current = internal_state::paused;
        }
    };

    /// Waiting out the >= 3 s inter-attempt guard of IEC 61851-23-3 CC.5.2.3.2 before asking for
    /// the restart routine. Entered from the outer machine on dlink_error, hence the explicit entry
    /// point - it is the one state a session-wide event has to land on directly. A bare carrier
    /// edge deliberately has no row here: the guard time is mandatory and the link is meant to
    /// come back through the B0-to-B restart, not because the carrier flickered.
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

    /// Both bounds of V2G10-056 and -058 at once: the repetition window is open and an attempt is
    /// left. Paired with no_repeat rather than with an unguarded fallback row, see the note above.
    struct may_repeat {
        template <class EVT, class FSM, class Source, class Target>
        bool operator()(EVT const& evt, FSM& fsm, Source&, Target&) {
            return evt.may_repeat and fsm.d->retries_left();
        }
    };

    struct no_repeat {
        template <class EVT, class FSM, class Source, class Target>
        bool operator()(EVT const& evt, FSM& fsm, Source&, Target&) {
            return not(evt.may_repeat and fsm.d->retries_left());
        }
    };

    // --- actions ---------------------------------------------------------------------------

    /// The communication initialization trigger (V2G10-055): open the TT_sync_repetition window.
    /// Only the enter_bcd edges do this - a restart after a link loss is a reconnect governed by
    /// C_conn_retry, not a repetition of the initial setup.
    struct begin_comm_init {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            if (fsm.d->cfg.sync_repetition_ms > 0) {
                fsm.d->start_timer(timer_id::sync_repetition, fsm.d->cfg.sync_repetition_ms);
            }
        }
    };

    /// V2G10-056: the initialization FAILED but the window is still open and both sides are still
    /// in state B, so restart it. Costs one attempt so the repetition cannot run forever even if
    /// each attempt fails fast.
    struct repeat_comm_init {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->take_retry();
        }
    };

    /// Link lost while up, with budget left. D-LINK_READY(no link) goes up first (V2G10-036) and
    /// UNMATCHED is published explicitly before the machine re-enters MATCHING, so a consumer sees
    /// that the link really went down even though matching resumes in the same breath.
    struct restart_matching {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->withdraw_dlink_ready();
            fsm.d->emit_state(link_state::unmatched);
            fsm.d->take_retry();
        }
    };

    /// Budget exhausted: report the link down and stay put. State B0 territory - EvseManager
    /// decides what happens next (V2G10-038).
    struct give_up {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->withdraw_dlink_ready();
        }
    };

    struct request_error_routine {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
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
    // Publishing is done by the state entry actions above, so the transition actions only carry
    // what is specific to the edge.
    //
    // The RestartWait/dlink_error row consumes its event on purpose: a repeated D-LINK_ERROR
    // while the guard is already running changes nothing, and must not restart the wait or spend
    // another attempt. Without this row the outer machine's dlink_error rows would fire.
    //
    // clang-format off
    using transition_table = mpl::vector<
        //    +-------------+---------------------+-----------+-----------------------+--------------+
        //    | Source      | Event               | Target    | Action                | Guard        |
        //    +-------------+---------------------+-----------+-----------------------+--------------+
        Row   < Unmatched   , enter_bcd           , Matched   , none                  , carrier      >,
        Row   < Unmatched   , enter_bcd           , Matching  , begin_comm_init       , no_carrier   >,
        Row   < Matching    , carrier_up          , Matched   , none                  , none         >,
        Row   < Matching    , link_detect_timeout , Matching  , repeat_comm_init      , may_repeat   >,
        Row   < Matching    , link_detect_timeout , Unmatched , none                  , no_repeat    >,
        Row   < Matched     , carrier_down        , Matching  , restart_matching      , retries_left >,
        Row   < Matched     , carrier_down        , Unmatched , give_up               , no_retries   >,
        Row   < Matched     , link_lost           , Matching  , restart_matching      , retries_left >,
        Row   < Matched     , link_lost           , Unmatched , give_up               , no_retries   >,
        Row   < Matched     , dlink_pause         , Paused    , none                  , none         >,
        Row   < Matched     , neighbor_reachable  , none      , publish_ev_mac        , none         >,
        Row   < Paused      , carrier_up          , Matched   , none                  , none         >,
        Row   < Paused      , neighbor_reachable  , Matched   , publish_ev_mac        , none         >,
        Row   < RestartWait , retry_wait_elapsed  , Matched   , request_error_routine , carrier      >,
        Row   < RestartWait , retry_wait_elapsed  , Matching  , request_error_routine , no_carrier   >,
        Row   < RestartWait , enter_bcd           , Matched   , none                  , carrier      >,
        Row   < RestartWait , enter_bcd           , Matching  , begin_comm_init       , no_carrier   >,
        Row   < RestartWait , dlink_error         , none      , none                  , none         >
        //    +-------------+---------------------+-----------+-----------------------+--------------+
        >;
    // clang-format on

    /// Deliberately silent, and deliberately not counting: an event with no row in this table is
    /// offered to the outer machine next, so counting it here would count every session-wide event
    /// as ignored. The outer machine's no_transition is the one that sees "nobody handled this".
    ///
    /// This holds only while there is exactly one substate machine. Add a second orthogonal region
    /// or a nested submachine and "the inner machine did not handle it" stops implying "the outer
    /// one will get a chance to", so the counting would have to move or be reconciled across them.
    template <class FSM, class Event> void no_transition(Event const&, FSM&, int) {
    }
};

using Session = msm::back::state_machine<SessionDef>;

// ==========================================================================================
// Outer machine: the session
// ==========================================================================================
//
// Four events mean the same thing wherever the session currently is, so they are handled here once
// instead of once per state. Three of them end the session; the fourth restarts it.
//
// The teardown rows are self-transitions on the session: exiting it runs the exit action of
// whichever substate was active (stopping its timer) and re-entering it starts the session again at
// Unmatched, which is exactly the target all three want.
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

    // --- actions ---------------------------------------------------------------------------

    /// The EV connection ends or is restarted from the top: withdraw D-LINK_READY and refill the
    /// retry budget.
    struct end_connection {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->withdraw_dlink_ready();
            fsm.d->refill_retries();
        }
    };

    /// D-LINK_ERROR with budget left: report the link down and spend an attempt. The restart itself
    /// is requested when the CC.5.2.3.2 wait elapses.
    struct spend_retry {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->withdraw_dlink_ready();
            fsm.d->take_retry();
        }
    };

    /// Budget exhausted: report the link down and fall back to unmatched.
    struct give_up {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.d->withdraw_dlink_ready();
        }
    };

    using initial_state = Session;

    /// The direct entry into the inner machine's RestartWait state - the one state a session-wide
    /// event has to land on.
    using RestartWait = Session::direct<SessionDef::RestartWait>;

    // --- transition table ------------------------------------------------------------------
    //
    // `reset` ignores its `enable` argument. The slac interface documents enable=false as "stop
    // matching", but EvseManager only ever calls reset(false) - as the session-end teardown, with
    // the matching reset(true) call commented out (EvseManager.cpp:409, :1088, :1097). Latching
    // matching off there would leave the module unable to serve any further session. SlacSimulator
    // and EvseSlacNeo treat reset(false) as "reset" for the same reason; only the BUSlac bring-up
    // tool uses the flag as start/stop.
    //
    // The dlink_error guards are mutually exclusive, so nothing depends on row order here either.
    //
    // clang-format off
    using transition_table = mpl::vector<
        //    +---------+-----------------+-------------+----------------+--------------+
        //    | Source  | Event           | Target      | Action         | Guard        |
        //    +---------+-----------------+-------------+----------------+--------------+
        Row   < Session , reset           , Session     , end_connection , none         >,
        Row   < Session , leave_bcd       , Session     , end_connection , none         >,
        Row   < Session , dlink_terminate , Session     , end_connection , none         >,
        Row   < Session , dlink_error     , RestartWait , spend_retry    , retries_left >,
        Row   < Session , dlink_error     , Session     , give_up        , no_retries   >
        //    +---------+-----------------+-------------+----------------+--------------+
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
