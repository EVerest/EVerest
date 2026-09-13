// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/service_discovery.hpp>

#include <algorithm>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/payment_service_selection.hpp>
#include <iso15118/ev/d2/state/session_stop.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/service_discovery.hpp>

namespace iso15118::ev::d2::state {

namespace service_discovery {

message_2::ServiceDiscoveryRequest create_request() {
    return {};
}

Result handle_response(const message_2::ServiceDiscoveryResponse& res, dt::EnergyTransferMode requested_mode) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    result.charge_service_id = res.charge_service.service_id;

    const auto& modes = res.charge_service.supported_energy_transfer_mode;
    result.mode_supported = std::find(modes.begin(), modes.end(), requested_mode) != modes.end();

    const auto& options = res.payment_option_list;
    result.eim_offered = std::any_of(options.begin(), options.end(),
                                     [](dt::PaymentOption o) { return o == dt::PaymentOption::ExternalPayment; });
    result.contract_offered = std::any_of(options.begin(), options.end(),
                                          [](dt::PaymentOption o) { return o == dt::PaymentOption::Contract; });

    // The Certificate service (ServiceID 2) is advertised in the optional service_list.
    if (res.service_list.has_value()) {
        const auto& services = res.service_list.value();
        result.certificate_service_offered = std::any_of(services.begin(), services.end(), [](const dt::Service& s) {
            return s.service_id == dt::CERTIFICATE_SERVICE_ID;
        });
    }

    return result;
}

} // namespace service_discovery

void ServiceDiscovery::enter() {
    logf_debug("Enter state: ServiceDiscovery (ISO 15118-2)");
    m_ctx.send_request(service_discovery::create_request());
}

Result ServiceDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_2::ServiceDiscoveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    const auto requested_mode = m_ctx.params().energy_transfer_mode;
    const auto result = service_discovery::handle_response(*res, requested_mode);

    // Contract needs both an EV preference and a SECC offer; otherwise the EV needs EIM.
    // enforce_contract selects Contract regardless of the offer ([V2G2-135] robustness test).
    const auto& pnc = m_ctx.params().pnc;
    const bool want_contract = pnc.enforce_contract or (pnc.prefer_contract and result.contract_offered);
    if (not result.eim_offered and not want_contract) {
        logf_error("SECC offers no usable payment option; stopping the session");
        return Result{m_ctx.create_state<SessionStop>()};
    }

    if (not result.mode_supported) {
        // Continue anyway: ChargeParameterDiscovery carries the requested mode and the SECC rejects it
        // with FAILED_WrongEnergyTransferMode [V2G2-464], which surfaces the mismatch.
        logf_warning("SECC does not offer the requested energy transfer mode; expecting a "
                     "ChargeParameterDiscovery rejection");
    }

    m_ctx.evse_info.selected_charge_service_id = result.charge_service_id;
    m_ctx.evse_info.contract_offered = result.contract_offered;
    m_ctx.evse_info.certificate_service_offered = result.certificate_service_offered;

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }
    return m_ctx.create_state<PaymentServiceSelection>();
}

} // namespace iso15118::ev::d2::state
