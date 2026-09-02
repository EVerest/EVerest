// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Init sub-machine: identifies the modem vendor by querying Qualcomm OP_ATTR and Lumissil
// NSCM_GET_VERSION, then exits to Reset.

#pragma once
#include "common.hpp"

namespace everest::lib::slac::msm::init_sm {

struct Init_def        : public state_machine_def<Init_def> {
    // States
    struct Init       : timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
            timeout_state::state_timeout_ms = fsm.ctx->slac_config.request_info_delay_ms;
            timeout_state::on_entry(e, fsm);
        }
    };
    struct OpAttr     : timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
            timeout_state::state_timeout_ms = fsm.ctx->slac_config.request_info_delay_ms;
            timeout_state::on_entry(e, fsm);
        }
    };
    struct GetVersion : timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
            timeout_state::state_timeout_ms = fsm.ctx->slac_config.request_info_delay_ms;
            timeout_state::on_entry(e, fsm);
        }
    };
    // clang-format off
    struct Done       : exit_pseudo_state<update> { };
    struct Other      : state<> { };
    // clang-format on
    struct Lumissil   : public state<> {
        static std::string device_info(message const& e) {
            auto msg = e.payload.payload_as<messages::lumissil::nscm_get_version_cnf>();
            return msg ? utils::device_info(*msg) : std::string{};
        }
        static auto constexpr modem_vendor = defs::ModemVendor::Lumissil;
        static auto constexpr msg_type = defs::lumissil::MMTYPE_NSCM_GET_VERSION | defs::MMTYPE_MODE_CNF;
    };
    struct Qualcomm   : public state<> {
        static std::string device_info(message const& e) {
            auto msg = e.payload.payload_as<messages::qualcomm::op_attr_cnf>();
            return msg ? utils::device_info(*msg) : std::string{};
        }
        static auto constexpr modem_vendor = defs::ModemVendor::Qualcomm;
        static auto constexpr msg_type = defs::qualcomm::MMTYPE_OP_ATTR | defs::MMTYPE_MODE_CNF;
    };

    // Guards
    struct is_qualcomm_msg : public is_message_of_type<Qualcomm::msg_type>{ };
    struct is_lumissil_msg : public is_message_of_type<Lumissil::msg_type>{ };

    // Actions
    struct op_attr_req : public send_default_msg<messages::qualcomm::op_attr_req> { };
    struct get_version_req : public send_default_msg<messages::lumissil::nscm_get_version_req> { };
    struct set_modem_vendor {
        template <class Fsm, class SrcT, class TarT>
        void operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
            fsm.ctx->modem_vendor = TarT::modem_vendor;
            fsm.ctx->log_info(TarT::device_info(e));
        }

    };

    // Transitions
    using initial_state = boost::mpl::vector<Init, Other>;
    // clang-format off
    struct transition_table : boost::mpl::vector<
        //    +------------+---------+------------+------------------+-----------------+
        //    | Source     | Event   | Target     | Action           | Guard           |
        //    +------------+---------+------------+------------------+-----------------+
        Row   < Init       , update  , OpAttr     , op_attr_req      , timeout         >,
        Row   < OpAttr     , update  , GetVersion , get_version_req  , timeout         >,
        Row   < GetVersion , update  , Done       , none             , timeout         >,
        //    +------------+---------+------------+------------------+-----------------+
        Row   < Other      , message , Lumissil   , set_modem_vendor , is_lumissil_msg >,
        Row   < Other      , message , Qualcomm   , set_modem_vendor , is_qualcomm_msg >
        //    +------------+---------+------------+------------------+-----------------+
        >{};
    // clang-format on
    template <class FSM,class Event>
    void no_transition(Event const&, FSM&, int) { }

    // Entry / exit
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        ctx->status.match_state = SlacState::Init;
        ctx->status.d3_state = D3State::Unmatched;
    }

    // Members
    fsm::evse::Context* ctx;
};

} // namespace everest::lib::slac::msm::init_sm
