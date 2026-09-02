// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Guards and actions of the ResetChip sub-machine (modem reset after CM_SET_KEY; see reset_chip.hpp).

#pragma once
#include "common.hpp"

namespace everest::lib::slac::msm::reset_chip_sm {

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

} // namespace everest::lib::slac::msm::reset_chip_sm
