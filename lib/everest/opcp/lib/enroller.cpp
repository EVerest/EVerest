// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/opcp/enroller.hpp>

#include <algorithm>

#include <evse_security/evse_types.hpp>

namespace opcp {

namespace {

std::string install_result_name(evse_security::InstallCertificateResult result) {
    return evse_security::conversions::install_certificate_result_to_string(result);
}

std::string csr_status_name(evse_security::GetCertificateSignRequestStatus status) {
    switch (status) {
    case evse_security::GetCertificateSignRequestStatus::Accepted:
        return "Accepted";
    case evse_security::GetCertificateSignRequestStatus::InvalidRequestedType:
        return "InvalidRequestedType";
    case evse_security::GetCertificateSignRequestStatus::KeyGenError:
        return "KeyGenError";
    case evse_security::GetCertificateSignRequestStatus::GenerationError:
        return "GenerationError";
    }
    return "Unknown";
}

} // namespace

const char* enroll_step_name(EnrollStep step) {
    switch (step) {
    case EnrollStep::NotStarted:
        return "not started";
    case EnrollStep::CheckRoots:
        return "checking installed V2G roots";
    case EnrollStep::GenerateCsr:
        return "generating CSR";
    case EnrollStep::Enroll:
        return "EST enrollment";
    case EnrollStep::FetchCaCertificates:
        return "fetching CPO sub-CA certificates";
    case EnrollStep::BuildChain:
        return "building certificate chain";
    case EnrollStep::InstallLeaf:
        return "installing leaf certificate";
    case EnrollStep::Done:
        return "done";
    }
    return "unknown";
}

Enroller::Enroller(SecurityStore& store_, EstClient& est_, RcpClient& rcp_) : store(store_), est(est_), rcp(rcp_) {
}

EnrollResult Enroller::enroll_leaf(IsoVersion version, const CsrSubject& subject, const Auth& auth, bool reenroll) {
    EnrollResult result;
    const auto& iso = info(version);

    result.failed_step = EnrollStep::CheckRoots;
    if (!store.is_ca_certificate_installed(evse_security::CaCertificateType::V2G)) {
        result.error = "no V2G root certificate is installed; synchronise the root certificates first";
        return result;
    }

    result.failed_step = EnrollStep::GenerateCsr;
    const auto csr = store.generate_certificate_signing_request(iso.leaf_type, subject.country, subject.organization,
                                                                subject.common_name, subject.use_tpm);
    if (csr.status != evse_security::GetCertificateSignRequestStatus::Accepted || !csr.csr.has_value()) {
        result.error = "CSR generation failed: " + csr_status_name(csr.status);
        return result;
    }
    result.csr_pem = *csr.csr;

    auto fail_with_cleanup = [&](EnrollStep step, const std::string& error) {
        result.failed_step = step;
        result.error = error;
        store.certificate_signing_request_failed(result.csr_pem, iso.leaf_type);
        return result;
    };

    result.failed_step = EnrollStep::Enroll;
    const EstResult enrolled = reenroll ? est.simple_reenroll(version, result.csr_pem, auth)
                                        : est.simple_enroll(version, result.csr_pem, auth);
    result.http_status = enrolled.http_status;
    if (!enrolled.ok) {
        result.auth_rejected = enrolled.auth_rejected;
        return fail_with_cleanup(EnrollStep::Enroll, enrolled.url + ": " + enrolled.error);
    }

    // The enroll response carries only the leaf; the intermediates come from cacerts
    const EstResult ca_chain = est.cacerts(version, auth);
    if (!ca_chain.ok) {
        result.auth_rejected = ca_chain.auth_rejected;
        result.http_status = ca_chain.http_status;
        return fail_with_cleanup(EnrollStep::FetchCaCertificates, ca_chain.url + ": " + ca_chain.error);
    }

    std::string chain_error;
    std::optional<std::string> chain_top_issuer;
    const auto chain = build_leaf_chain(result.csr_pem, enrolled.certificates_pem, ca_chain.certificates_pem,
                                        chain_error, &chain_top_issuer);
    if (!chain.has_value()) {
        return fail_with_cleanup(EnrollStep::BuildChain, chain_error);
    }
    result.chain_pem = *chain;
    result.chain_length = count_pem_certificates(result.chain_pem);
    result.leaf =
        describe_certificate(enrolled.certificates_pem.empty() ? result.chain_pem : enrolled.certificates_pem.front());

    result.failed_step = EnrollStep::InstallLeaf;
    const auto install = store.update_leaf_certificate(result.chain_pem, iso.leaf_type);
    if (install != evse_security::InstallCertificateResult::Accepted) {
        std::string error = "the security store rejected the chain: " + install_result_name(install);
        if (install == evse_security::InstallCertificateResult::InvalidCertificateChain ||
            install == evse_security::InstallCertificateResult::NoRootCertificateInstalled) {
            error += " (the chain does not end at an installed V2G root - synchronise the root certificates)";
        }
        return fail_with_cleanup(EnrollStep::InstallLeaf, error);
    }

    result.failed_step = EnrollStep::Done;
    result.ok = true;
    return result;
}

RootSyncResult Enroller::sync_roots(const Auth& auth, const std::vector<RootType>& types) {
    RootSyncResult result;

    // One request without filter; the pool is small and this avoids N round trips
    const RcpResult roots = rcp.get_root_certificates(std::nullopt, auth);
    result.http_status = roots.http_status;
    if (!roots.ok) {
        result.auth_rejected = roots.auth_rejected;
        result.error = roots.url + ": " + roots.error;
        return result;
    }

    for (const auto& root : roots.roots) {
        if (std::find(types.begin(), types.end(), root.root_type) == types.end()) {
            continue;
        }
        const std::string label = std::string(root_type_name(root.root_type)) + " '" + root.common_name + "'";
        const auto bundle = ca_certificate_type_for(root.root_type);
        if (!bundle.has_value()) {
            ++result.skipped;
            result.messages.push_back(label + ": skipped (no CA bundle for this root type in the store)");
            continue;
        }
        const auto install = store.install_ca_certificate(root.pem, *bundle);
        if (install == evse_security::InstallCertificateResult::Accepted) {
            ++result.installed;
            result.messages.push_back(label + ": installed");
        } else {
            ++result.failed;
            result.messages.push_back(label + ": " + install_result_name(install));
        }
    }

    result.ok = result.failed == 0;
    if (!result.ok) {
        result.error = std::to_string(result.failed) + " root certificate(s) could not be installed";
    }
    if (!roots.error.empty()) {
        result.messages.push_back("note: " + roots.error);
    }
    return result;
}

} // namespace opcp
