// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace everest::lib::API::V1_0::types::evse_security {

enum class CaCertificateType {
    V2G,
    MO,
    CSMS,
    MF,
};

enum class LeafCertificateType {
    CSMS,
    V2G,
    MF,
    MO,
};

enum class EncodingFormat {
    DER,
    PEM,
};

struct GetLeafCertificateInfoRequest {
    LeafCertificateType certificate_type;
    EncodingFormat encoding;
    bool include_ocsp;
};

enum class GetCertificateInfoStatus {
    Accepted,
    Rejected,
    NotFound,
    NotFoundValid,
    PrivateKeyNotFound,
};

enum class HashAlgorithm {
    SHA256,
    SHA384,
    SHA512,
};

struct CertificateHashData {
    HashAlgorithm hash_algorithm;
    std::string issuer_name_hash;
    std::string issuer_key_hash;
    std::string serial_number;
};

struct CertificateOCSP {
    CertificateHashData hash;
    std::optional<std::string> ocsp_path;
};

struct CertificateInfo {
    std::string key;
    std::optional<std::string> certificate_root;
    std::optional<std::string> certificate;
    std::optional<std::string> certificate_single;
    int32_t certificate_count;
    std::optional<std::string> password;
    std::optional<std::vector<CertificateOCSP>> ocsp;
};

struct GetCertificateInfoResult {
    GetCertificateInfoStatus status;
    std::optional<CertificateInfo> info;
};

} // namespace everest::lib::API::V1_0::types::evse_security
