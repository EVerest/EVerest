// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iso15118::ev::d2 {

// A contract: leaf and sub-CA certificates DER, key PEM, and the eMAID the leaf names.
struct ContractMaterial {
    std::vector<uint8_t> cert_der;
    std::vector<std::vector<uint8_t>> sub_certs_der;
    std::string key_pem;
    std::optional<std::string> key_password;
    std::string emaid;
};

// ISO 15118-2 Plug & Charge (Contract) material. Certificates DER, keys PEM.
struct PnCConfig {
    // Select Contract when the SECC offers it.
    bool prefer_contract{false};
    // Select Contract even when not offered ([V2G2-465] robustness test: the SECC must reject).
    // A conformance-test override, subordinate to allow_insecure_pnc: it forces Contract against a
    // charger that did not offer it, not onto a plaintext link.
    bool enforce_contract{false};
    // Accept the deviation from the standard: present a contract over a session with no transport
    // security. Off means the vehicle charges by EIM on a plaintext link instead, which is the
    // correction this repository's own charger already makes from the other end.
    bool allow_insecure_pnc{false};

    // OEM provisioning certificate + key: signs CertificateInstallationReq, decrypts the contract key.
    std::vector<uint8_t> oem_prov_cert_der;
    std::string oem_prov_key_pem;
    std::optional<std::string> oem_prov_key_password;

    // Pre-installed contract; presented in PaymentDetailsReq unless force_cert_install.
    std::optional<ContractMaterial> contract;

    bool force_cert_install{false};

    bool needs_cert_install() const {
        return force_cert_install or not contract.has_value();
    }
};

} // namespace iso15118::ev::d2
