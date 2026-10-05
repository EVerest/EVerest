// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

// Walks the active states of a Boost.MSM machine, its sub-machines and dynamic sessions.

#pragma once
#include "msm_helpers.hpp"

#include <everest/slac/timer.hpp>

#include <boost/mpl/for_each.hpp>
#include <boost/mpl/placeholders.hpp>
#include <boost/msm/back/metafunctions.hpp>
#include <boost/msm/common.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace everest::lib::slac::msm {

template <typename T, typename = void> struct is_submachine : std::false_type {};
template <typename T>
struct is_submachine<T, std::void_t<decltype(std::declval<T>().current_state())>> : std::true_type {};
template <typename T, typename = void> struct has_sessions : std::false_type {};
template <typename T> struct has_sessions<T, std::void_t<decltype(std::declval<T>().sessions)>> : std::true_type {};
template <typename T> T extract_wrap_type(boost::msm::wrap<T>);

template <class FSM, class Visitor> void traverse_fsm(FSM const& fsm, std::size_t depth, Visitor& visitor) {
    for (auto region_id = 0U; region_id < FSM::nr_regions::value; ++region_id) {
        using Stt = typename FSM::stt;
        using all_states = typename boost::msm::back::generate_state_set<Stt>::type;

        auto const active_id = fsm.current_state()[region_id];
        auto found = false;

        boost::mpl::for_each<all_states, boost::msm::wrap<boost::mpl::placeholders::_1>>([&](auto wrap) {
            using StateType = decltype(extract_wrap_type(wrap));
            if (found or boost::msm::back::get_state_id<Stt, StateType>::value != active_id) {
                return;
            }
            found = true;
            auto const& state = fsm.template get_state<StateType const&>();
            visitor.on_active_state(state, active_id, depth);
            if constexpr (is_submachine<StateType>::value) {
                visitor.template on_submachine_start<StateType>(active_id, depth);
                traverse_fsm(state, depth + 1, visitor);
                visitor.template on_submachine_end<StateType>(active_id, depth);
            }
        });
    }

    if constexpr (has_sessions<FSM>::value) {
        visitor.on_dynamic_sessions_size(fsm.sessions.size());
        int idx = 0;
        for (auto const& session : fsm.sessions) {
            visitor.on_dynamic_session_start(idx, depth);
            traverse_fsm(session, depth + 1, visitor);
            visitor.on_dynamic_session_end(idx++, depth);
        }
    }
}

struct silent_visitor {
    template <class State> void on_active_state(State const&, int, std::size_t) {
    }
    template <class State> void on_submachine_start(int, std::size_t) {
    }
    template <class State> void on_submachine_end(int, std::size_t) {
    }
    void on_dynamic_sessions_size(std::size_t) {
    }
    void on_dynamic_session_start(int, std::size_t) {
    }
    void on_dynamic_session_end(int, std::size_t) {
    }
};

struct signature_visitor : silent_visitor {
    std::vector<int> sig;
    template <class State> void on_active_state(State const&, int state_id, std::size_t) {
        sig.push_back(state_id);
    }
    void on_dynamic_sessions_size(std::size_t size) {
        sig.push_back(static_cast<int>(size));
    }
};

struct deadline_visitor : silent_visitor {
    earliest_deadline next;
    template <class State> void on_active_state(State const& state, int, std::size_t) {
        if constexpr (has_deadlines<State>::value) {
            state.deadlines(next);
        }
    }
};

// Active-state ids of the machine, its sub-machines and sessions: equal signatures, same states.
template <class FSM> std::vector<int> signature_of(FSM const& fsm) {
    signature_visitor v;
    traverse_fsm(fsm, 0, v);
    return std::move(v.sig);
}

// Wait from `now` until the earliest deadline an active state or its handlers holds, or nothing.
template <class FSM> std::optional<timer::tick> next_wakeup_of(FSM const& fsm, timer::tp now) {
    deadline_visitor v{{}, earliest_deadline{now}};
    traverse_fsm(fsm, 0, v);
    return v.next.wait();
}

// Feeds `update` until it moves no state any more, so what the last event made true is acted on
// now instead of at a later wake-up. Time stays frozen meanwhile, which rules out timer-driven
// cycles; a guard-driven one would be a table bug and is reported rather than spun on.
template <class FSM> void settle(FSM& fsm) {
    auto before = signature_of(fsm);
    for (int round = 0; round < 32; ++round) {
        fsm.process_event(update{});
        // A callback failure parked during that update surfaces now, before another round runs and
        // before a machine that then fails to settle could mask it with its own logic_error.
        rethrow_recorded(fsm);
        auto after = signature_of(fsm);
        if (after == before) {
            return;
        }
        before = std::move(after);
    }
    throw std::logic_error("SLAC state machine does not settle");
}

} // namespace everest::lib::slac::msm
