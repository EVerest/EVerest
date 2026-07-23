// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/certificate_installation.hpp>

#include <iso15118/d2/ev/state/payment_details.hpp>
#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/crypto.hpp>
#include <iso15118/detail/d2/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/message_2/certificate_installation.hpp>

namespace iso15118::d2::ev::state {

void CertificateInstallation::enter() {
    m_ctx.log.enter_state("CertificateInstallation");
}

d2::ev::Result CertificateInstallation::feed(Event ev) {
    const auto& pnc = m_ctx.session_config.pnc;

    if (ev == Event::SEND_REQUEST) {
        message_2::CertificateInstallationRequest req;
        m_ctx.setup_header(req.header);
        req.id = "id1";
        req.oem_provisioning_cert = pnc.oem_prov_cert_der;
        req.root_certificate_ids = pnc.root_certificate_ids;

        const crypto::PrivateKey oem_key{pnc.oem_prov_key_pem, pnc.oem_prov_key_password};
        const auto signed_exi = crypto::serialize_signed(req, oem_key);
        if (signed_exi.empty()) {
            m_ctx.log("CertificateInstallation: failed to build/sign CertificateInstallationReq, terminating");
            m_ctx.session_stopped = true;
            return {};
        }
        m_ctx.send_raw(signed_exi, message_2::Type::CertificateInstallationReq);
        m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_CERTIFICATE_INSTALL_MS);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("CertificateInstallation message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();
    const auto res = variant->get_if<message_2::CertificateInstallationResponse>();
    if (res == nullptr) {
        m_ctx.log("expected CertificateInstallationRes! But code type id: %d", variant->get_type());
        m_ctx.session_stopped = true;
        return {};
    }
    if (res->response_code >= dt::ResponseCode::FAILED) {
        m_ctx.log("CertificateInstallation failed (response code %d), terminating session", res->response_code);
        m_ctx.session_stopped = true;
        return {};
    }

    // Verify the CPS signature over the four signed elements against the trusted V2G root.
    if (not crypto::verify_certificate_installation_res(variant->get_exi_payload(), pnc.v2g_root_path)) {
        m_ctx.log("CertificateInstallation: response signature verification failed, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    // Decrypt the contract private key (ECDH with the OEM provisioning key, ConcatKDF, AES-128-CBC).
    const crypto::PrivateKey oem_key{pnc.oem_prov_key_pem, pnc.oem_prov_key_password};
    const auto scalar = crypto::decrypt_contract_private_key(res->encrypted_private_key, res->dh_public_key, oem_key);
    const auto contract_key_pem = crypto::contract_scalar_to_pem(scalar);
    if (contract_key_pem.empty()) {
        m_ctx.log("CertificateInstallation: failed to decrypt the contract private key, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    // Install the contract certificate for the following PaymentDetails/Authorization exchange.
    m_ctx.pnc.contract_cert_der = res->contract_chain.certificate;
    m_ctx.pnc.contract_sub_certs_der = res->contract_chain.sub_certificates;
    m_ctx.pnc.contract_key_pem = contract_key_pem;
    m_ctx.pnc.contract_key_password = std::nullopt;
    m_ctx.pnc.emaid = res->emaid;

    // Report to the module so it can persist the installed contract (e.g. via EvseSecurity).
    const auto chain_pem =
        crypto::der_chain_to_pem(res->contract_chain.certificate, res->contract_chain.sub_certificates);
    m_ctx.feedback.pnc_contract_installed(chain_pem, contract_key_pem, res->emaid);

    if (auto stop = stop_if_pending(m_ctx)) {
        return stop;
    }
    return m_ctx.create_state<PaymentDetails>();
}

} // namespace iso15118::d2::ev::state
