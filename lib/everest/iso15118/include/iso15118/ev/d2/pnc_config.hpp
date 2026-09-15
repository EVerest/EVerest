// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/message_2/certificate_installation.hpp>

namespace iso15118::ev::d2 {

// ISO 15118-2 Plug & Charge (Contract) material. Certificates DER, keys PEM.
struct PnCConfig {
    // Select Contract when the SECC offers it.
    bool prefer_contract{false};
    // Select Contract even when not offered ([V2G2-135] robustness test: the SECC must reject).
    bool enforce_contract{false};

    // OEM provisioning certificate + key: signs CertificateInstallationReq, decrypts the contract key.
    std::vector<uint8_t> oem_prov_cert_der;
    std::string oem_prov_key_pem;
    std::optional<std::string> oem_prov_key_password;

    // Roots declared in CertificateInstallationReq; v2g_root_path verifies the CPS signature.
    std::vector<message_2::RootCertificateId> root_certificate_ids;
    std::string v2g_root_path;

    // Pre-installed contract; presented in PaymentDetailsReq unless force_cert_install.
    std::vector<uint8_t> contract_cert_der;
    std::vector<std::vector<uint8_t>> contract_sub_certs_der;
    std::string contract_key_pem;
    std::optional<std::string> contract_key_password;
    std::string contract_emaid;

    bool force_cert_install{false};

    bool has_contract_cert() const {
        return not contract_cert_der.empty();
    }
    bool needs_cert_install() const {
        return force_cert_install or not has_contract_cert();
    }
};

} // namespace iso15118::ev::d2
