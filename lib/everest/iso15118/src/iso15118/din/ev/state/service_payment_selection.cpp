// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/ev/state/service_payment_selection.hpp>

#include <iso15118/din/ev/state/contract_authentication.hpp>
#include <iso15118/din/ev/timeouts.hpp>

#include <iso15118/detail/din/ev/state/service_payment_selection.hpp>
#include <iso15118/detail/din/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::ev::state {

namespace service_payment_selection {

message_din::ServicePaymentSelectionRequest create_request(uint16_t charge_service_id) {
    message_din::ServicePaymentSelectionRequest req;
    req.selected_payment_option = dt::PaymentOption::ExternalPayment;
    req.selected_service_list.push_back(dt::SelectedService{charge_service_id, std::nullopt});
    return req;
}

Result handle_response(const message_din::ServicePaymentSelectionResponse& res) {
    return {res.response_code < dt::ResponseCode::FAILED};
}

} // namespace service_payment_selection

using namespace service_payment_selection;

void ServicePaymentSelection::enter() {
    m_ctx.log.enter_state("ServicePaymentSelection");
}

din::ev::Result ServicePaymentSelection::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        auto req = create_request(m_ctx.evse_info.charge_service_id);
        m_ctx.setup_header(req.header);
        m_ctx.send_request(req);
        m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("ServicePaymentSelection message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_din::ServicePaymentSelectionResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("ServicePaymentSelection failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }
        return m_ctx.create_state<ContractAuthentication>();
    }

    m_ctx.log("expected ServicePaymentSelectionRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::ev::state
