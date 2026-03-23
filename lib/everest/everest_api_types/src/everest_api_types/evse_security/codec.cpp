// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "evse_security/codec.hpp"
#include "evse_security/API.hpp"
#include "evse_security/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include <stdexcept>
#include <string>

namespace everest::lib::API::V1_0::types::evse_security {

std::string serialize(CaCertificateType val) noexcept {
    json result = val;
    return result.dump(json_indent);
}
std::string serialize(LeafCertificateType val) noexcept {
    json result = val;
    return result.dump(json_indent);
}
std::string serialize(EncodingFormat val) noexcept {
    json result = val;
    return result.dump(json_indent);
}
std::string serialize(GetLeafCertificateInfoRequest val) noexcept {
    json result = val;
    return result.dump(json_indent);
}
std::string serialize(GetCertificateInfoStatus val) noexcept {
    json result = val;
    return result.dump(json_indent);
}
std::string serialize(HashAlgorithm val) noexcept {
    json result = val;
    return result.dump(json_indent);
}
std::string serialize(CertificateHashData val) noexcept {
    json result = val;
    return result.dump(json_indent);
}
std::string serialize(CertificateOCSP val) noexcept {
    json result = val;
    return result.dump(json_indent);
}
std::string serialize(CertificateInfo val) noexcept {
    json result = val;
    return result.dump(json_indent);
}
std::string serialize(GetCertificateInfoResult val) noexcept {
    json result = val;
    return result.dump(json_indent);
}

std::ostream& operator<<(std::ostream& os, CaCertificateType const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, LeafCertificateType const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, EncodingFormat const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, GetLeafCertificateInfoRequest const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, GetCertificateInfoStatus const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, HashAlgorithm const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, CertificateHashData const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, CertificateOCSP const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, CertificateInfo const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, GetCertificateInfoResult const& val) {
    os << serialize(val);
    return os;
}

template <> CaCertificateType deserialize(std::string const& val) {
    auto data = json::parse(val);
    CaCertificateType obj = data;
    return obj;
}

template <> LeafCertificateType deserialize(std::string const& val) {
    auto data = json::parse(val);
    LeafCertificateType obj = data;
    return obj;
}

template <> EncodingFormat deserialize(std::string const& val) {
    auto data = json::parse(val);
    EncodingFormat obj = data;
    return obj;
}

template <> GetLeafCertificateInfoRequest deserialize(std::string const& val) {
    auto data = json::parse(val);
    GetLeafCertificateInfoRequest obj = data;
    return obj;
}

template <> GetCertificateInfoStatus deserialize(std::string const& val) {
    auto data = json::parse(val);
    GetCertificateInfoStatus obj = data;
    return obj;
}

template <> HashAlgorithm deserialize(std::string const& val) {
    auto data = json::parse(val);
    HashAlgorithm obj = data;
    return obj;
}

template <> CertificateHashData deserialize(std::string const& val) {
    auto data = json::parse(val);
    CertificateHashData obj = data;
    return obj;
}

template <> CertificateOCSP deserialize(std::string const& val) {
    auto data = json::parse(val);
    CertificateOCSP obj = data;
    return obj;
}

template <> CertificateInfo deserialize(std::string const& val) {
    auto data = json::parse(val);
    CertificateInfo obj = data;
    return obj;
}

template <> GetCertificateInfoResult deserialize(std::string const& val) {
    auto data = json::parse(val);
    GetCertificateInfoResult obj = data;
    return obj;
}

} // namespace everest::lib::API::V1_0::types::evse_security
