// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/service_discovery.hpp>

#include <iso15118/din/state/service_payment_selection.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/service_discovery.hpp>
#include <iso15118/detail/din/state/session_stop.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

message_din::ServiceDiscoveryResponse handle_request([[maybe_unused]] const message_din::ServiceDiscoveryRequest& req,
                                                     const SessionConfig& config, const dt::SessionId& session_id) {
    message_din::ServiceDiscoveryResponse res;
    setup_header(res.header, session_id);

    // Mandatory in ServiceDiscoveryRes even on a FAILED_UnknownSession response
    // (TC_SECC_VTB_ServiceDiscovery_003), so populate them before the check below.
    // [V2G-DC-628/629]: no ServiceName / ServiceScope in DIN 70121.
    res.charge_service.service_tag.service_id = config.charge_service_id;
    res.charge_service.service_tag.service_category = dt::ServiceCategory::EVCharging;
    res.charge_service.free_service = config.free_service;
    res.charge_service.energy_transfer_type = config.energy_transfer_mode;

    // DIN 70121 offers ExternalPayment only.
    res.payment_options = {dt::PaymentOption::ExternalPayment};
    return response_with_code(res, dt::ResponseCode::OK);
}

void ServiceDiscovery::enter() {
    logf_debug("Enter state: ServiceDiscovery");
}

Result ServiceDiscovery::on_request(const message_din::Variant& received) {
    if (const auto req = received.get_if<message_din::ServiceDiscoveryRequest>()) {
        const auto res = handle_request(*req, m_ctx.session_config, m_ctx.get_session_id());
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        return m_ctx.create_state<ServicePaymentSelection>();
    }

    // [V2G-DC-439] admits a SessionStopReq here.
    if (const auto stop = received.get_if<message_din::SessionStopRequest>()) {
        return process_session_stop(m_ctx, *stop);
    }

    logf_warning("Expected ServiceDiscoveryReq or SessionStopReq! But code type id: %d", received.get_type());
    respond_sequence_error(m_ctx, received);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
