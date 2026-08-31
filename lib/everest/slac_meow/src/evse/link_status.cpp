// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/detail/link_status.hpp>

#include <everest/slac/protocol/messages.hpp>

namespace everest::slac::evse {

LinkCheckMode link_check_mode_for(defs::ModemVendor vendor) {
    switch (vendor) {
    case defs::ModemVendor::Lumissil:
        return LinkCheckMode::Lumissil;
    case defs::ModemVendor::Qualcomm:
        return LinkCheckMode::Qualcomm;
    default:
        return LinkCheckMode::None;
    }
}

void send_link_status_req(Context& ctx, LinkCheckMode mode) {
    if (mode == LinkCheckMode::Lumissil) {
        messages::lumissil::nscm_get_d_link_status_req request{};
        if (not ctx.send_slac_message(ctx.slac_config.plc_peer_mac, request)) {
            ctx.log_warn("Failed to send CM_GET_D_LINK_STATUS.REQ to SLAC peer");
        }
        return;
    }
    if (mode == LinkCheckMode::Qualcomm) {
        messages::qualcomm::link_status_req request{};
        if (not ctx.send_slac_message(ctx.slac_config.plc_peer_mac, request)) {
            ctx.log_warn("Failed to send LINK_STATUS.REQ to SLAC peer");
        }
    }
}

bool parse_link_status_cnf(messages::HomeplugMessage const& frame, LinkCheckMode mode, bool& linked) {
    if (mode == LinkCheckMode::Lumissil) {
        if (frame.get_mmtype() != defs::lumissil::MMTYPE_NSCM_GET_D_LINK_STATUS_CNF) {
            return false;
        }
        auto const msg = frame.payload_as<messages::lumissil::nscm_get_d_link_status_cnf>();
        if (not msg.has_value()) {
            return false;
        }
        linked = (msg->link_status == defs::D_LINK_STATUS_LINKED);
        return true;
    }

    if (mode == LinkCheckMode::Qualcomm) {
        if (frame.get_mmtype() != defs::qualcomm::MMTYPE_LINK_STATUS_CNF) {
            return false;
        }
        auto const msg = frame.payload_as<messages::qualcomm::link_status_cnf>();
        if (not msg.has_value()) {
            return false;
        }
        linked = (msg->link_status == defs::D_LINK_STATUS_LINKED);
        return true;
    }

    return false;
}

bool is_link_up(messages::HomeplugMessage const& frame, LinkCheckMode mode) {
    bool linked{false};
    return parse_link_status_cnf(frame, mode, linked) and linked;
}

bool is_link_down(messages::HomeplugMessage const& frame, LinkCheckMode mode) {
    bool linked{false};
    return parse_link_status_cnf(frame, mode, linked) and not linked;
}

} // namespace everest::slac::evse
