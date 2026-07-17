// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/service_discovery.hpp>

#include <iso15118/din/state/service_payment_selection.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/service_discovery.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

message_din::ServiceDiscoveryResponse handle_request(const message_din::ServiceDiscoveryRequest& req,
                                                     const SessionConfig& config, const dt::SessionId& session_id) {
    message_din::ServiceDiscoveryResponse res;

    if (not validate_and_setup_header(res.header, session_id, req.header.session_id)) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    // [V2G-DC-628/629] ServiceName / ServiceScope shall not be used in DIN 70121.
    res.charge_service.service_tag.service_id = config.charge_service_id;
    res.charge_service.service_tag.service_category = dt::ServiceCategory::EVCharging;
    res.charge_service.free_service = config.free_service;
    res.charge_service.energy_transfer_type = config.energy_transfer_mode;

    // In the scope of DIN 70121 only ExternalPayment shall be offered.
    res.payment_options = {dt::PaymentOption::ExternalPayment};

    return response_with_code(res, dt::ResponseCode::OK);
}

void ServiceDiscovery::enter() {
    m_ctx.log.enter_state("ServiceDiscovery");
}

Result ServiceDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_din::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::ServiceDiscoveryRequest>()) {
        const auto res = handle_request(*req, m_ctx.session_config, m_ctx.get_session_id());
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        return m_ctx.create_state<ServicePaymentSelection>();
    }

    m_ctx.log("expected ServiceDiscoveryReq! But code type id: %d", variant->get_type());
    message_din::ServiceDiscoveryResponse res;
    setup_header(res.header, m_ctx.get_session_id());
    m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED_SequenceError));
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
