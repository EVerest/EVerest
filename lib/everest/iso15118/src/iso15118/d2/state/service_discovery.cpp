// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/service_discovery.hpp>

#include <iso15118/d2/state/service_detail.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/service_discovery.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

message_2::ServiceDiscoveryResponse
handle_request([[maybe_unused]] const message_2::ServiceDiscoveryRequest& req, const dt::SessionId& session_id,
               uint16_t charge_service_id,
               const everest::lib::util::fixed_vector<dt::EnergyTransferMode, 6>& supported_modes, bool pnc_enabled) {
    message_2::ServiceDiscoveryResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;

    // Always advertise ExternalPayment (EIM); advertise Contract too when Plug-and-Charge is enabled.
    res.payment_option_list.push_back(dt::PaymentOption::ExternalPayment);
    if (pnc_enabled) {
        res.payment_option_list.push_back(dt::PaymentOption::Contract);
    }

    auto& charge_service = res.charge_service;
    charge_service.service_id = charge_service_id;
    charge_service.service_category = dt::ServiceCategory::EVCharging;
    charge_service.free_service = true;
    charge_service.supported_energy_transfer_mode = supported_modes;

    return res;
}

void ServiceDiscovery::enter() {
    m_ctx.log.enter_state("ServiceDiscovery");
}

Result ServiceDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_2::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    const auto req = variant->get_if<message_2::ServiceDiscoveryRequest>();
    if (req == nullptr) {
        m_ctx.log("expected ServiceDiscoveryReq! But code type id: %d", variant->get_type());
        // [V2G2-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
        respond_sequence_error(m_ctx, *variant);
        m_ctx.session_stopped = true;
        return {};
    }

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with the
    // received-type response carrying FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    const auto res = handle_request(*req, m_ctx.get_session_id(), m_ctx.session_config.charge_service_id,
                                    m_ctx.session_config.supported_energy_transfer_modes,
                                    m_ctx.session_config.pnc_enabled);
    m_ctx.respond(res);

    return m_ctx.create_state<ServiceDetail>();
}

} // namespace iso15118::d2::state
