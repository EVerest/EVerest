// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/service_detail.hpp>

#include <iso15118/d2/state/payment_service_selection.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/service_detail.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

message_2::ServiceDetailResponse handle_request(const message_2::ServiceDetailRequest& req,
                                                const dt::SessionId& session_id, uint16_t charge_service_id) {
    message_2::ServiceDetailResponse res;
    res.header.session_id = session_id;
    res.service_id = req.service_id;
    res.response_code =
        (req.service_id == charge_service_id) ? dt::ResponseCode::OK : dt::ResponseCode::FAILED_ServiceIDInvalid;
    return res;
}

void ServiceDetail::enter() {
    m_ctx.log.enter_state("ServiceDetail");
}

Result ServiceDetail::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_2::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    // ServiceDetail is optional: loop on ServiceDetailReq, otherwise hand the pending request to the
    // PaymentServiceSelection state (transition without consuming; the engine re-feeds it).
    if (m_ctx.peek_request_type() != message_2::Type::ServiceDetailReq) {
        return m_ctx.create_state<PaymentServiceSelection>();
    }

    const auto variant = m_ctx.pull_request();
    const auto req = variant->get<message_2::ServiceDetailRequest>();

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with
    // ServiceDetailRes/FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    const auto res = handle_request(req, m_ctx.get_session_id(), m_ctx.session_config.charge_service_id);
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
    }
    return {};
}

} // namespace iso15118::d2::state
