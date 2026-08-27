// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

#include <evse_security/evse_types.hpp>

/// \file security_store.hpp
/// The subset of the evse_security interface the enrollment logic needs. Implemented directly on top of
/// libevse-security by the opcp-enroll tool and on top of the `evse_security` EVerest interface requirement
/// by the OpcpCertificateManager module.
namespace opcp {

struct LeafInfo {
    /// Path of the certificate chain file (leaf + sub-CAs) if present, else of the single leaf file
    std::string certificate_path;
    std::string key_path;
    std::optional<std::string> key_password;
    std::string public_key_algorithm;
};

class SecurityStore {
public:
    virtual ~SecurityStore() = default;

    virtual evse_security::GetCertificateSignRequestResult
    generate_certificate_signing_request(evse_security::LeafCertificateType type, const std::string& country,
                                         const std::string& organization, const std::string& common_name,
                                         bool use_tpm) = 0;

    /// Best effort cleanup of the private key belonging to a CSR that will never receive a certificate.
    /// Stores without that capability (the interface) leave it to garbage collection.
    virtual void certificate_signing_request_failed(const std::string& csr_pem,
                                                    evse_security::LeafCertificateType type) = 0;

    virtual evse_security::InstallCertificateResult
    update_leaf_certificate(const std::string& certificate_chain_pem, evse_security::LeafCertificateType type) = 0;

    virtual evse_security::InstallCertificateResult install_ca_certificate(const std::string& certificate_pem,
                                                                           evse_security::CaCertificateType type) = 0;

    virtual bool is_ca_certificate_installed(evse_security::CaCertificateType type) = 0;

    /// The newest valid leaf of the type with its private key, or std::nullopt when none is usable
    virtual std::optional<LeafInfo> get_leaf_certificate_info(evse_security::LeafCertificateType type) = 0;

    /// Days until the newest leaf of the type expires; 0 when none is installed
    virtual int get_leaf_expiry_days_count(evse_security::LeafCertificateType type) = 0;

    /// PEM bundle of the installed CA certificates of the type (to check that the enrolled chain ends at a
    /// known root). Empty when unavailable.
    virtual std::string get_ca_bundle_pem(evse_security::CaCertificateType type) = 0;
};

} // namespace opcp
