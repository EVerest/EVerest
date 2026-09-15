// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/certificate_installation.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/payment_details.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/crypto.hpp>
#include <iso15118/message_2/certificate_installation.hpp>

namespace iso15118::ev::d2::state {

void CertificateInstallation::enter() {
    logf_debug("Enter state: CertificateInstallation (ISO 15118-2)");
    const auto& pnc = m_ctx.params().pnc;

    message_2::CertificateInstallationRequest req;
    req.header.session_id = m_ctx.get_session_id();
    req.id = "id1";
    req.oem_provisioning_cert = pnc.oem_prov_cert_der;
    req.root_certificate_ids = pnc.root_certificate_ids;

    const crypto::PrivateKey oem_key{pnc.oem_prov_key_pem, pnc.oem_prov_key_password};
    auto signed_exi = crypto::serialize_signed(req, oem_key);
    if (signed_exi.empty()) {
        logf_error("CertificateInstallation: failed to sign CertificateInstallationReq; stopping the session");
        m_ctx.stop_session();
        return;
    }
    m_ctx.send_raw(std::move(signed_exi), message_2::Type::CertificateInstallationReq);
}

Result CertificateInstallation::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_2::CertificateInstallationResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    const auto& pnc = m_ctx.params().pnc;

    // CPS signature over the four signed elements, against the trusted V2G root.
    if (not crypto::verify_certificate_installation_res(variant->get_exi_payload(), pnc.v2g_root_path)) {
        logf_error("CertificateInstallation: response signature verification failed; stopping the session");
        m_ctx.stop_session();
        return Result::stopping();
    }

    const crypto::PrivateKey oem_key{pnc.oem_prov_key_pem, pnc.oem_prov_key_password};
    const auto scalar = crypto::decrypt_contract_private_key(res->encrypted_private_key, res->dh_public_key, oem_key);
    const auto contract_key_pem = crypto::contract_scalar_to_pem(scalar);
    if (contract_key_pem.empty()) {
        logf_error("CertificateInstallation: failed to decrypt the contract private key; stopping the session");
        m_ctx.stop_session();
        return Result::stopping();
    }

    m_ctx.pnc.contract_cert_der = res->contract_chain.certificate;
    m_ctx.pnc.contract_sub_certs_der = res->contract_chain.sub_certificates;
    m_ctx.pnc.contract_key_pem = contract_key_pem;
    m_ctx.pnc.contract_key_password = std::nullopt;
    m_ctx.pnc.emaid = res->emaid;

    // The module persists the installed contract (EvseSecurity).
    m_ctx.feedback.pnc_contract_installed(
        crypto::der_chain_to_pem(res->contract_chain.certificate, res->contract_chain.sub_certificates),
        contract_key_pem, res->emaid);

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }
    return m_ctx.create_state<PaymentDetails>();
}

} // namespace iso15118::ev::d2::state
