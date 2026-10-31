// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/certificate_installation.hpp>

#include <fstream>
#include <sstream>

#include <iso15118/detail/d2/crypto.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/x509_helper.hpp>
#include <iso15118/ev/d2/state/payment_details.hpp>
#include <iso15118/ev/detail/crypto.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/certificate_installation.hpp>

namespace iso15118::ev::d2::state {

namespace certificate_installation {

message_2::CertificateInstallationRequest create_request(const dt::SessionId& session_id,
                                                         const std::vector<uint8_t>& oem_prov_cert_der,
                                                         const message_2::RootCertificateId& root_id) {
    message_2::CertificateInstallationRequest req;
    req.header.session_id = session_id;
    req.id = "id1";
    req.oem_provisioning_cert = oem_prov_cert_der;
    req.root_certificate_ids = {root_id};
    return req;
}

} // namespace certificate_installation

namespace {

Result stop(Context& ctx) {
    ctx.stop_session();
    return Result::stopping();
}

} // namespace

Result to_certificate_installation(Context& ctx) {
    const auto& pnc = ctx.params().pnc;
    const auto& root_path = ctx.params().v2g_root_cert_path;

    std::ifstream root_file(root_path, std::ios::binary);
    std::ostringstream root_pem;
    root_pem << root_file.rdbuf();
    if (not root_file.is_open() or root_file.bad() or root_pem.fail()) {
        logf_error("CertificateInstallation: cannot read V2G root at %s; stopping the session", root_path.c_str());
        return stop(ctx);
    }
    const auto root_chain = iso15118::d2::crypto::pem_chain_to_der(root_pem.str());
    if (root_chain.empty()) {
        logf_error("CertificateInstallation: no PEM certificate in V2G root at %s; stopping the session",
                   root_path.c_str());
        return stop(ctx);
    }
    const auto root_id = iso15118::d2::crypto::root_cert_id_from_der(root_chain.front());
    if (root_id.issuer_name.empty()) {
        logf_error("CertificateInstallation: unusable V2G root certificate ID at %s; stopping the session",
                   root_path.c_str());
        return stop(ctx);
    }

    const auto req = certificate_installation::create_request(ctx.get_session_id(), pnc.oem_prov_cert_der, root_id);
    const iso15118::d2::crypto::PrivateKey oem_key{pnc.oem_prov_key_pem, pnc.oem_prov_key_password};
    auto signed_exi = iso15118::d2::crypto::serialize_signed(req, oem_key);
    if (signed_exi.empty()) {
        logf_error("CertificateInstallation: failed to sign CertificateInstallationReq; stopping the session");
        return stop(ctx);
    }
    ctx.send_raw(std::move(signed_exi), message_2::Type::CertificateInstallationReq);
    return ctx.create_state<CertificateInstallation>();
}

void CertificateInstallation::enter() {
    logf_debug("Enter state: CertificateInstallation (ISO 15118-2)");
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
    if (not iso15118::d2::crypto::verify_certificate_installation_res(variant->get_exi_payload(),
                                                                      m_ctx.params().v2g_root_cert_path)) {
        logf_error("CertificateInstallation: response signature verification failed; stopping the session");
        m_ctx.stop_session();
        return Result::stopping();
    }

    // [V2G2-896]: the signer must be a certificate provisioning service.
    const auto sa_leaf = iso15118::x509::der_to_x509(res->sa_provisioning_chain.certificate);
    if (not iso15118::ev::crypto::has_cps_domain_component(sa_leaf.get())) {
        logf_error("CertificateInstallation: [V2G2-896] the response signer carries no DomainComponent=CPS; stopping "
                   "the session");
        m_ctx.stop_session();
        return Result::stopping();
    }

    const iso15118::d2::crypto::PrivateKey oem_key{pnc.oem_prov_key_pem, pnc.oem_prov_key_password};
    auto scalar =
        iso15118::d2::crypto::decrypt_contract_private_key(res->encrypted_private_key, res->dh_public_key, oem_key);
    const auto contract_key_pem = iso15118::d2::crypto::contract_scalar_to_pem(scalar);
    // The PEM is what the session keeps; the raw scalar has no further use.
    iso15118::d2::crypto::wipe(scalar);
    if (contract_key_pem.empty()) {
        logf_error("CertificateInstallation: failed to decrypt the contract private key; stopping the session");
        m_ctx.stop_session();
        return Result::stopping();
    }

    m_ctx.pnc.contract = ContractMaterial{res->contract_chain.certificate, res->contract_chain.sub_certificates,
                                          contract_key_pem, std::nullopt, res->emaid};

    // The module may persist the installed contract; this session keeps the in-memory one.
    m_ctx.feedback.pnc_contract_installed(
        iso15118::d2::crypto::der_chain_to_pem(res->contract_chain.certificate, res->contract_chain.sub_certificates),
        contract_key_pem, res->emaid);

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }
    return to_payment_details(m_ctx);
}

} // namespace iso15118::ev::d2::state
