// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// The machine lives in its own translation unit behind link_state_machine.hpp so that no other
// translation unit pays msm's instantiation cost, and so that none of them can be affected by
// anything this one might have to configure for boost. Nothing needs configuring at this size: the
// table is well inside the 20 entries boost::mpl ships preprocessed headers for. If it ever grows
// past that, raise BOOST_MPL_LIMIT_VECTOR_SIZE here - never in a header, where it would silently
// change the mpl configuration of every other consumer.

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
struct reset {};
struct trigger_matching {
    bool carrier_up{false};
};
struct carrier_up {};
struct carrier_down {};
struct link_lost {};
struct link_detect_timeout {};
struct neighbor_reachable {
    std::string mac;
};

/// The front end. Holds the machine's data; msm::back::state_machine derives from it, so both the
/// actions (which get the back end) and the owner reach the same members.
///
/// <b>Why this table is flat.</b> The EVSE-side module is hierarchical because 23 of its 39 rows
/// were four session-wide events repeated once per state, which is what a composite state exists to
/// collapse. Here the only repeated event is `reset`, at three rows out of ten, so a composite plus
/// an explicit entry point would trade two rows for a second table and a shared-data indirection -
/// cargo-culting the structure instead of applying the reasoning behind it. If `ev_slac` ever gains
/// the pause/error/terminate commands its EVSE counterpart has, this table grows the same repeats
/// and McsDataLink is then the pattern to follow.
struct link_def : public msm::front::state_machine_def<link_def> {
    link_config cfg{};
    std::vector<effect> effects{};
    link_state current{link_state::unmatched};
    /// Suppresses redundant publishes. Unset until the first one, so start() always publishes.
    std::optional<link_state> last_state{};
    bool ready{false};
    int ignored{0};
    std::string published_mac{};

    // --- effect emitters -------------------------------------------------------------------

    void emit_state(link_state value) {
        current = value;
        if (last_state == value) {
            return;
        }
        last_state = value;
        effect item;
        item.what = effect::kind::publish_state;
        item.state = value;
        effects.push_back(std::move(item));
    }

    /// The matched state is only ever entered from a state where the link was down, so this is
    /// always a genuine edge. (The EVSE side needs an unconditional re-issue here for the
    /// V2G10-042 wake-up out of D-LINK_PAUSE; `ev_slac` has no pause command, so there is no
    /// re-entry from a state that already held D-LINK_READY.)
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

    void emit_connector_mac(std::string const& mac) {
        if (not cfg.publish_connector_mac or mac.empty() or mac == published_mac) {
            return;
        }
        published_mac = mac;
        effect item;
        item.what = effect::kind::publish_connector_mac;
        item.mac = mac;
        effects.push_back(std::move(item));
    }

    void forget_connector_mac() {
        published_mac.clear();
    }

    void start_timer(int timeout_ms) {
        effect item;
        item.what = effect::kind::start_timer;
        item.timeout_ms = timeout_ms;
        effects.push_back(std::move(item));
    }

    void stop_timer() {
        effect item;
        item.what = effect::kind::stop_timer;
        effects.push_back(std::move(item));
    }

    // --- states ----------------------------------------------------------------------------

    /// No link, and none being established. Also where the EV waits after a failed communication
    /// initialization: V2G10-039 has it wait for the EVSE's restart indication rather than retrying
    /// on its own, and that indication arrives as another trigger_matching from the EV stack. This
    /// is why there is no retry budget anywhere in this module.
    struct Unmatched : public msm::front::state<> {
        template <class Event, class FSM> void on_entry(Event const&, FSM& fsm) {
            fsm.forget_connector_mac();
            fsm.emit_state(link_state::unmatched);
        }
    };

    /// The EV stack asked for the link and it is not up yet. The communication-setup deadline runs.
    struct Matching : public msm::front::state<> {
        template <class Event, class FSM> void on_entry(Event const&, FSM& fsm) {
            fsm.forget_connector_mac();
            fsm.emit_state(link_state::matching);
            fsm.start_timer(fsm.cfg.link_detect_timeout_ms);
        }
        template <class Event, class FSM> void on_exit(Event const&, FSM& fsm) {
            fsm.stop_timer();
        }
    };

    /// Link up and D-LINK_READY issued (V2G10-030).
    struct Matched : public msm::front::state<> {
        template <class Event, class FSM> void on_entry(Event const&, FSM& fsm) {
            fsm.emit_state(link_state::matched);
            fsm.emit_dlink_ready();
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

    // --- actions ---------------------------------------------------------------------------

    /// The link is going away, for whatever reason. V2G10-036: report D-LINK_READY(no link) upward
    /// if it was ever issued. A no-op on the paths where it was not, which is why every transition
    /// into the Unmatched state can share it.
    struct link_down {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const&, FSM& fsm, Source&, Target&) {
            fsm.withdraw_dlink_ready();
        }
    };

