// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>
#include <string>

#include <everest/opcp/security_store.hpp>
#include <evse_security/evse_security.hpp>

namespace opcp::tool {

/// Certificate store layout, mirroring the defaults of the EvseSecurity module manifest (relative to
/// <prefix>/etc/everest/certs unless absolute)
struct StoreLayout {
    std::string certs_dir;
    std::string csms_ca_bundle{"ca/csms/CSMS_ROOT_CA.pem"};
    std::string mf_ca_bundle{"ca/mf/MF_ROOT_CA.pem"};
    std::string mo_ca_bundle{"ca/mo/MO_ROOT_CA.pem"};
    std::string v2g_ca_bundle{"ca/v2g/V2G_ROOT_CA.pem"};
    std::string csms_leaf_cert_directory{"client/csms"};
    std::string csms_leaf_key_directory{"client/csms"};
    std::string secc_leaf_cert_directory{"client/cso"};
    std::string secc_leaf_key_directory{"client/cso"};
    std::string private_key_password;

    evse_security::FilePaths to_file_paths() const;
};

/// SecurityStore backed by libevse-security operating directly on the filesystem (no running EVerest)
class DirectSecurityStore : public SecurityStore {
public:
    explicit DirectSecurityStore(const StoreLayout& layout);

    evse_security::GetCertificateSignRequestResult
    generate_certificate_signing_request(evse_security::LeafCertificateType type, const std::string& country,
                                         const std::string& organization, const std::string& common_name,
                                         bool use_tpm) override;
    void certificate_signing_request_failed(const std::string& csr_pem,
                                            evse_security::LeafCertificateType type) override;
    evse_security::InstallCertificateResult update_leaf_certificate(const std::string& certificate_chain_pem,
                                                                    evse_security::LeafCertificateType type) override;
    evse_security::InstallCertificateResult install_ca_certificate(const std::string& certificate_pem,
                                                                   evse_security::CaCertificateType type) override;
    bool is_ca_certificate_installed(evse_security::CaCertificateType type) override;
    std::optional<LeafInfo> get_leaf_certificate_info(evse_security::LeafCertificateType type) override;
    int get_leaf_expiry_days_count(evse_security::LeafCertificateType type) override;
    std::string get_ca_bundle_pem(evse_security::CaCertificateType type) override;

private:
    std::unique_ptr<evse_security::EvseSecurity> security;
};

} // namespace opcp::tool
