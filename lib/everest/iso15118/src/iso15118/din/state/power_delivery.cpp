// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/power_delivery.hpp>

#include <iso15118/din/state/current_demand.hpp>
#include <iso15118/din/state/session_stop.hpp>
#include <iso15118/din/state/welding_detection.hpp>

#include <iso15118/detail/din/state/power_delivery.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

message_din::PowerDeliveryResponse handle_request(const message_din::PowerDeliveryRequest& req,
                                                  const dt::SessionId& session_id) {
    message_din::PowerDeliveryResponse res;

    if (not validate_and_setup_header(res.header, session_id, req.header.session_id)) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    dt::DcEvseStatus status;
    status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    status.evse_isolation_status = dt::IsolationLevel::Valid;
    res.dc_evse_status = status;

    return response_with_code(res, dt::ResponseCode::OK);
}

void PowerDelivery::enter() {
    m_ctx.log.enter_state("PowerDelivery");
}

Result PowerDelivery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_din::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::PowerDeliveryRequest>()) {
        const auto res = handle_request(*req, m_ctx.get_session_id());
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        if (req->ready_to_charge_state) {
            // Contactor closed / power path established -> the charge loop can start.
            m_ctx.feedback.signal(session::feedback::Signal::SETUP_FINISHED);
            return m_ctx.create_state<CurrentDemand>();
        }

        // End of charging: open the contactor and move on to welding detection.
        m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_FINISHED);
        m_ctx.feedback.signal(session::feedback::Signal::DC_OPEN_CONTACTOR);
        return m_ctx.create_state<WeldingDetection>();
    }

    m_ctx.log("expected PowerDeliveryReq! But code type id: %d", variant->get_type());
    message_din::PowerDeliveryResponse res;
    setup_header(res.header, m_ctx.get_session_id());
    m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED_SequenceError));
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
