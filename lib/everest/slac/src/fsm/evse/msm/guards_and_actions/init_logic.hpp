// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Guards and actions of the Init sub-machine (modem vendor detection; see init.hpp).

#pragma once
#include "../../../msm_helpers.hpp"

#include <everest/slac/slac_messages.hpp>

namespace everest::lib::slac::msm::init_sm {

// Guards
// Matches the modem's answer for one vendor state (Init_def::Lumissil / Init_def::Qualcomm), which
// carries the expected MMTYPE as msg_type.
template <class VendorState> struct is_vendor_msg : public is_message_of_type<VendorState::msg_type> {};

// Actions
struct op_attr_req : public send_default_msg<messages::qualcomm::op_attr_req> {};
struct get_version_req : public send_default_msg<messages::lumissil::nscm_get_version_req> {};
struct set_modem_vendor {
    template <class Fsm, class SrcT, class TarT> void operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
        fsm.ctx->modem_vendor = TarT::modem_vendor;
        fsm.ctx->log_info(TarT::device_info(e));
    }
};

} // namespace everest::lib::slac::msm::init_sm
