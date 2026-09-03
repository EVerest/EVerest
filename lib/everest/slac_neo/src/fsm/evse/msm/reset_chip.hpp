// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// ResetChip sub-machine: optional modem reset after the NMK was programmed (Reset -> ResetChip -> Idle).

#pragma once
#include "common.hpp"
#include "guards_and_actions/reset_chip_logic.hpp"

namespace everest::lib::slac::msm::reset_chip_sm {

struct ResetChip_def : public state_machine_def<ResetChip_def> {
    // States
    struct Delay : public state<> {
        template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
            to.arm(fsm.ctx->current_time, std::chrono::milliseconds(fsm.ctx->slac_config.chip_reset.delay_ms));
        }

        timer to;
        bool state_timeout(timer::tp now) const {
            return to.expired(now);
        }
    };
    // clang-format off
    struct Sent      : public state<> { };
    struct Received  : public state<> { };
    struct Done      : public exit_pseudo_state<update> { };
    // clang-format on

    // Transitions
    using initial_state = Delay;
    // clang-format off
    struct transition_table : boost::mpl::vector<
        //    +----------+---------+----------+----------------+------------------+
        //    | Source   | Event   | Target   | Action         | Guard            |
        //    +----------+---------+----------+----------------+------------------+
        Row   < Delay    , update  , Sent     , send_message   , timeout          >,
        Row   < Sent     , message , Received , trigger_update , is_reset_message >,
        Row   < Sent     , update  , Done     , none           , reset_done       >,
        Row   < Received , update  , Done     , none           , none             >
        //    +----------+---------+----------+----------------+------------------+
        >{};
    // clang-format on
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
