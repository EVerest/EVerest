// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Guards and actions of the WaitForLink sub-machine (link detection after CM_SLAC_MATCH.CNF; see wait_for_link.hpp).

#pragma once
#include <everest/slac/MatchingSessionData.hpp>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>

namespace everest::lib::slac::msm::wait_for_link_sm {

// Guards
struct is_match_req {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const& e, Fsm& fsm, SrcT&, TarT&) {
        if (e.payload.get_mmtype() != (defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_REQ)) {
            return false;
        }
        auto const msg = e.payload.template payload_as<slac::messages::cm_slac_match_req>();
        if (not msg.has_value()) {
            return false;
        }
        if (not fsm.ctx->match_confirm_cache.valid) {
            return false;
        }
        auto const source_mac = e.payload.get_src_mac();
        if (source_mac == nullptr) {
            return false;
        }
        if (not wire_pointer_equal(source_mac, fsm.ctx->match_confirm_cache.ev_mac)) {
            return false;
        }
        fsm::evse::MatchingSessionData data(fsm.ctx->match_confirm_cache.ev_mac, fsm.ctx->match_confirm_cache.run_id,
                                            fsm.ctx->match_confirm_cache.evse_mac);
        return data.validate_message(*msg);
    }
};

// Actions
struct send_match_cnf {
    template <class Fsm, class Evt, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        auto& ctx = *fsm.ctx;
        if (not ctx.match_confirm_cache.valid) {
            return;
        }
        if (not ctx.send_slac_message(ctx.match_confirm_cache.ev_mac, ctx.match_confirm_cache.message)) {
            ctx.log_warn("Failed to send cached CM_SLAC_MATCH.CNF");
        }
    }
};

} // namespace everest::lib::slac::msm::wait_for_link_sm
