// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Reset sub-machine: (re)generates the NMK and programs it into the modem with CM_SET_KEY.REQ,
// then exits to ResetChip or Idle.

#pragma once
#include "../../msm_helpers.hpp"
#include "guards_and_actions/reset_logic.hpp"

#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/slac_types.hpp>
#include <everest/slac/telemetry.hpp>
#include <everest/slac/timer.hpp>

#include <boost/mpl/vector.hpp>
#include <boost/msm/front/completion_event.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/states.hpp>

namespace everest::lib::slac::msm::reset_sm {

struct Reset_def : public state_machine_def<Reset_def> {
    // States
    // clang-format off
    struct Init      : public state<>{ };
    struct MsgSent   : public state<>{ };
    struct MsgValid  : public state<>{ };
    struct ResetChip : public exit_pseudo_state<update>{ };
    struct Idle      : public exit_pseudo_state<update>{ };
    // clang-format on

    // Transition guards
    using timeout_retry = And_<set_key_timeout, And_<is_retry_confirmed_set_key, has_set_key_attempts_left>>;
    using timeout_give_up = And_<set_key_timeout, And_<is_retry_confirmed_set_key, has_no_set_key_attempts_left>>;
    using set_key_ok = And_<msg_expected, is_set_key_cnf_success>;
    using set_key_failed = And_<msg_expected, is_set_key_cnf_failed>;
    using set_key_failed_retry = And_<set_key_failed, And_<is_retry_confirmed_set_key, has_set_key_attempts_left>>;
    using set_key_failed_give_up = And_<set_key_failed, And_<is_retry_confirmed_set_key, has_no_set_key_attempts_left>>;
    using no_reset_chip = Not_<is_reset_chip_on>;

    // Transitions
    using initial_state = Init;
    // clang-format off
    struct transition_table : boost::mpl::vector<
        //  +----------+---------+-----------+------------------------+--------------------------+
        //  | Source   | Event   | Target    | Action                 | Guard                    |
        //  +----------+---------+-----------+------------------------+--------------------------+
        Row < Init     , none    , MsgSent   , send_set_key_req       , none                     >,
        Row < MsgSent  , update  , MsgSent   , retry_send_set_key_req , timeout_retry            >,
        Row < MsgSent  , update  , MsgValid  , fail_send_set_key_req  , timeout_give_up          >,
        Row < MsgSent  , message , MsgValid  , apply_set_key_cnf      , set_key_ok               >,
        Row < MsgSent  , message , MsgSent   , note_set_key_failed    , set_key_failed_retry     >,
        Row < MsgSent  , message , MsgValid  , give_up_set_key_failed , set_key_failed_give_up   >,
        Row < MsgValid , update  , ResetChip , none                   , is_reset_chip_on         >,
        Row < MsgValid , update  , Idle      , none                   , no_reset_chip            >
        //  +----------+---------+-----------+------------------------+--------------------------+
        > {};
    // clang-format on
    template <class FSM, class Event> void no_transition(Event const&, FSM&, int) {
    }

    // Entry / exit
    template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        select_nmk_for_reset(*this);
        ctx->enter_state(SlacState::Reset, D3State::Unmatched, "Entered Reset state");
        ctx->clear_match_confirm_cache();
        ctx->status.modem_NMK = false;
    }

    // Members
    fsm::evse::Context* ctx;
    Nmk pending_nmk{};
    int set_key_attempts{0};
    timer set_key_timer;

    // Named differently from the set_key_timeout guard functor in guards_and_actions/reset_logic.hpp, which the
    // transition guards above refer to.
    bool set_key_timer_expired(timer::tp now) const {
        return set_key_timer.expired(now);
    }
    bool state_timeout(timer::tp now) const {
        return set_key_timer_expired(now);
    }
};

} // namespace everest::lib::slac::msm::reset_sm
