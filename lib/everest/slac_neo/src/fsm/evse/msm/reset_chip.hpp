// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// ResetChip sub-machine: optional modem reset after the NMK was programmed (Reset -> ResetChip -> Idle).

#pragma once
#include "common.hpp"

namespace everest::lib::slac::msm::reset_chip_sm {

struct ResetChip_def : public state_machine_def<ResetChip_def> {
    // States
    struct Delay : public state<> {
        template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
            to.setDuration(std::chrono::milliseconds(fsm.ctx->slac_config.chip_reset.delay_ms));
            to.reset();
        }

        timer to;
        bool state_timeout() {
            return to.timeout();
        }
    };
    // clang-format off
    struct Sent      : public state<> { };
    struct Received  : public state<> { };
    struct Done      : public exit_pseudo_state<update> { };
    // clang-format on

    // Guards
    struct is_reset_message {
        template <class Fsm, class SrcT, class TarT> bool operator()(message const& e, Fsm&, SrcT&, TarT&) {
            const auto mmtype = e.payload.get_mmtype();
            auto expected = defs::qualcomm::MMTYPE_CM_RESET_DEVICE | defs::MMTYPE_MODE_CNF;
            return mmtype == expected;
        }
    };
    // The reset is done without waiting for a reply: the Lumissil CG5317 does not answer the reset
    // packet (Qualcomm replies, handled via Received; other chips do not support the chip reset).
    struct reset_done {
        template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            return fsm.ctx->modem_vendor == defs::ModemVendor::Lumissil;
        }
    };

    // Actions
    struct send_message {
        template <class Fsm, class Evt, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            auto& ctx = *fsm.ctx;
            if (ctx.modem_vendor == defs::ModemVendor::Qualcomm) {
                messages::qualcomm::cm_reset_device_req reset_req{};
                if (not ctx.send_slac_message(ctx.slac_config.plc_peer_mac, reset_req)) {
                    ctx.log_warn("Failed to send CM_RESET_DEVICE.REQ");
                }
            } else if (ctx.modem_vendor == defs::ModemVendor::Lumissil) {
                messages::lumissil::nscm_reset_device_req reset_req{};
                if (not ctx.send_slac_message(ctx.slac_config.plc_peer_mac, reset_req)) {
                    ctx.log_warn("Failed to send NSCM_RESET_DEVICE.REQ");
                }
            }
        }
    };

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
        ctx->status.match_state = SlacState::ResetChip;
        ctx->status.d3_state = D3State::Unmatched;
    }

    // Members
    fsm::evse::Context* ctx;
};

} // namespace everest::lib::slac::msm::reset_chip_sm
