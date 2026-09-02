// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Reset sub-machine: (re)generates the NMK and programs it into the modem with CM_SET_KEY.REQ,
// then exits to ResetChip or Idle.

#pragma once
#include "common.hpp"
#include "reset_actions.hpp"

namespace everest::lib::slac::msm::reset_sm {

struct Reset_def       : public state_machine_def<Reset_def> {
    // States
    // clang-format off
    struct Init      : public state<>{ };
    struct MsgSent   : public state<>{ };
    struct MsgValid  : public state<>{ };
    struct ResetChip : public exit_pseudo_state<update>{ };
    struct Idle      : public exit_pseudo_state<update>{ };
    // clang-format on

    // Transition guards
    using timeout_retry            = And_<set_key_timeout, And_<is_retry_confirmed_set_key, has_set_key_attempts_left>>;
    using timeout_give_up          = And_<set_key_timeout, And_<is_retry_confirmed_set_key, has_no_set_key_attempts_left>>;
    using set_key_ok               = And_<msg_expected, is_set_key_cnf_success>;
    using set_key_failed           = And_<msg_expected, is_set_key_cnf_failed>;
    using set_key_failed_retry     = And_<set_key_failed, And_<is_retry_confirmed_set_key, has_set_key_attempts_left>>;
    using set_key_failed_give_up   = And_<set_key_failed, And_<is_retry_confirmed_set_key, has_no_set_key_attempts_left>>;
    using no_reset_chip            = Not_<is_reset_chip_on>;

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
    template <class FSM,class Event>
    void no_transition(Event const&, FSM&, int) { }

    // Entry / exit
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        if (fsm.ctx->slac_config.regenerate_key_on_reset){
            if (fsm.ctx->slac_config.set_key_handling_mode == fsm::evse::SetKeyHandlingMode::retry_confirmed) {
                fsm.ctx->slac_config.generate_nmk(this->pending_nmk);
            } else {
                fsm.ctx->slac_config.generate_nmk(fsm.ctx->slac_config.session_nmk);
            }
        } else {
            this->pending_nmk = fsm.ctx->slac_config.session_nmk;
        }
        if (fsm.ctx->slac_config.set_key_handling_mode == fsm::evse::SetKeyHandlingMode::legacy_single_attempt) {
            this->pending_nmk = fsm.ctx->slac_config.session_nmk;
        }
        this->set_key_attempts = 1;
        ctx->log_info("Entered Reset state");
        ctx->clear_match_confirm_cache();
        ctx->status.match_state = SlacState::Reset;
        ctx->status.d3_state = D3State::Unmatched;
        ctx->status.modem_NMK = false;
    }

    // Members
    fsm::evse::Context* ctx;
    Nmk pending_nmk{};
    int set_key_attempts{0};
    timer set_key_timer;

    // Named differently from the set_key_timeout guard functor in reset_actions.hpp, which the
    // transition guards above refer to.
    bool set_key_timer_expired() {
        return set_key_timer.timeout();
    }
    bool state_timeout() {
        return set_key_timer_expired();
    }
};

} // namespace everest::lib::slac::msm::reset_sm
