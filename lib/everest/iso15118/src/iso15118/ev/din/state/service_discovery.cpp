// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/service_discovery.hpp>

#include <algorithm>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/ev/detail/din/state/service_discovery.hpp>
#include <iso15118/ev/detail/din/state/state_helper.hpp>
#include <iso15118/ev/din/state/service_payment_selection.hpp>

namespace iso15118::ev::din::state {

namespace service_discovery {

message_din::ServiceDiscoveryRequest create_request() {
    // service_scope and service_category omitted.
    return {};
}

Result handle_response(const message_din::ServiceDiscoveryResponse& res, dt::EnergyTransferMode requested) {
    Result result;
    const auto& charge_service = res.charge_service;
    result.charge_service_id = charge_service.service_tag.service_id;

    // DIN SPEC numbers DC_core/DC_extended identically in both enums, so the offered ChargeService
    // supports the request when the underlying values match.
    result.charge_service_supported = (message_din::to_underlying_value(charge_service.energy_transfer_type) ==
                                       message_din::to_underlying_value(requested));

    result.eim_offered =
        std::any_of(res.payment_options.begin(), res.payment_options.end(),
                    [](dt::PaymentOption option) { return option == dt::PaymentOption::ExternalPayment; });
    return result;
}

} // namespace service_discovery

void ServiceDiscovery::enter() {
    logf_debug("Enter state: ServiceDiscovery (DIN 70121)");
    m_ctx.send_request(service_discovery::create_request());
}

Result ServiceDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_din::ServiceDiscoveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    const auto result =
        service_discovery::handle_response(*res, din_energy_transfer_mode(m_ctx.params().energy_transfer_mode));

    if (not result.eim_offered) {
        logf_error("SECC does not offer ExternalPayment, stopping the session");
        return m_ctx.create_state<SessionStop>();
    }
    if (not result.charge_service_supported) {
        logf_error("SECC ChargeService does not support the requested energy transfer mode, stopping the session");
        return m_ctx.create_state<SessionStop>();
    }

    m_ctx.evse_info.charge_service_id = result.charge_service_id;

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }
    return m_ctx.create_state<ServicePaymentSelection>();
}

} // namespace iso15118::ev::din::state
