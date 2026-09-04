// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Top-level EVSE SLAC state machine: composes the sub-machines and defines the transitions
// between them. Include this file to get the complete machine (SlacFSM).

#pragma once
#include "../../msm_helpers.hpp"
#include "guards_and_actions/machine_logic.hpp"
#include "init.hpp"
#include "matched.hpp"
#include "matching.hpp"
#include "reset.hpp"
#include "reset_chip.hpp"
#include "wait_for_link.hpp"

#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/telemetry.hpp>

#include <boost/mpl/vector.hpp>
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/completion_event.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/states.hpp>

namespace everest::lib::slac::msm {

using boost::msm::back::state_machine;

struct SlacFSM_def : state_machine_def<SlacFSM_def> {
    // States
    using Init = state_machine<init_sm::Init_def>;
    using Init_Done = Init::exit_pt<init_sm::Init_def::Done>;

    using Reset = state_machine<reset_sm::Reset_def>;
    using Reset_ResetChip = Reset::exit_pt<reset_sm::Reset_def::ResetChip>;
    using Reset_Idle = Reset::exit_pt<reset_sm::Reset_def::Idle>;

    using ResetChip = state_machine<reset_chip_sm::ResetChip_def>;
    using ResetChip_Done = ResetChip::exit_pt<reset_chip_sm::ResetChip_def::Done>;

    using Matching = state_machine<matching_sm::Matching_def>;
    using Matching_Fail = Matching::exit_pt<matching_sm::Matching_def::Failed>;
    using Matching_Match = Matching::exit_pt<matching_sm::Matching_def::Matched>;

    using Matched = state_machine<matched_sm::Matched_def>;
    using Matched_Fail = Matched::exit_pt<matched_sm::Matched_def::Failed>;

    using WaitForLink = state_machine<wait_for_link_sm::WaitForLink_def>;
    using WaitForLink_Fail = WaitForLink::exit_pt<wait_for_link_sm::WaitForLink_def::Failed>;
    using WaitForLink_Match = WaitForLink::exit_pt<wait_for_link_sm::WaitForLink_def::Matched>;

    struct Idle : public state<> {
        template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
            fsm.ctx->enter_state(SlacState::Idle, D3State::Unmatched, "Entered Idle state");
            fsm.ctx->clear_match_confirm_cache();
            fsm.ctx->status.modem_PIB = true;
        }
    };
    struct Failed : public state<> {
        template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
            auto& ctx = *fsm.ctx;
            ctx.enter_state(SlacState::Failed, D3State::Unmatched, "Entered Failed state");
            if (ctx.slac_config.ac_mode_five_percent) {
                ctx.signal_error_routine_request();
            }
            ctx.clear_match_confirm_cache();
        }
    };

    // Transitions
    using initial_state = Init;
    using reset_timeout = And_<timeout, is_legacy_set_key_handling_mode>;
    using no_link_wait = Not_<cfg_wait_for_link>;
    // clang-format off
    struct transition_table : boost::mpl::vector<
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        //  | Source            | Event     | Target      | Action          | Guard             |
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < Init_Done         , update    , Reset       , none            , none              >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < Reset             , reset     , Reset       , none            , none              >,
        Row < Reset             , update    , Failed      , none            , reset_timeout     >,
        Row < Reset_ResetChip   , update    , ResetChip   , none            , none              >,
        Row < Reset_Idle        , update    , Idle        , none            , none              >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < ResetChip_Done    , update    , Idle        , none            , none              >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < Idle              , enter_bcd , Matching    , none            , none              >,
        Row < Idle              , reset     , Reset       , none            , none              >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < Matching          , reset     , Reset       , none            , none              >,
        Row < Matching          , leave_bcd , Idle        , none            , none              >,
        Row < Matching_Fail     , none      , Failed      , none            , none              >,
        Row < Matching_Match    , none      , WaitForLink , none            , cfg_wait_for_link >,
        Row < Matching_Match    , none      , Matched     , none            , no_link_wait      >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < WaitForLink       , update    , Failed      , none            , timeout           >,
        Row < WaitForLink       , reset     , Reset       , none            , none              >,
        Row < WaitForLink       , leave_bcd , Reset       , none            , none              >,
        Row < WaitForLink_Fail  , none      , Failed      , none            , none              >,
        Row < WaitForLink_Match , message   , Matched     , none            , none              >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < Matched           , reset     , Reset       , none            , none              >,
        Row < Matched           , leave_bcd , Reset       , none            , none              >,
        Row < Matched_Fail      , message   , Reset       , on_matched_fail , none              >,
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        Row < Failed            , reset     , Reset       , none            , none              >,
        Row < Failed            , leave_bcd , Reset       , none            , none              >
        //  +-------------------+-----------+-------------+-----------------+-------------------+
        > {};
    // clang-format on
    template <class FSM, class Event> void no_transition(Event const&, FSM&, int) {
    }

    // Members
    fsm::evse::Context* ctx;

    SlacFSM_def(fsm::evse::Context& ctx_) : ctx(&ctx_) {
    }
};

using SlacFSM = state_machine<SlacFSM_def>;

} // namespace everest::lib::slac::msm
