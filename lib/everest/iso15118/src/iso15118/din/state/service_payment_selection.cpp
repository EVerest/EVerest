// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/service_payment_selection.hpp>

#include <iso15118/din/state/contract_authentication.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/service_payment_selection.hpp>
#include <iso15118/detail/din/state/session_stop.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

message_din::ServicePaymentSelectionResponse handle_request(const message_din::ServicePaymentSelectionRequest& req,
                                                            uint16_t charge_service_id,
                                                            const dt::SessionId& session_id) {
    message_din::ServicePaymentSelectionResponse res;

    setup_header(res.header, session_id);

    // [V2G-DC-395] Only ExternalPayment is allowed in DIN 70121.
    if (req.selected_payment_option != dt::PaymentOption::ExternalPayment) {
        return response_with_code(res, dt::ResponseCode::FAILED_PaymentSelectionInvalid);
    }

    // [V2G-DC-396/635]: the selected service list contains exactly the charge service.
    if (req.selected_service_list.size() != 1 or req.selected_service_list.front().service_id != charge_service_id) {
        return response_with_code(res, dt::ResponseCode::FAILED_ServiceSelectionInvalid);
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void ServicePaymentSelection::enter() {
    logf_debug("Enter state: ServicePaymentSelection");
}

Result ServicePaymentSelection::on_request(const message_din::Variant& received) {
    if (const auto req = received.get_if<message_din::ServicePaymentSelectionRequest>()) {
        const auto res = handle_request(*req, m_ctx.session_config.charge_service_id, m_ctx.get_session_id());
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        // Always ExternalPayment in DIN 70121 [V2G-DC-395]; EvseV2G leaves this unpublished on its DIN path.
        m_ctx.feedback.selected_payment_option(req->selected_payment_option);

        return m_ctx.create_state<ContractAuthentication>();
    }

    // [V2G-DC-441] admits a SessionStopReq here.
    if (const auto stop = received.get_if<message_din::SessionStopRequest>()) {
        return process_session_stop(m_ctx, *stop);
    }

    logf_warning("Expected ServicePaymentSelectionReq or SessionStopReq! But code type id: %d", received.get_type());
    respond_sequence_error(m_ctx, received);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
