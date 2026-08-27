// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "direct_security_store.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace opcp::tool {

namespace {
std::filesystem::path resolve(const std::string& base, const std::string& entry) {
    const std::filesystem::path path(entry);
    return path.is_absolute() ? path : std::filesystem::path(base) / path;
}
} // namespace

evse_security::FilePaths StoreLayout::to_file_paths() const {
    evse_security::FilePaths paths;
    paths.csms_ca_bundle = resolve(certs_dir, csms_ca_bundle);
    paths.mf_ca_bundle = resolve(certs_dir, mf_ca_bundle);
    paths.mo_ca_bundle = resolve(certs_dir, mo_ca_bundle);
    paths.v2g_ca_bundle = resolve(certs_dir, v2g_ca_bundle);
    paths.directories.csms_leaf_cert_directory = resolve(certs_dir, csms_leaf_cert_directory);
    paths.directories.csms_leaf_key_directory = resolve(certs_dir, csms_leaf_key_directory);
    paths.directories.secc_leaf_cert_directory = resolve(certs_dir, secc_leaf_cert_directory);
    paths.directories.secc_leaf_key_directory = resolve(certs_dir, secc_leaf_key_directory);
    return paths;
}

DirectSecurityStore::DirectSecurityStore(const StoreLayout& layout) {
    const auto paths = layout.to_file_paths();
    // libevse-security expects the directories and bundle parents to exist
    for (const auto& dir : {paths.directories.secc_leaf_cert_directory, paths.directories.secc_leaf_key_directory,
                            paths.directories.csms_leaf_cert_directory, paths.directories.csms_leaf_key_directory}) {
        std::filesystem::create_directories(dir);
    }
    for (const auto& bundle : {paths.csms_ca_bundle, paths.mf_ca_bundle, paths.mo_ca_bundle, paths.v2g_ca_bundle}) {
        if (bundle.has_parent_path()) {
            std::filesystem::create_directories(bundle.parent_path());
        }
    }
    std::optional<std::string> password;
    if (!layout.private_key_password.empty()) {
        password = layout.private_key_password;
    }
    security = std::make_unique<evse_security::EvseSecurity>(paths, password);
}

evse_security::GetCertificateSignRequestResult
DirectSecurityStore::generate_certificate_signing_request(evse_security::LeafCertificateType type,
                                                          const std::string& country, const std::string& organization,
                                                          const std::string& common_name, bool use_tpm) {
    return security->generate_certificate_signing_request(type, country, organization, common_name, use_tpm);
}

void DirectSecurityStore::certificate_signing_request_failed(const std::string& csr_pem,
                                                             evse_security::LeafCertificateType type) {
    security->certificate_signing_request_failed(csr_pem, type);
}

evse_security::InstallCertificateResult
DirectSecurityStore::update_leaf_certificate(const std::string& certificate_chain_pem,
                                             evse_security::LeafCertificateType type) {
    return security->update_leaf_certificate(certificate_chain_pem, type);
}

evse_security::InstallCertificateResult
DirectSecurityStore::install_ca_certificate(const std::string& certificate_pem, evse_security::CaCertificateType type) {
    return security->install_ca_certificate(certificate_pem, type);
}

bool DirectSecurityStore::is_ca_certificate_installed(evse_security::CaCertificateType type) {
    return security->is_ca_certificate_installed(type);
}

std::optional<LeafInfo> DirectSecurityStore::get_leaf_certificate_info(evse_security::LeafCertificateType type) {
    const auto result = security->get_leaf_certificate_info(type, evse_security::EncodingFormat::PEM, false);
    if (result.status != evse_security::GetCertificateInfoStatus::Accepted || !result.info.has_value()) {
        return std::nullopt;
    }
    LeafInfo info;
    if (result.info->certificate.has_value()) {
        info.certificate_path = result.info->certificate->string();
    } else if (result.info->certificate_single.has_value()) {
        info.certificate_path = result.info->certificate_single->string();
    } else {
        return std::nullopt;
    }
    info.key_path = result.info->key.string();
    info.key_password = result.info->password;
    info.public_key_algorithm = result.info->public_key_algorithm;
    return info;
}

int DirectSecurityStore::get_leaf_expiry_days_count(evse_security::LeafCertificateType type) {
    return security->get_leaf_expiry_days_count(type);
}

std::string DirectSecurityStore::get_ca_bundle_pem(evse_security::CaCertificateType type) {
    const std::string file = security->get_verify_file(type);
    std::ifstream stream(file);
    if (!stream) {
        return {};
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace opcp::tool
