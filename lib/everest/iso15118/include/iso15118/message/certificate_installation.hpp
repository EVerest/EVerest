// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common_types.hpp"

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::message_20 {

namespace datatypes {

// SubCertificates is optional on the wire; an empty vector leaves it out.
struct SignedCertificateChain {
    std::string id;
    Certificate certificate;
    SubCertificate sub_certificates;
};

struct CertificateChain {
    Certificate certificate;
    SubCertificate sub_certificates;
};

enum class EcdhCurve {
    SECP521 = 0,
    X448 = 1,
};

struct SignedInstallationData {
    std::string id;
    ContractCertificateChain contract_certificate_chain;
    EcdhCurve ecdh_curve{EcdhCurve::SECP521};
    std::vector<uint8_t> dh_public_key;
    std::optional<std::vector<uint8_t>> secp521_encrypted_private_key;
    std::optional<std::vector<uint8_t>> x448_encrypted_private_key;
    std::optional<std::vector<uint8_t>> tpm_encrypted_private_key;
};

using EmaidList = everest::lib::util::fixed_vector<Identifier, 8>;

} // namespace datatypes

struct CertificateInstallationRequest {
    Header header;
    datatypes::SignedCertificateChain oem_provisioning_certificate_chain;
    datatypes::ListOfRootCertificateIDs list_of_root_certificate_ids;
    uint8_t maximum_contract_certificate_chains{0};
    std::optional<datatypes::EmaidList> prioritized_emaids;
};

// The SECC builds this itself only for Ongoing and WARNING, with empty placeholders in the mandatory
// chain and installation data ([V2G20-2202]). A positive response is the backend's signed EXI, spliced on.
struct CertificateInstallationResponse {
    Header header;
    datatypes::ResponseCode response_code;
    datatypes::Processing evse_processing{datatypes::Processing::Finished};
    datatypes::CertificateChain cps_certificate_chain;
    datatypes::SignedInstallationData signed_installation_data;
    uint8_t remaining_contract_certificate_chains{0};
};

} // namespace iso15118::message_20
