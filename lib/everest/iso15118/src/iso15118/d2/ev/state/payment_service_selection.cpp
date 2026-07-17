// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/payment_service_selection.hpp>

#include <iso15118/d2/ev/state/authorization.hpp>
#include <iso15118/d2/ev/state/certificate_installation.hpp>
#include <iso15118/d2/ev/state/payment_details.hpp>
#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/ev/state/payment_service_selection.hpp>
#include <iso15118/detail/d2/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::ev::state {

namespace payment_service_selection {

message_2::PaymentServiceSelectionRequest create_request(uint16_t charge_service_id, dt::PaymentOption payment_option,
                                                         bool add_certificate_service) {
    message_2::PaymentServiceSelectionRequest req;
    req.selected_payment_option = payment_option;
    req.selected_service_list.push_back(dt::SelectedService{charge_service_id, std::nullopt});
    if (add_certificate_service) {
        req.selected_service_list.push_back(dt::SelectedService{dt::CERTIFICATE_SERVICE_ID, std::nullopt});
    }
    return req;
}

Result handle_response(const message_2::PaymentServiceSelectionResponse& res) {
    return {res.response_code < dt::ResponseCode::FAILED};
}

} // namespace payment_service_selection

using namespace payment_service_selection;

void PaymentServiceSelection::enter() {
    m_ctx.log.enter_state("PaymentServiceSelection");
}

d2::ev::Result PaymentServiceSelection::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        // Contract (Plug-and-Charge) is used when the EV prefers it and the SECC offered it; otherwise EIM.
        // enforce_contract selects Contract regardless of the SECC offer (negative-testing knob).
        const bool use_contract = m_ctx.session_config.pnc.enforce_contract or
                                  (m_ctx.session_config.pnc.prefer_contract and m_ctx.evse_info.contract_offered);
        const bool install_cert = use_contract and m_ctx.session_config.pnc.needs_cert_install() and
                                  m_ctx.evse_info.certificate_service_offered;
        const auto option = use_contract ? dt::PaymentOption::Contract : dt::PaymentOption::ExternalPayment;
        m_ctx.pnc.contract_selected = use_contract;

        auto req = create_request(m_ctx.evse_info.selected_charge_service_id, option, install_cert);
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
        m_ctx.log("PaymentServiceSelection message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_2::PaymentServiceSelectionResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("PaymentServiceSelection failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }

        // Plug-and-Charge routing: run CertificateInstallation first when a contract cert must be
        // obtained, else present the (pre-installed) contract cert in PaymentDetails; EIM goes straight to
        // Authorization.
        if (m_ctx.pnc.contract_selected) {
            const bool install_cert =
                m_ctx.session_config.pnc.needs_cert_install() and m_ctx.evse_info.certificate_service_offered;
            if (install_cert) {
                return m_ctx.create_state<CertificateInstallation>();
            }
            // Present the pre-installed contract certificate: seed the runtime PnC state from config so
            // PaymentDetails/Authorization use the configured contract chain + key.
            const auto& cfg = m_ctx.session_config.pnc;
            m_ctx.pnc.contract_cert_der = cfg.contract_cert_der;
            m_ctx.pnc.contract_sub_certs_der = cfg.contract_sub_certs_der;
            m_ctx.pnc.contract_key_pem = cfg.contract_key_pem;
            m_ctx.pnc.contract_key_password = cfg.contract_key_password;
            m_ctx.pnc.emaid = cfg.contract_emaid;
            return m_ctx.create_state<PaymentDetails>();
        }
        return m_ctx.create_state<Authorization>();
    }

    m_ctx.log("expected PaymentServiceSelectionRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::ev::state
