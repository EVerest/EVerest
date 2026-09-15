// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Init sub-machine: identifies the modem vendor by querying Qualcomm OP_ATTR and Lumissil
// NSCM_GET_VERSION, then exits to Reset.

#pragma once
#include "../../msm_helpers.hpp"
#include "common.hpp"
#include "guards_and_actions/init_logic.hpp"

#include <everest/slac/EvseSlacConfig.hpp>
#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>
#include <everest/slac/slac_utils.hpp>
#include <everest/slac/telemetry.hpp>

#include <boost/mpl/vector.hpp>
#include <boost/msm/front/completion_event.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/states.hpp>

#include <string>

namespace everest::lib::slac::msm::init_sm {

struct Init_def : public state_machine_def<Init_def> {
    // States
    // Each query state waits request_info_delay_ms for the modem's answer before moving on.
    using request_info_timeout_state = config_timeout_state<&fsm::evse::EvseSlacConfig::request_info_delay>;
    // clang-format off
    struct Init       : request_info_timeout_state { };
    struct OpAttr     : request_info_timeout_state { };
    struct GetVersion : request_info_timeout_state { };
    struct Done       : exit_pseudo_state<update> { };
    struct Other      : state<> { };
    // clang-format on
    struct Lumissil : public state<> {
        static std::string device_info(message const& e) {
            auto msg = e.payload.payload_as<messages::lumissil::nscm_get_version_cnf>();
            return msg ? utils::device_info(*msg) : std::string{};
        }
        static auto constexpr modem_vendor = defs::ModemVendor::Lumissil;
        static auto constexpr msg_type = defs::lumissil::MMTYPE_NSCM_GET_VERSION | defs::MMTYPE_MODE_CNF;
    };
    struct Qualcomm : public state<> {
        static std::string device_info(message const& e) {
            auto msg = e.payload.payload_as<messages::qualcomm::op_attr_cnf>();
            return msg ? utils::device_info(*msg) : std::string{};
        }
        static auto constexpr modem_vendor = defs::ModemVendor::Qualcomm;
        static auto constexpr msg_type = defs::qualcomm::MMTYPE_OP_ATTR | defs::MMTYPE_MODE_CNF;
    };

    // Transitions
    using initial_state = boost::mpl::vector<Init, Other>;
    using is_lumissil_msg = is_vendor_msg<Lumissil>;
    using is_qualcomm_msg = is_vendor_msg<Qualcomm>;
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
    template <class FSM, class Event> void no_transition(Event const&, FSM&, int) {
    }

    // Entry / exit
    template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        ctx->enter_state(SlacState::Init, D3State::Unmatched);
    }

    // Members
    fsm::evse::Context* ctx;
};

} // namespace everest::lib::slac::msm::init_sm
