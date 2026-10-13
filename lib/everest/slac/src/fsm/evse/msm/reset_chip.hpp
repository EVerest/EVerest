// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// ResetChip sub-machine: optional modem reset after the NMK was programmed (Reset -> ResetChip -> Idle).

#pragma once
#include "../../msm_helpers.hpp"
#include "guards_and_actions/reset_chip_logic.hpp"

#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/telemetry.hpp>
#include <everest/slac/timer.hpp>

#include <boost/mpl/vector.hpp>
#include <boost/msm/front/completion_event.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/states.hpp>

namespace everest::lib::slac::msm::reset_chip_sm {

struct ResetChip_def : public state_machine_def<ResetChip_def> {
    // States
    struct Delay : public timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
            duration = fsm.ctx->slac_config.chip_reset.delay;
            timeout_state::on_entry(e, fsm);
        }
    };
    struct Sent : public timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
            duration = fsm.ctx->slac_config.chip_reset.timeout;
            timeout_state::on_entry(e, fsm);
        }
    };
    // clang-format off
    struct Received  : public state<> { };
    struct Done      : public exit_pseudo_state<update> { };
    // clang-format on

    // Transitions
    using initial_state = Delay;
    // clang-format off
    struct transition_table : boost::mpl::vector<
        //    +----------+---------+----------+-------------------+-------------------+
        //    | Source   | Event   | Target   | Action            | Guard             |
        //    +----------+---------+----------+-------------------+-------------------+
        Row   < Delay    , update  , Sent     , send_message      , timeout           >,
        Row   < Sent     , message , Received , none              , is_reset_message  >,
        Row   < Sent     , update  , Done     , none              , reset_done        >,
        Row   < Sent     , update  , Done     , none              , reset_unsupported >,
        Row   < Sent     , update  , Done     , log_reset_timeout , timeout           >,
        Row   < Received , update  , Done     , none              , none              >
        //    +----------+---------+----------+-------------------+-------------------+
        >{};
    // clang-format on
    // No try/catch around the event: the callbacks the machine calls never throw (the context parks
    // their failures, see rethrow_recorded), so a throw here is a bug and must not be swallowed.
    typedef int no_exception_thrown;
    template <class FSM, class Event> void no_transition(Event const&, FSM&, int) {
    }

    // Entry / exit
    template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        ctx->enter_state(SlacState::ResetChip, D3State::Unmatched);
    }

    // Members
    fsm::evse::Context* ctx;
};

} // namespace everest::lib::slac::msm::reset_chip_sm
