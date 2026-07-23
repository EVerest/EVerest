// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/payment_service_selection.hpp>

#include <algorithm>

#include <iso15118/d2/state/authorization.hpp>
#include <iso15118/d2/state/payment_details.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/payment_service_selection.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

message_2::PaymentServiceSelectionResponse handle_request(const message_2::PaymentServiceSelectionRequest& req,
                                                          const dt::SessionId& session_id, uint16_t charge_service_id,
                                                          bool pnc_enabled, bool cert_service_offered) {
    message_2::PaymentServiceSelectionResponse res;
    res.header.session_id = session_id;

    // Accept ExternalPayment (EIM) always, and Contract (PnC) only when Plug-and-Charge is enabled;
    // otherwise reject [V2G2-465].
    const bool contract_allowed = pnc_enabled and req.selected_payment_option == dt::PaymentOption::Contract;
    if (req.selected_payment_option != dt::PaymentOption::ExternalPayment and not contract_allowed) {
        res.response_code = dt::ResponseCode::FAILED_PaymentSelectionInvalid;
        return res;
    }

    const auto& list = req.selected_service_list;

    // The charging service must be among the selected services [V2G2-804]. Checked BEFORE the
    // offered-service validation below: omitting the charge service must be reported as
    // FAILED_NoChargeServiceSelected even when the EV selected some other (unoffered) service in its
    // place -- the -4 ATS gives "no charge service" the higher precedence (TC PaymentServiceSelection_007;
    // e.g. selecting only ServiceID 2 on a non-PnC session must yield FAILED_NoChargeServiceSelected, not
    // FAILED_ServiceSelectionInvalid).
    const bool charge_service_selected = std::any_of(
        list.begin(), list.end(), [&](const dt::SelectedService& s) { return s.service_id == charge_service_id; });
    if (not charge_service_selected) {
        res.response_code = dt::ResponseCode::FAILED_NoChargeServiceSelected;
        return res;
    }

    // Every selected ServiceID must have been offered in ServiceDiscoveryRes [V2G2-433/467]: the charge
    // service (always) and the Certificate service (only when advertised over a PnC/TLS session). The
    // charge service is guaranteed present here (checked above), so this catches any extra unoffered
    // service (TC PaymentServiceSelection_006).
    for (const auto& s : list) {
        const bool offered = (s.service_id == charge_service_id) or
                             (cert_service_offered and s.service_id == dt::CERTIFICATE_SERVICE_ID);
        if (not offered) {
            res.response_code = dt::ResponseCode::FAILED_ServiceSelectionInvalid;
            return res;
        }
    }

    res.response_code = dt::ResponseCode::OK;
    return res;
}

void PaymentServiceSelection::enter() {
    m_ctx.log.enter_state("PaymentServiceSelection");
}

Result PaymentServiceSelection::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_2::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    const auto req = variant->get_if<message_2::PaymentServiceSelectionRequest>();
    if (req == nullptr) {
        m_ctx.log("expected PaymentServiceSelectionReq! But code type id: %d", variant->get_type());
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

    // Accept Contract (PnC) only over TLS [V2G2-634]; on a plain-TCP session Contract is rejected. The
    // Certificate service was offered under that same condition and only when cert installation is
    // supported, so its selection is validated under the matching flag.
    const bool allow_contract = m_ctx.session_config.pnc_enabled and m_ctx.session_config.tls_active;
    const bool cert_service_offered = allow_contract and m_ctx.session_config.cert_install_service;
    const auto res = handle_request(*req, m_ctx.get_session_id(), m_ctx.session_config.charge_service_id,
                                    allow_contract, cert_service_offered);
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
        return {};
    }

    // Record which certificate exchange(s) the EV selected (ISO 15118-2 Table 106): ParameterSetID 1 =
    // Installation, 2 = Update; a certificate SelectedService without a ParameterSetID permits either.
    // PaymentDetails gates the CertificateInstallation/Update relay on this [V2G2-432].
    for (const auto& s : req->selected_service_list) {
        if (s.service_id != dt::CERTIFICATE_SERVICE_ID) {
            continue;
        }
        if (not s.parameter_set_id.has_value()) {
            m_ctx.cert_install_selected = true;
            m_ctx.cert_update_selected = true;
        } else if (s.parameter_set_id.value() == 1) {
            m_ctx.cert_install_selected = true;
        } else if (s.parameter_set_id.value() == 2) {
            m_ctx.cert_update_selected = true;
        }
    }

    // Contract (PnC) selected: run PaymentDetails (contract chain validation, GenChallenge) before
    // Authorization. ExternalPayment (EIM) goes straight to Authorization.
    if (req->selected_payment_option == dt::PaymentOption::Contract) {
        m_ctx.contract_selected = true;
        return m_ctx.create_state<PaymentDetails>();
    }

    return m_ctx.create_state<Authorization>();
}

} // namespace iso15118::d2::state
