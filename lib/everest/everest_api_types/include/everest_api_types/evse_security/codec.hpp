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

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::evse_security
