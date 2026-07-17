// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/ev/state/service_discovery.hpp>

#include <algorithm>

#include <iso15118/din/ev/state/service_payment_selection.hpp>
#include <iso15118/din/ev/timeouts.hpp>

#include <iso15118/detail/din/ev/state/service_discovery.hpp>
#include <iso15118/detail/din/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::ev::state {

namespace service_discovery {

message_din::ServiceDiscoveryRequest create_request() {
    return {}; // service_scope and service_category omitted
}

Result handle_response(const message_din::ServiceDiscoveryResponse& res, dt::EnergyTransferMode requested) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    const auto& charge_service = res.charge_service;
    result.charge_service_id = charge_service.service_tag.service_id;
    result.offered_energy_transfer_mode = charge_service.energy_transfer_type;

    // DIN SPEC numbers DC_core/DC_extended identically in both enums, so the offered ChargeService
    // supports the request when the underlying values match.
    result.charge_service_supported = (message_din::to_underlying_value(charge_service.energy_transfer_type) ==
                                       message_din::to_underlying_value(requested));

    result.eim_offered = std::any_of(res.payment_options.begin(), res.payment_options.end(),
                                     [](dt::PaymentOption o) { return o == dt::PaymentOption::ExternalPayment; });

    return result;
}

} // namespace service_discovery

using namespace service_discovery;

void ServiceDiscovery::enter() {
    m_ctx.log.enter_state("ServiceDiscovery");
}

din::ev::Result ServiceDiscovery::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        auto req = create_request();
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
        m_ctx.log("ServiceDiscovery message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_din::ServiceDiscoveryResponse>()) {
        const auto result = handle_response(*res, m_ctx.session_config.requested_energy_transfer_type);

        if (not result.valid) {
            m_ctx.log("ServiceDiscovery failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (not result.eim_offered) {
            m_ctx.log("SECC does not offer ExternalPayment, terminating session");
            m_ctx.pending_stop = ChargingSession::Terminate;
            return m_ctx.create_state<SessionStop>();
        }

        if (not result.charge_service_supported) {
            m_ctx.log("SECC ChargeService does not support the requested energy transfer type, stopping session");
            m_ctx.pending_stop = ChargingSession::Terminate;
            return m_ctx.create_state<SessionStop>();
        }

        m_ctx.evse_info.charge_service_id = result.charge_service_id;
        m_ctx.evse_info.offered_energy_transfer_mode = result.offered_energy_transfer_mode;

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }
        return m_ctx.create_state<ServicePaymentSelection>();
    }

    m_ctx.log("expected ServiceDiscoveryRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::ev::state
