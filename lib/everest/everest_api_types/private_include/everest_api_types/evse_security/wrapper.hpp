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

using CaCertificateType_Internal = ::types::evse_security::CaCertificateType;
using CaCertificateType_External = CaCertificateType;

CaCertificateType_Internal to_internal_api(CaCertificateType_External const& val);
CaCertificateType_External to_external_api(CaCertificateType_Internal const& val);

using LeafCertificateType_Internal = ::types::evse_security::LeafCertificateType;
using LeafCertificateType_External = LeafCertificateType;

LeafCertificateType_Internal to_internal_api(LeafCertificateType_External const& val);
LeafCertificateType_External to_external_api(LeafCertificateType_Internal const& val);

using EncodingFormat_Internal = ::types::evse_security::EncodingFormat;
using EncodingFormat_External = EncodingFormat;

EncodingFormat_Internal to_internal_api(EncodingFormat_External const& val);
EncodingFormat_External to_external_api(EncodingFormat_Internal const& val);

using GetCertificateInfoStatus_Internal = ::types::evse_security::GetCertificateInfoStatus;
using GetCertificateInfoStatus_External = GetCertificateInfoStatus;

GetCertificateInfoStatus_Internal to_internal_api(GetCertificateInfoStatus_External const& val);
GetCertificateInfoStatus_External to_external_api(GetCertificateInfoStatus_Internal const& val);

using HashAlgorithm_Internal = ::types::evse_security::HashAlgorithm;
using HashAlgorithm_External = HashAlgorithm;

HashAlgorithm_Internal to_internal_api(HashAlgorithm_External const& val);
HashAlgorithm_External to_external_api(HashAlgorithm_Internal const& val);

using CertificateHashData_Internal = ::types::evse_security::CertificateHashData;
using CertificateHashData_External = CertificateHashData;

CertificateHashData_Internal to_internal_api(CertificateHashData_External const& val);
CertificateHashData_External to_external_api(CertificateHashData_Internal const& val);

using CertificateOCSP_Internal = ::types::evse_security::CertificateOCSP;
using CertificateOCSP_External = CertificateOCSP;

CertificateOCSP_Internal to_internal_api(CertificateOCSP_External const& val);
CertificateOCSP_External to_external_api(CertificateOCSP_Internal const& val);

using CertificateInfo_Internal = ::types::evse_security::CertificateInfo;
using CertificateInfo_External = CertificateInfo;

CertificateInfo_Internal to_internal_api(CertificateInfo_External const& val);
CertificateInfo_External to_external_api(CertificateInfo_Internal const& val);

using GetCertificateInfoResult_Internal = ::types::evse_security::GetCertificateInfoResult;
using GetCertificateInfoResult_External = GetCertificateInfoResult;

GetCertificateInfoResult_Internal to_internal_api(GetCertificateInfoResult_External const& val);
GetCertificateInfoResult_External to_external_api(GetCertificateInfoResult_Internal const& val);

} // namespace everest::lib::API::V1_0::types::evse_security