    struct publish_connector_mac {
        template <class EVT, class FSM, class Source, class Target>
        void operator()(EVT const& evt, FSM& fsm, Source&, Target&) {
            fsm.emit_connector_mac(evt.mac);
        }
    };

    // --- transition table ------------------------------------------------------------------
    //
    // Publishing is done by the state entry actions above, so the transition actions only carry
    // what is specific to the edge. Wherever two rows share a source and an event their guards are
    // mutually exclusive, so nothing depends on the order msm evaluates them in - msm does not
    // resolve such a conflict in declaration order, so a guarded row paired with an unguarded
    // fallback silently picks the fallback.
    //
    // Note what is absent by design: no retry counter (V2G10-039, see the Unmatched state), no
    // paused state and no restart-guard state (`ev_slac` has no pause or error command to enter
    // them from). The EV's own sleep is a BSP concern; see the note in datalink_controller.cpp
    // about why a sleeping comm module looks exactly like a link loss here, and why that is honest.
    //
    // clang-format off
    using transition_table = mpl::vector<
        //    +-----------+---------------------+-----------+-----------------------+------------+
        //    | Source    | Event               | Target    | Action                | Guard      |
        //    +-----------+---------------------+-----------+-----------------------+------------+
        Row   < Unmatched , trigger_matching    , Matched   , none                  , carrier    >,
        Row   < Unmatched , trigger_matching    , Matching  , none                  , no_carrier >,
        Row   < Unmatched , reset               , Unmatched , link_down             , none       >,
        Row   < Matching  , carrier_up          , Matched   , none                  , none       >,
        Row   < Matching  , link_detect_timeout , Unmatched , link_down             , none       >,
        Row   < Matching  , reset               , Unmatched , link_down             , none       >,
        Row   < Matched   , carrier_down        , Unmatched , link_down             , none       >,
        Row   < Matched   , link_lost           , Unmatched , link_down             , none       >,
        Row   < Matched   , reset               , Unmatched , link_down             , none       >,
        Row   < Matched   , neighbor_reachable  , none      , publish_connector_mac , none       >
        //    +-----------+---------------------+-----------+-----------------------+------------+
        >;
    // clang-format on

    /// Events with no row for the current state are ignored, not an error: a second
    /// trigger_matching, a carrier edge in a state that does not care, a stray timer expiry. The
    /// default implementation asserts, which would turn a benign race into a crash.
    template <class FSM, class Event> void no_transition(Event const&, FSM&, int) {
        ++ignored;
    }
};

using link = msm::back::state_machine<link_def>;

} // namespace

struct link_state_machine::impl {
    link machine;

    explicit impl(link_config config) {
        machine.cfg = config;
    }
};

link_state_machine::link_state_machine(link_config config) : m_impl(std::make_unique<impl>(config)) {
}

link_state_machine::~link_state_machine() = default;

void link_state_machine::start() {
    m_impl->machine.start();
}

void link_state_machine::reset() {
    m_impl->machine.process_event(main::reset{});
}

void link_state_machine::trigger_matching(bool carrier_up) {
    m_impl->machine.process_event(main::trigger_matching{carrier_up});
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

void link_state_machine::link_detect_timeout() {
    m_impl->machine.process_event(main::link_detect_timeout{});
}

void link_state_machine::neighbor_reachable(std::string mac) {
    m_impl->machine.process_event(main::neighbor_reachable{std::move(mac)});
}

std::vector<effect> link_state_machine::take_effects() {
    auto taken = std::move(m_impl->machine.effects);
    m_impl->machine.effects.clear();
    return taken;
}

link_state link_state_machine::state() const {
    return m_impl->machine.current;
}

bool link_state_machine::dlink_ready() const {
    return m_impl->machine.ready;
}

int link_state_machine::ignored_events() const {
    return m_impl->machine.ignored;
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

char const* to_string(effect::kind value) {
    switch (value) {
    case effect::kind::publish_state:
        return "publish_state";
    case effect::kind::publish_dlink_ready:
        return "publish_dlink_ready";
    case effect::kind::publish_connector_mac:
        return "publish_connector_mac";
    case effect::kind::start_timer:
        return "start_timer";
    case effect::kind::stop_timer:
        return "stop_timer";
    }
    return "unknown";
}

} // namespace main
} // namespace module
