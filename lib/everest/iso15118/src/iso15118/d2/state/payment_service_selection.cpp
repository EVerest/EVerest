// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/authorization.hpp>
#include <iso15118/d2/state/payment_service_selection.hpp>
#include <iso15118/message/d2/payment_service_selection.hpp>
#include <iso15118/message/d2/service_detail.hpp>

#include <algorithm>
#include <iso15118/detail/d2/context_helper.hpp>
#include <iso15118/detail/helper.hpp>
#include <optional>
#include <vector>

namespace iso15118::d2::state {

namespace dt = d2::msg::data_types;

d2::msg::PaymentServiceSelectionResponse
handle_request([[maybe_unused]] const d2::msg::PaymentServiceSelectionRequest& req, d2::Session& session,
               bool processing_ok) {

    d2::msg::PaymentServiceSelectionResponse res;
    setup_header(res.header, session);

    // [V2G2-552]
    if (not processing_ok) {
        return response_with_code(res, dt::ResponseCode::FAILED);
    }

    // [V2G2-551]
    return response_with_code(res, dt::ResponseCode::OK);
}

d2::msg::ServiceDetailResponse handle_request([[maybe_unused]] const d2::msg::ServiceDetailRequest& req,
                                              d2::Session& session, dt::ServiceID service_id,
                                              std::optional<dt::ServiceParameterList> service_parameters) {

    d2::msg::ServiceDetailResponse res;
    setup_header(res.header, session);

    res.service_id = service_id;
    res.service_parameter_list = service_parameters;

    // [V2G2-545]
    return response_with_code(res, dt::ResponseCode::OK);
}

void PaymentServiceSelection::enter() {
    // m_ctx.log.enter_state("PaymentServiceSelection");
}

Result PaymentServiceSelection::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<msg::PaymentServiceSelectionRequest>()) {
        // [V2G2-550] process PaymentServiceSelectionRequest

        bool processing_ok = false;

        bool payment_option_supported =
            std::find(m_ctx.session_config.supported_payment_options.begin(),
                      m_ctx.session_config.supported_payment_options.end(),
                      req->selected_payment_option) != m_ctx.session_config.supported_payment_options.end();
        if (payment_option_supported) {
            m_ctx.session.set_selected_payment_option(req->selected_payment_option);
            processing_ok = true;
        } else {
            // m_ctx.log("Payment option not supported");
        }

        for (const auto& selected_service : req->selected_service_list) {
            m_ctx.session.select_service(selected_service);
        }

        const auto res = handle_request(*req, m_ctx.session, processing_ok);

        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            // m_ctx.log("Error processing PaymentServiceSelectionReq");
            // [V2G2-539] Terminate session after ResponseCode::FAILED
            m_ctx.session_stopped = true;
            return {};
        }

        return m_ctx.create_state<Authorization>();
    } else if (const auto req = variant->get_if<msg::ServiceDetailRequest>()) {
        // [V2G2-547] process ServiceDetailRequest
        std::optional<dt::ServiceParameterList> service_parameters(std::nullopt);
        service_parameters = m_ctx.session.get_service_parameter_list(req->service_id);
        const auto res = handle_request(*req, m_ctx.session, req->service_id, service_parameters);

        m_ctx.respond(res);

        // [V2G2-549]
        if (res.response_code >= dt::ResponseCode::FAILED) {
            // m_ctx.log("Error processing ServiceDetailReq");
            // [V2G2-539] Terminate session after ResponseCode::FAILED
            m_ctx.session_stopped = true;
            return {};
        }

        // [V2G2-548] respond with OK and persist in this state
        return {};
    }
    // m_ctx.log("expected PaymentServiceSelectionReq! But code type id: %d", variant->get_type());

    // Sequence Error [V2G2-538]
    const msg::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);

    m_ctx.session_stopped = true;

    return {};
}

} // namespace iso15118::d2::state
