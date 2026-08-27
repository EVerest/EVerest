// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/opcp/security_store.hpp>
#include <generated/interfaces/evse_security/Interface.hpp>

namespace module {

/// opcp::SecurityStore on top of the evse_security interface requirement
class InterfaceSecurityStore : public opcp::SecurityStore {
public:
    explicit InterfaceSecurityStore(evse_securityIntf& security);

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
    std::optional<opcp::LeafInfo> get_leaf_certificate_info(evse_security::LeafCertificateType type) override;
    int get_leaf_expiry_days_count(evse_security::LeafCertificateType type) override;
    std::string get_ca_bundle_pem(evse_security::CaCertificateType type) override;

private:
    evse_securityIntf& security;
};

} // namespace module
