// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "evse_security/codec.hpp"
#include "evse_security/API.hpp"
#include "evse_security/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

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

template <> CaCertificateType deserialize(std::string_view val) {
    return utilities::parse_json<CaCertificateType>(val);
}

template <> LeafCertificateType deserialize(std::string_view val) {
    return utilities::parse_json<LeafCertificateType>(val);
}

template <> EncodingFormat deserialize(std::string_view val) {
    return utilities::parse_json<EncodingFormat>(val);
}

template <> GetLeafCertificateInfoRequest deserialize(std::string_view val) {
    return utilities::parse_json<GetLeafCertificateInfoRequest>(val);
}

template <> GetCertificateInfoStatus deserialize(std::string_view val) {
    return utilities::parse_json<GetCertificateInfoStatus>(val);
}

template <> HashAlgorithm deserialize(std::string_view val) {
    return utilities::parse_json<HashAlgorithm>(val);
}

template <> CertificateHashData deserialize(std::string_view val) {
    return utilities::parse_json<CertificateHashData>(val);
}

template <> CertificateOCSP deserialize(std::string_view val) {
    return utilities::parse_json<CertificateOCSP>(val);
}

template <> CertificateInfo deserialize(std::string_view val) {
    return utilities::parse_json<CertificateInfo>(val);
}

template <> GetCertificateInfoResult deserialize(std::string_view val) {
    return utilities::parse_json<GetCertificateInfoResult>(val);
}

} // namespace everest::lib::API::V1_0::types::evse_security
