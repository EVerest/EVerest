// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/service_selection.hpp>

#include <algorithm>

#include <iso15118/d2/state/authorization.hpp>
#include <iso15118/d2/state/identification.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/payment_service_selection.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/vas.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

message_2::PaymentServiceSelectionResponse
handle_request(const message_2::PaymentServiceSelectionRequest& req, const dt::SessionId& session_id,
               uint16_t charge_service_id, bool eim_allowed, bool contract_allowed, bool cert_service_offered,
               const std::optional<dt::PaymentOption>& resumed_payment_option,
               const dt::ServiceList& offered_vas_services) {
    message_2::PaymentServiceSelectionResponse res;
    res.header.session_id = session_id;

    if (resumed_payment_option.has_value()) {
        // On a resumed session only the previously selected option was offered [V2G2-741], so a matching
        // selection is accepted regardless of the current flags.
        if (req.selected_payment_option != resumed_payment_option.value()) {
            res.response_code = dt::ResponseCode::FAILED_PaymentSelectionInvalid;
            return res;
        }
    } else {
        // Only the options offered in ServiceDiscoveryRes are accepted [V2G2-465]. EIM is not implicitly
        // allowed: a Contract-only SECC rejects ExternalPayment (the -4 ATS models single-option SECCs).
        const bool selection_allowed =
            (req.selected_payment_option == dt::PaymentOption::ExternalPayment and eim_allowed) or
            (req.selected_payment_option == dt::PaymentOption::Contract and contract_allowed);
        if (not selection_allowed) {
            res.response_code = dt::ResponseCode::FAILED_PaymentSelectionInvalid;
            return res;
        }
    }

    const auto& list = req.selected_service_list;

    // [V2G2-804]. Checked BEFORE the offered-service validation below: the -4 ATS gives "no charge
    // service" the higher precedence (TC PaymentServiceSelection_007), so omitting it must be reported
    // as FAILED_NoChargeServiceSelected even when the EV also selected an unoffered service.
    const bool charge_service_selected = std::any_of(
        list.begin(), list.end(), [&](const dt::SelectedService& s) { return s.service_id == charge_service_id; });
    if (not charge_service_selected) {
        res.response_code = dt::ResponseCode::FAILED_NoChargeServiceSelected;
        return res;
    }

    // [V2G2-433/467]. The charge service is guaranteed present here, so this catches any extra
    // unoffered service (TC PaymentServiceSelection_006).
    for (const auto& s : list) {
        const bool offered = (s.service_id == charge_service_id) or
                             (cert_service_offered and s.service_id == dt::CERTIFICATE_SERVICE_ID) or
                             is_offered_vas(offered_vas_services, s.service_id);
        if (not offered) {
            res.response_code = dt::ResponseCode::FAILED_ServiceSelectionInvalid;
            return res;
        }
    }

    res.response_code = dt::ResponseCode::OK;
    return res;
}

Result ServiceSelection::process_payment_selection(const message_2::PaymentServiceSelectionRequest& req) {
    // Contract (PnC) only over TLS [V2G2-634]. The Certificate service was offered under that same
    // condition, so its selection is validated under the matching flag.
    const bool allow_contract = m_ctx.session_config.pnc_enabled and m_ctx.session_config.tls_active;
    const bool allow_eim = m_ctx.session_config.eim_enabled or not allow_contract;
    const bool cert_service_offered = allow_contract and m_ctx.session_config.cert_install_service;

    // Mirrors the ServiceDiscovery resume restriction [V2G2-741].
    std::optional<dt::PaymentOption> resumed_payment_option;
    if (m_ctx.session().session_resumed and m_ctx.pause_ctx.has_value()) {
        const auto stored_option = m_ctx.pause_ctx->selected_payment_option;
        if (stored_option != dt::PaymentOption::Contract or allow_contract) {
            resumed_payment_option = stored_option;
        }
    }

    const auto res =
        handle_request(req, m_ctx.get_session_id(), m_ctx.session_config.charge_service_id, allow_eim, allow_contract,
                       cert_service_offered, resumed_payment_option, m_ctx.session_config.offered_vas_services);
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
        return {};
    }

    // Only for a selection that was offered and accepted, never for a rejected one (EvseV2G parity).
    m_ctx.feedback.selected_payment_option(req.selected_payment_option);

    // Only once the whole selection has been validated.
    const auto selected_vas =
        selected_vas_services(req.selected_service_list, m_ctx.session_config.offered_vas_services);
    if (not selected_vas.empty()) {
        m_ctx.feedback.selected_vas_services(selected_vas);
    }

    // Table 106: ParameterSetID 1 = Installation, 2 = Update; a certificate SelectedService without one
    // permits either. PaymentDetails gates the relay on this [V2G2-432].
    bool cert_install_selected = false;
    bool cert_update_selected = false;
    for (const auto& s : req.selected_service_list) {
        if (s.service_id != dt::CERTIFICATE_SERVICE_ID) {
            continue;
        }
        if (not s.parameter_set_id.has_value()) {
            cert_install_selected = true;
            cert_update_selected = true;
        } else if (s.parameter_set_id.value() == 1) {
            cert_install_selected = true;
        } else if (s.parameter_set_id.value() == 2) {
            cert_update_selected = true;
        }
    }
    m_ctx.set_certificate_services(cert_install_selected, cert_update_selected);

    // ExternalPayment (EIM) goes straight to Authorization.
    if (req.selected_payment_option == dt::PaymentOption::Contract) {
        m_ctx.set_contract_selected();
        return m_ctx.create_state<Identification>();
    }

    return m_ctx.create_state<Authorization>();
}

} // namespace iso15118::d2::state
