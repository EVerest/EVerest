// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Matched sub-machine: the AVLN is up. Polls the modem link status (vendor specific), debounces
// link loss and runs the CM_AMP_MAP exchange. Exits to Failed when the link is lost.

#pragma once
#include "../amp_map_handler.hpp"
#include "common.hpp"
#include "matched_actions.hpp"

namespace everest::lib::slac::msm::matched_sm {

struct Matched_def : public state_machine_def<Matched_def> {
    // States
    // clang-format off
    struct Init          : public state<> { };
    struct NoDetect      : public state<> { };
    struct Other         : public state<> { };
    struct Failed        : public exit_pseudo_state<message> { };
    // clang-format on

    // Transitions
    using initial_state = Init;
    using link_lost = And_<link_status_neg, neg_threshold_reached>;
    using link_flap = And_<link_status_neg, Not_<neg_threshold_reached>>;
    using link_detection_off = Not_<detect_link>;
    // clang-format off
    struct transition_table : boost::mpl::vector<
        //    +----------+---------+----------+-----------------------+--------------------------------+
        //    | Source   | Event   | Target   | Action                | Guard                          |
        //    +----------+---------+----------+-----------------------+--------------------------------+
        Row   < Init     , none    , Lumissil , link_status_req       , And_<detect_link, is_lumissil> >,
        Row   < Init     , none    , Qualcomm , link_status_req       , And_<detect_link, is_qualcomm> >,
        Row   < Init     , none    , NoDetect , none                  , link_detection_off             >,
        //    +----------+---------+----------+-----------------------+--------------------------------+
        Row   < Lumissil , update  , Lumissil , link_status_req       , timeout                        >,
        Row   < Lumissil , update  , Lumissil , retransmit_amp_map    , amp_map_retransmit_due         >,
        Row   < Lumissil , message , Failed   , none                  , link_lost                      >,
        Row   < Lumissil , message , Lumissil , count_link_status_neg , link_flap                      >,
        Row   < Lumissil , message , Lumissil , clear_link_status_neg , link_status_cnf                >,
        Row   < Lumissil , message , Lumissil , send_amp_map_cnf      , is_amp_map_req                 >,
        Row   < Lumissil , message , Lumissil , amp_map_cnf_ack       , is_amp_map_cnf_ok              >,
        //    +----------+---------+----------+-----------------------+--------------------------------+
        Row   < Qualcomm , update  , Qualcomm , link_status_req       , timeout                        >,
        Row   < Qualcomm , update  , Qualcomm , retransmit_amp_map    , amp_map_retransmit_due         >,
        Row   < Qualcomm , message , Failed   , none                  , link_lost                      >,
        Row   < Qualcomm , message , Qualcomm , count_link_status_neg , link_flap                      >,
        Row   < Qualcomm , message , Qualcomm , clear_link_status_neg , link_status_cnf                >,
        Row   < Qualcomm , message , Qualcomm , send_amp_map_cnf      , is_amp_map_req                 >,
        Row   < Qualcomm , message , Qualcomm , amp_map_cnf_ack       , is_amp_map_cnf_ok              >,
        //    +----------+---------+----------+-----------------------+--------------------------------+
        Row   < NoDetect , message , NoDetect , send_amp_map_cnf      , is_amp_map_req                 >,
        Row   < NoDetect , update  , NoDetect , retransmit_amp_map    , amp_map_retransmit_due         >,
        Row   < NoDetect , message , NoDetect , amp_map_cnf_ack       , is_amp_map_cnf_ok              >,
        Row   < Other    , message , Other    , send_amp_map_cnf      , is_amp_map_req                 >,
        Row   < Init     , message , Init     , send_amp_map_cnf      , is_amp_map_req                 >
        //    +----------+---------+----------+-----------------------+--------------------------------+
        >{};
    // clang-format on

    template <class FSM, class Event> void no_transition(Event const&, FSM&, int) {
    }

    // Entry / exit
    template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        {
            std::ostringstream ss;
            ss << "Entered Matched state (EV " << format_mac_addr(ctx->status.ev_mac) << ", avg. attenuation "
               << std::fixed << std::setprecision(1) << ctx->status.average_attenuation << " dB)";
            ctx->enter_state(SlacState::Matched, D3State::Matched, ss.str());
        }
        ctx->clear_match_confirm_cache();
        ctx->signal_dlink_ready(true);
        link_check_to_ms = ctx->slac_config.link_status.poll_in_matched_state_ms;
        consecutive_neg_link_status = 0;
        neg_link_status_threshold =
            ctx->slac_config.link_status.debounce_count < 1 ? 1 : ctx->slac_config.link_status.debounce_count;
        ctx->status.modem_link_ready = true;
        amp_map.start(*ctx);
    }

    template <class Event, class Fsm> void on_exit(Event const&, Fsm&) {
        ctx->signal_dlink_ready(false);
        ctx->status.ev_mac.fill(0);
        ctx->status.average_attenuation = 0.f;
        ctx->status.modem_link_ready = false;
        ctx->clear_match_confirm_cache();
    }

    // Members
    fsm::evse::Context* ctx;
    int link_check_to_ms{0};
    int consecutive_neg_link_status{0};
    int neg_link_status_threshold{1};
    fsm::evse::AmpMapHandler amp_map; // SECC-initiated CM_AMP_MAP exchange (ISO 15118-3 A.9.6)
};

} // namespace everest::lib::slac::msm::matched_sm
