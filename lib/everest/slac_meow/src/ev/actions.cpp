// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/ev/detail/actions.hpp>

#include <endian.h>

#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>

namespace everest::slac::ev {

void start_matching(Context& ctx) {
    ctx.clear_session();
    randomize(ctx.active_session.run_id);

    send_slac_parm_req(ctx);
    ctx.log_info("EV MSM start matching");
}

void send_slac_parm_req(Context& ctx) {
    messages::cm_slac_parm_req req{};
    req.application_type = defs::COMMON_APPLICATION_TYPE;
    req.security_type = defs::COMMON_SECURITY_TYPE;
    copy_to_wire(req.run_id, ctx.active_session.run_id);

    if (not ctx.send_slac_message(Context::BROADCAST_MAC, req)) {
        ctx.log_warn("Failed to send CM_SLAC_PARM.REQ");
    }
    ctx.parm_req_attempt_count++;
}

void send_slac_match_req(Context& ctx) {
    messages::cm_slac_match_req match_req{};
    match_req.application_type = defs::COMMON_APPLICATION_TYPE;
    match_req.security_type = defs::COMMON_SECURITY_TYPE;
    match_req.mvf_length = htole16(defs::mme::slac_match_req::MVF_LENGTH);
    zero_wire(match_req.pev_id);
    copy_to_wire(match_req.pev_mac, ctx.ev_host_mac);
    zero_wire(match_req.evse_id);
    copy_to_wire(match_req.evse_mac, ctx.active_session.evse_mac);
    copy_to_wire(match_req.run_id, ctx.active_session.run_id);
    zero_wire(match_req._reserved);

    if (not ctx.send_slac_message(ctx.active_session.evse_mac, match_req)) {
        ctx.log_warn("Failed to send CM_SLAC_MATCH.REQ");
    }
    ctx.match_req_attempt_count++;
}

void send_atten_char_rsp_and_match_req(Context& ctx, messages::HomeplugMessage const& frame) {
    auto const atten_char_ind = frame.payload_as<messages::cm_atten_char_ind>();
    if (not atten_char_ind.has_value()) {
        ctx.log_warn("Received CM_ATTEN_CHAR.IND with invalid payload");
        return;
    }

    messages::cm_atten_char_rsp rsp{};
    rsp.application_type = defs::COMMON_APPLICATION_TYPE;
    rsp.security_type = defs::COMMON_SECURITY_TYPE;
    copy_to_wire(rsp.source_address, ctx.ev_host_mac);
    copy_to_wire(rsp.run_id, ctx.active_session.run_id);
    zero_wire(rsp.source_id);
    zero_wire(rsp.resp_id);
    rsp.result = defs::mme::atten_char_rsp::RESULT;

    if (not ctx.send_slac_message(ctx.active_session.evse_mac, rsp)) {
        ctx.log_warn("Failed to send CM_ATTEN_CHAR.RSP");
    }

    send_slac_match_req(ctx);
}

} // namespace everest::slac::ev
