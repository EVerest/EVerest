// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/service_payment_selection.hpp>

#include <iso15118/din/state/contract_authentication.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/service_payment_selection.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

message_din::ServicePaymentSelectionResponse handle_request(const message_din::ServicePaymentSelectionRequest& req,
                                                            uint16_t charge_service_id,
                                                            const dt::SessionId& session_id) {
    message_din::ServicePaymentSelectionResponse res;

    if (not validate_and_setup_header(res.header, session_id, req.header.session_id)) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    // [V2G-DC-395] Only ExternalPayment is allowed in DIN 70121.
    if (req.selected_payment_option != dt::PaymentOption::ExternalPayment) {
        return response_with_code(res, dt::ResponseCode::FAILED_PaymentSelectionInvalid);
    }

    // [V2G-DC-396/635] The selected service list shall contain exactly the charge service.
    if (req.selected_service_list.size() != 1 or
        req.selected_service_list.front().service_id != charge_service_id) {
        return response_with_code(res, dt::ResponseCode::FAILED_ServiceSelectionInvalid);
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void ServicePaymentSelection::enter() {
    m_ctx.log.enter_state("ServicePaymentSelection");
}

Result ServicePaymentSelection::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_din::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::ServicePaymentSelectionRequest>()) {
        const auto res = handle_request(*req, m_ctx.session_config.charge_service_id, m_ctx.get_session_id());
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        return m_ctx.create_state<ContractAuthentication>();
    }

    m_ctx.log("expected ServicePaymentSelectionReq! But code type id: %d", variant->get_type());
    message_din::ServicePaymentSelectionResponse res;
    setup_header(res.header, m_ctx.get_session_id());
    m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED_SequenceError));
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
