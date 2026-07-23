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
               const everest::lib::util::fixed_vector<dt::EnergyTransferMode, 6>& supported_modes, bool pnc_enabled,
               bool cert_service_offered) {
    message_2::ServiceDiscoveryResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;

    // Always advertise ExternalPayment (EIM); advertise Contract too when Plug-and-Charge is enabled.
    res.payment_option_list.push_back(dt::PaymentOption::ExternalPayment);
    if (pnc_enabled) {
        res.payment_option_list.push_back(dt::PaymentOption::Contract);
    }

    // Advertise the Certificate service (ServiceID 2, Table 105) so a PnC EVCC can discover that this SECC
    // supports certificate installation/update [V2G2-410/416/417]; without it the relay still works but is
    // undiscoverable. Gated separately from the Contract payment option: a Contract-auth-only SECC does not
    // advertise it (cert_service_offered folds in the enable_certificate_install_service capability).
    if (cert_service_offered) {
        auto& service_list = res.service_list.emplace();
        dt::Service cert_service;
        cert_service.service_id = dt::CERTIFICATE_SERVICE_ID;
        cert_service.service_name = "Certificate";
        cert_service.service_category = dt::ServiceCategory::ContractCertificate;
        cert_service.free_service = true;
        service_list.push_back(cert_service);
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

    // PnC (Contract) is offered only over TLS [V2G2-632]; on a plain-TCP session only ExternalPayment. The
    // Certificate service is offered under that same condition and only when cert installation is supported.
    const bool offer_contract = m_ctx.session_config.pnc_enabled and m_ctx.session_config.tls_active;
    const bool cert_service_offered = offer_contract and m_ctx.session_config.cert_install_service;
    const auto res =
        handle_request(*req, m_ctx.get_session_id(), m_ctx.session_config.charge_service_id,
                       m_ctx.session_config.supported_energy_transfer_modes, offer_contract, cert_service_offered);
    m_ctx.respond(res);

    return m_ctx.create_state<ServiceDetail>();
}

} // namespace iso15118::d2::state
