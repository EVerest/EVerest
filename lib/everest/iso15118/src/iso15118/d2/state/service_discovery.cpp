// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/service_discovery.hpp>

#include <iso15118/d2/state/service_selection.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/service_discovery.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

message_2::ServiceDiscoveryResponse handle_request(
    [[maybe_unused]] const message_2::ServiceDiscoveryRequest& req, const dt::SessionId& session_id,
    uint16_t charge_service_id, const everest::lib::util::fixed_vector<dt::EnergyTransferMode, 6>& supported_modes,
    bool offer_eim, bool offer_contract, bool cert_service_offered,
    const std::optional<dt::PaymentOption>& resumed_payment_option, const dt::ServiceList& offered_vas_services) {
    message_2::ServiceDiscoveryResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;

    if (resumed_payment_option.has_value()) {
        // Resumed session: offer only the payment option selected in the paused session [V2G2-741].
        res.payment_option_list.push_back(resumed_payment_option.value());
    } else {
        // Exactly the configured options (EvseV2G/Josev parity); the caller gates them and guarantees at
        // least one is offered.
        if (offer_eim) {
            res.payment_option_list.push_back(dt::PaymentOption::ExternalPayment);
        }
        if (offer_contract) {
            res.payment_option_list.push_back(dt::PaymentOption::Contract);
        }
    }

    // [V2G2-410/416/417]: without the advertisement the relay still works but is undiscoverable. Gated
    // separately from the Contract payment option, so a Contract-auth-only SECC does not advertise it.
    if (cert_service_offered) {
        auto& service_list = res.service_list.emplace();
        dt::Service cert_service;
        cert_service.service_id = dt::CERTIFICATE_SERVICE_ID;
        cert_service.service_name = "Certificate";
        cert_service.service_category = dt::ServiceCategory::ContractCertificate;
        cert_service.free_service = true;
        service_list.push_back(cert_service);
    }

    // Already filtered and sized by make_d2_config so they fit next to the Certificate service.
    for (const auto& vas : offered_vas_services) {
        if (not res.service_list.has_value()) {
            res.service_list.emplace();
        }
        if (res.service_list->try_emplace_back(vas) == nullptr) {
            logf_warning("ServiceList full; dropping VAS ServiceID %u", vas.service_id);
            break;
        }
    }

    auto& charge_service = res.charge_service;
    charge_service.service_id = charge_service_id;
    charge_service.service_category = dt::ServiceCategory::EVCharging;
    charge_service.free_service = true;
    charge_service.supported_energy_transfer_mode = supported_modes;

    return res;
}

void ServiceDiscovery::enter() {
    logf_debug("Enter state: ServiceDiscovery");
}

Result ServiceDiscovery::on_request(const message_2::Variant& received) {

    const auto req = received.get_if<message_2::ServiceDiscoveryRequest>();
    if (req == nullptr) {
        logf_warning("Expected ServiceDiscoveryReq! But code type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }

    // PnC (Contract) is offered only over TLS [V2G2-632], and the Certificate service under that same
    // condition. Where the gating leaves no option at all -- a Contract-only configuration on plain TCP,
    // or an empty configuration -- fall back to ExternalPayment like EvseV2G.
    const bool offer_contract = m_ctx.session_config.pnc_enabled and m_ctx.session_config.tls_active;
    const bool offer_eim = m_ctx.session_config.eim_enabled or not offer_contract;
    const bool cert_service_offered = offer_contract and m_ctx.session_config.cert_install_service;

    // Resumed session: only the previously selected option [V2G2-741] -- unless it no longer passes the
    // gating (a PnC pause resumed over plain TCP), where offering Contract would break [V2G2-632].
    std::optional<dt::PaymentOption> resumed_payment_option;
    if (m_ctx.session().session_resumed and m_ctx.pause_ctx.has_value()) {
        const auto stored_option = m_ctx.pause_ctx->selected_payment_option;
        if (stored_option != dt::PaymentOption::Contract or offer_contract) {
            resumed_payment_option = stored_option;
        } else {
            logf_warning("Resumed a PnC-paused session without TLS; offering the normal payment options");
        }
    }

    const auto res =
        handle_request(*req, m_ctx.get_session_id(), m_ctx.session_config.charge_service_id,
                       m_ctx.session_config.supported_energy_transfer_modes, offer_eim, offer_contract,
                       cert_service_offered, resumed_payment_option, m_ctx.session_config.offered_vas_services);
    m_ctx.respond(res);

    return m_ctx.create_state<ServiceSelection>();
}

} // namespace iso15118::d2::state
