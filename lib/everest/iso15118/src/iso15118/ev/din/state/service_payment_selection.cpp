// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/service_payment_selection.hpp>

#include <optional>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/ev/detail/din/state/service_payment_selection.hpp>
#include <iso15118/ev/din/state/contract_authentication.hpp>

namespace iso15118::ev::din::state {

namespace service_payment_selection {

namespace dt = message_din::datatypes;

message_din::ServicePaymentSelectionRequest create_request(uint16_t charge_service_id) {
    message_din::ServicePaymentSelectionRequest req;
    req.selected_payment_option = dt::PaymentOption::ExternalPayment;
    req.selected_service_list.push_back(dt::SelectedService{charge_service_id, std::nullopt});
    return req;
}

} // namespace service_payment_selection

void ServicePaymentSelection::enter() {
    logf_debug("Enter state: ServicePaymentSelection (DIN 70121)");
    m_ctx.send_request(service_payment_selection::create_request(m_ctx.evse_info.charge_service_id));
}

Result ServicePaymentSelection::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_din::ServicePaymentSelectionResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }
    return m_ctx.create_state<ContractAuthentication>();
}

} // namespace iso15118::ev::din::state
