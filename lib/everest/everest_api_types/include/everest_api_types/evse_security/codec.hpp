// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include "API.hpp"

namespace everest::lib::API::V1_0::types::evse_security {

std::string serialize(CaCertificateType val) noexcept;
std::string serialize(LeafCertificateType val) noexcept;
std::string serialize(EncodingFormat val) noexcept;
std::string serialize(GetLeafCertificateInfoRequest val) noexcept;
std::string serialize(GetCertificateInfoStatus val) noexcept;
std::string serialize(HashAlgorithm val) noexcept;
std::string serialize(CertificateHashData val) noexcept;
std::string serialize(CertificateOCSP val) noexcept;
std::string serialize(CertificateInfo val) noexcept;
std::string serialize(GetCertificateInfoResult val) noexcept;

std::ostream& operator<<(std::ostream& os, CaCertificateType const& val);
std::ostream& operator<<(std::ostream& os, LeafCertificateType const& val);
std::ostream& operator<<(std::ostream& os, EncodingFormat const& val);
std::ostream& operator<<(std::ostream& os, GetLeafCertificateInfoRequest const& val);
std::ostream& operator<<(std::ostream& os, GetCertificateInfoStatus const& val);
std::ostream& operator<<(std::ostream& os, HashAlgorithm const& val);
std::ostream& operator<<(std::ostream& os, CertificateHashData const& val);
std::ostream& operator<<(std::ostream& os, CertificateOCSP const& val);
std::ostream& operator<<(std::ostream& os, CertificateInfo const& val);
std::ostream& operator<<(std::ostream& os, GetCertificateInfoResult const& val);

template <class T> T deserialize(std::string const& val);
template <class T> std::optional<T> try_deserialize(std::string const& val) {
    try {
        return deserialize<T>(val);
    } catch (...) {
        return std::nullopt;
    }
}
template <class T> bool adl_deserialize(std::string const& json_data, T& obj) {
    auto opt = try_deserialize<T>(json_data);
    if (opt) {
        obj = opt.value();
        return true;
    }
    return false;
}

} // namespace everest::lib::API::V1_0::types::evse_security
