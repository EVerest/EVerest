// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "evse_security/json_codec.hpp"
#include "evse_security/API.hpp"
#include "evse_security/codec.hpp"

#include <stdexcept>

#include "nlohmann/json.hpp"

namespace everest::lib::API::V1_0::types::evse_security {

using json = nlohmann::json;

void to_json(json& j, CaCertificateType const& k) noexcept {
    switch (k) {
    case CaCertificateType::V2G:
        j = "V2G";
        return;
    case CaCertificateType::MO:
        j = "MO";
        return;
    case CaCertificateType::CSMS:
        j = "CSMS";
        return;
    case CaCertificateType::MF:
        j = "MF";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::evse_security::CaCertificateType";
}

void from_json(const json& j, CaCertificateType& k) {
    std::string s = j;
    if (s == "V2G") {
        k = CaCertificateType::V2G;
        return;
    }
    if (s == "MO") {
        k = CaCertificateType::MO;
        return;
    }
    if (s == "CSMS") {
        k = CaCertificateType::CSMS;
        return;
    }
    if (s == "MF") {
        k = CaCertificateType::MF;
        return;
    }
    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::evse_security::CaCertificateType");
}

void to_json(json& j, LeafCertificateType const& k) noexcept {
    switch (k) {
    case LeafCertificateType::CSMS:
        j = "CSMS";
        return;
    case LeafCertificateType::V2G:
        j = "V2G";
        return;
    case LeafCertificateType::MF:
        j = "MF";
        return;
    case LeafCertificateType::MO:
        j = "MO";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::evse_security::LeafCertificateType";
}

void from_json(const json& j, LeafCertificateType& k) {
    std::string s = j;
    if (s == "CSMS") {
        k = LeafCertificateType::CSMS;
        return;
    }
    if (s == "V2G") {
        k = LeafCertificateType::V2G;
        return;
    }
    if (s == "MF") {
        k = LeafCertificateType::MF;
        return;
    }
    if (s == "MO") {
        k = LeafCertificateType::MO;
        return;
    }
    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::evse_security::LeafCertificateType");
}

void to_json(json& j, EncodingFormat const& k) noexcept {
    switch (k) {
    case EncodingFormat::DER:
        j = "DER";
        return;
    case EncodingFormat::PEM:
        j = "PEM";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::evse_security::EncodingFormat";
}

void from_json(const json& j, EncodingFormat& k) {
    std::string s = j;
    if (s == "DER") {
        k = EncodingFormat::DER;
        return;
    }
    if (s == "PEM") {
        k = EncodingFormat::PEM;
        return;
    }
    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::evse_security::EncodingFormat");
}

void to_json(json& j, GetLeafCertificateInfoRequest const& k) noexcept {
    j = json{{"certificate_type", k.certificate_type}, {"encoding", k.encoding}, {"include_ocsp", k.include_ocsp}};
}

void from_json(const json& j, GetLeafCertificateInfoRequest& k) {
    k.certificate_type = j.at("certificate_type");
    k.encoding = j.at("encoding");
    k.include_ocsp = j.at("include_ocsp");
}

void to_json(json& j, GetCertificateInfoStatus const& k) noexcept {
    switch (k) {
    case GetCertificateInfoStatus::Accepted:
        j = "Accepted";
        return;
    case GetCertificateInfoStatus::Rejected:
        j = "Rejected";
        return;
    case GetCertificateInfoStatus::NotFound:
        j = "NotFound";
        return;
    case GetCertificateInfoStatus::NotFoundValid:
        j = "NotFoundValid";
        return;
    case GetCertificateInfoStatus::PrivateKeyNotFound:
        j = "PrivateKeyNotFound";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::evse_security::GetCertificateInfoStatus";
}

void from_json(const json& j, GetCertificateInfoStatus& k) {
    std::string s = j;
    if (s == "Accepted") {
        k = GetCertificateInfoStatus::Accepted;
        return;
    }
    if (s == "Rejected") {
        k = GetCertificateInfoStatus::Rejected;
        return;
    }
    if (s == "NotFound") {
        k = GetCertificateInfoStatus::NotFound;
        return;
    }
    if (s == "NotFoundValid") {
        k = GetCertificateInfoStatus::NotFoundValid;
        return;
    }
    if (s == "PrivateKeyNotFound") {
        k = GetCertificateInfoStatus::PrivateKeyNotFound;
        return;
    }
    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type "
                            "everest::lib::API::V1_0::types::evse_security::GetCertificateInfoStatus");
}

void to_json(json& j, HashAlgorithm const& k) noexcept {
    switch (k) {
    case HashAlgorithm::SHA256:
        j = "SHA256";
        return;
    case HashAlgorithm::SHA384:
        j = "SHA384";
        return;
    case HashAlgorithm::SHA512:
        j = "SHA512";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::evse_security::HashAlgorithm";
}

void from_json(const json& j, HashAlgorithm& k) {
    std::string s = j;
    if (s == "SHA256") {
        k = HashAlgorithm::SHA256;
        return;
    }
    if (s == "SHA384") {
        k = HashAlgorithm::SHA384;
        return;
    }
    if (s == "SHA512") {
        k = HashAlgorithm::SHA512;
        return;
    }
    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::evse_security::HashAlgorithm");
}

void to_json(json& j, CertificateHashData const& k) noexcept {
    j = json{{"hash_algorithm", k.hash_algorithm},
             {"issuer_name_hash", k.issuer_name_hash},
             {"issuer_key_hash", k.issuer_key_hash},
             {"serial_number", k.serial_number}};
}

void from_json(const json& j, CertificateHashData& k) {
    k.hash_algorithm = j.at("hash_algorithm");
    k.issuer_name_hash = j.at("issuer_name_hash");
    k.issuer_key_hash = j.at("issuer_key_hash");
    k.serial_number = j.at("serial_number");
}

void to_json(json& j, CertificateOCSP const& k) noexcept {
    j = json{{"hash", k.hash}};
    if (k.ocsp_path) {
        j["ocsp_path"] = k.ocsp_path.value();
    }
}

void from_json(const json& j, CertificateOCSP& k) {
    k.hash = j.at("hash");
    if (j.contains("ocsp_path")) {
        k.ocsp_path.emplace(j.at("ocsp_path"));
    }
}

void to_json(json& j, CertificateInfo const& k) noexcept {
    j = json{{"key", k.key}, {"certificate_count", k.certificate_count}};
    if (k.certificate_root) {
        j["certificate_root"] = k.certificate_root.value();
    }
    if (k.certificate) {
        j["certificate"] = k.certificate.value();
    }
    if (k.certificate_single) {
        j["certificate_single"] = k.certificate_single.value();
    }
    if (k.password) {
        j["password"] = k.password.value();
    }
    if (k.ocsp) {
        j["ocsp"] = json::array();
        for (auto val : k.ocsp.value()) {
            j["ocsp"].push_back(val);
        }
    }
}

void from_json(const json& j, CertificateInfo& k) {
    k.key = j.at("key");
    if (j.contains("certificate_root")) {
        k.certificate_root.emplace(j.at("certificate_root"));
    }
    if (j.contains("certificate")) {
        k.certificate.emplace(j.at("certificate"));
    }
    if (j.contains("certificate_single")) {
        k.certificate_single.emplace(j.at("certificate_single"));
    }
    k.certificate_count = j.at("certificate_count");
    if (j.contains("password")) {
        k.password.emplace(j.at("password"));
    }
    if (j.contains("ocsp")) {
        json arr = j.at("ocsp");
        std::vector<CertificateOCSP> vec;
        for (auto val : arr) {
            vec.push_back(val);
        }
        k.ocsp.emplace(vec);
    }
}

void to_json(json& j, GetCertificateInfoResult const& k) noexcept {
    j = json{{"status", k.status}};
    if (k.info) {
        j["info"] = k.info.value();
    }
}

void from_json(const json& j, GetCertificateInfoResult& k) {
    k.status = j.at("status");
    if (j.contains("info")) {
        k.info.emplace(j.at("info"));
    }
}

} // namespace everest::lib::API::V1_0::types::evse_security
