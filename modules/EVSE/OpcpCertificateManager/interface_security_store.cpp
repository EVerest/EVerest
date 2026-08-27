// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "interface_security_store.hpp"

#include <fstream>
#include <sstream>

#include <everest/conversions/evse_security/conversions.hpp>

namespace module {

namespace {

evse_security::GetCertificateSignRequestStatus from_everest(types::evse_security::GetCertificateSignRequestStatus s) {
    using In = types::evse_security::GetCertificateSignRequestStatus;
    using Out = evse_security::GetCertificateSignRequestStatus;
    switch (s) {
    case In::Accepted:
        return Out::Accepted;
    case In::InvalidRequestedType:
        return Out::InvalidRequestedType;
    case In::KeyGenError:
        return Out::KeyGenError;
    case In::GenerationError:
        return Out::GenerationError;
    }
    return Out::GenerationError;
}

} // namespace

InterfaceSecurityStore::InterfaceSecurityStore(evse_securityIntf& security_) : security(security_) {
}

evse_security::GetCertificateSignRequestResult InterfaceSecurityStore::generate_certificate_signing_request(
    evse_security::LeafCertificateType type, const std::string& country, const std::string& organization,
    const std::string& common_name, bool use_tpm) {
    const auto response = security.call_generate_certificate_signing_request(conversions::to_everest(type), country,
                                                                             organization, common_name, use_tpm);
    evse_security::GetCertificateSignRequestResult result;
    result.status = from_everest(response.status);
    result.csr = response.csr;
    return result;
}

void InterfaceSecurityStore::certificate_signing_request_failed(const std::string&,
                                                                evse_security::LeafCertificateType) {
    // Not exposed by the interface; libevse-security garbage collects unanswered CSR keys after its CSR expiry
}

evse_security::InstallCertificateResult
InterfaceSecurityStore::update_leaf_certificate(const std::string& certificate_chain_pem,
                                                evse_security::LeafCertificateType type) {
    return conversions::from_everest(
        security.call_update_leaf_certificate(certificate_chain_pem, conversions::to_everest(type)));
}

evse_security::InstallCertificateResult
InterfaceSecurityStore::install_ca_certificate(const std::string& certificate_pem,
                                               evse_security::CaCertificateType type) {
    return conversions::from_everest(
        security.call_install_ca_certificate(certificate_pem, conversions::to_everest(type)));
}

bool InterfaceSecurityStore::is_ca_certificate_installed(evse_security::CaCertificateType type) {
    return security.call_is_ca_certificate_installed(conversions::to_everest(type));
}

std::optional<opcp::LeafInfo>
InterfaceSecurityStore::get_leaf_certificate_info(evse_security::LeafCertificateType type) {
    const auto response = security.call_get_leaf_certificate_info(conversions::to_everest(type),
                                                                  types::evse_security::EncodingFormat::PEM, false);
    if (response.status != types::evse_security::GetCertificateInfoStatus::Accepted || !response.info.has_value()) {
        return std::nullopt;
    }
    opcp::LeafInfo info;
    if (response.info->certificate.has_value()) {
        info.certificate_path = *response.info->certificate;
    } else if (response.info->certificate_single.has_value()) {
        info.certificate_path = *response.info->certificate_single;
    } else {
        return std::nullopt;
    }
    info.key_path = response.info->key;
    info.key_password = response.info->password;
    info.public_key_algorithm = response.info->public_key_algorithm.value_or("");
    return info;
}

int InterfaceSecurityStore::get_leaf_expiry_days_count(evse_security::LeafCertificateType type) {
    return security.call_get_leaf_expiry_days_count(conversions::to_everest(type));
}

std::string InterfaceSecurityStore::get_ca_bundle_pem(evse_security::CaCertificateType type) {
    const std::string file = security.call_get_verify_file(conversions::to_everest(type));
    std::ifstream stream(file);
    if (!stream) {
        return {};
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace module
