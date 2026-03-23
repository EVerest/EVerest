// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest_api_types/evse_security/API.hpp>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "generated/types/evse_security.hpp"
#pragma GCC diagnostic pop

namespace everest::lib::API::V1_0::types::evse_security {

using json = nlohmann::json;

void to_json(json& j, CaCertificateType const& k) noexcept;
void from_json(const json& j, CaCertificateType& k);

void to_json(json& j, LeafCertificateType const& k) noexcept;
void from_json(const json& j, LeafCertificateType& k);

void to_json(json& j, EncodingFormat const& k) noexcept;
void from_json(const json& j, EncodingFormat& k);

void to_json(json& j, GetLeafCertificateInfoRequest const& k) noexcept;
void from_json(const json& j, GetLeafCertificateInfoRequest& k);

void to_json(json& j, GetCertificateInfoStatus const& k) noexcept;
void from_json(const json& j, GetCertificateInfoStatus& k);

void to_json(json& j, HashAlgorithm const& k) noexcept;
void from_json(const json& j, HashAlgorithm& k);

void to_json(json& j, CertificateHashData const& k) noexcept;
void from_json(const json& j, CertificateHashData& k);

void to_json(json& j, CertificateOCSP const& k) noexcept;
void from_json(const json& j, CertificateOCSP& k);

void to_json(json& j, CertificateInfo const& k) noexcept;
void from_json(const json& j, CertificateInfo& k);

void to_json(json& j, GetCertificateInfoResult const& k) noexcept;
void from_json(const json& j, GetCertificateInfoResult& k);

} // namespace everest::lib::API::V1_0::types::evse_security
