// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/payment_service_selection.hpp>

#include <iso15118/d2/ev/state/authorization.hpp>
#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/ev/state/payment_service_selection.hpp>
#include <iso15118/detail/d2/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::ev::state {

namespace payment_service_selection {

message_2::PaymentServiceSelectionRequest create_request(uint16_t charge_service_id) {
    message_2::PaymentServiceSelectionRequest req;
    req.selected_payment_option = dt::PaymentOption::ExternalPayment;
    req.selected_service_list.push_back(dt::SelectedService{charge_service_id, std::nullopt});
    return req;
}

Result handle_response(const message_2::PaymentServiceSelectionResponse& res) {
    return {res.response_code < dt::ResponseCode::FAILED};
}

} // namespace payment_service_selection

using namespace payment_service_selection;

void PaymentServiceSelection::enter() {
    m_ctx.log.enter_state("PaymentServiceSelection");
}

d2::ev::Result PaymentServiceSelection::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        auto req = create_request(m_ctx.evse_info.selected_charge_service_id);
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
        m_ctx.log("PaymentServiceSelection message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_2::PaymentServiceSelectionResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("PaymentServiceSelection failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }
        return m_ctx.create_state<Authorization>();
    }

    m_ctx.log("expected PaymentServiceSelectionRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::ev::state
