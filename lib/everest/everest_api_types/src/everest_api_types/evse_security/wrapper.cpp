// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "evse_security/wrapper.hpp"
#include "evse_security/API.hpp"
#include <vector>

namespace everest::lib::API::V1_0::types {

namespace {
using namespace evse_security;
template <class SrcT, class ConvT>
auto srcToTarOpt(std::optional<SrcT> const& src, ConvT const& converter)
    -> std::optional<decltype(converter(src.value()))> {
    if (src) {
        return std::make_optional(converter(src.value()));
    }
    return std::nullopt;
}

template <class SrcT, class ConvT> auto srcToTarVec(std::vector<SrcT> const& src, ConvT const& converter) {
    using TarT = decltype(converter(src[0]));
    std::vector<TarT> result;
    for (SrcT const& elem : src) {
        result.push_back(converter(elem));
    }
    return result;
}

template <class SrcT>
auto optToInternal(std::optional<SrcT> const& src) -> std::optional<decltype(to_internal_api(src.value()))> {
    return srcToTarOpt(src, [](SrcT const& val) { return to_internal_api(val); });
}

template <class SrcT>
auto optToExternal(std::optional<SrcT> const& src) -> std::optional<decltype(to_external_api(src.value()))> {
    return srcToTarOpt(src, [](SrcT const& val) { return to_external_api(val); });
}

template <class SrcT> auto vecToExternal(std::vector<SrcT> const& src) {
    return srcToTarVec(src, [](SrcT const& val) { return to_external_api(val); });
}

template <class SrcT> auto vecToInternal(std::vector<SrcT> const& src) {
    return srcToTarVec(src, [](SrcT const& val) { return to_internal_api(val); });
}
} // namespace

namespace evse_security {

CaCertificateType_Internal to_internal_api(CaCertificateType_External const& val) {
    using SrcT = CaCertificateType_External;
    using TarT = CaCertificateType_Internal;
    switch (val) {
    case SrcT::V2G:
        return TarT::V2G;
    case SrcT::MO:
        return TarT::MO;
    case SrcT::CSMS:
        return TarT::CSMS;
    case SrcT::MF:
        return TarT::MF;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::evse_security::CaCertificateType_External");
}

CaCertificateType_External to_external_api(CaCertificateType_Internal const& val) {
    using SrcT = CaCertificateType_Internal;
    using TarT = CaCertificateType_External;
    switch (val) {
    case SrcT::V2G:
        return TarT::V2G;
    case SrcT::MO:
        return TarT::MO;
    case SrcT::CSMS:
        return TarT::CSMS;
    case SrcT::MF:
        return TarT::MF;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::evse_security::CaCertificateType_Internal");
}

LeafCertificateType_Internal to_internal_api(LeafCertificateType_External const& val) {
    using SrcT = LeafCertificateType_External;
    using TarT = LeafCertificateType_Internal;
    switch (val) {
    case SrcT::CSMS:
        return TarT::CSMS;
    case SrcT::V2G:
        return TarT::V2G;
    case SrcT::MF:
        return TarT::MF;
    case SrcT::MO:
        return TarT::MO;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::evse_security::LeafCertificateType_External");
}

LeafCertificateType_External to_external_api(LeafCertificateType_Internal const& val) {
    using SrcT = LeafCertificateType_Internal;
    using TarT = LeafCertificateType_External;
    switch (val) {
    case SrcT::CSMS:
        return TarT::CSMS;
    case SrcT::V2G:
        return TarT::V2G;
    case SrcT::MF:
        return TarT::MF;
    case SrcT::MO:
        return TarT::MO;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::evse_security::LeafCertificateType_Internal");
}

EncodingFormat_Internal to_internal_api(EncodingFormat_External const& val) {
    using SrcT = EncodingFormat_External;
    using TarT = EncodingFormat_Internal;
    switch (val) {
    case SrcT::DER:
        return TarT::DER;
    case SrcT::PEM:
        return TarT::PEM;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::evse_security::EncodingFormat_External");
}

EncodingFormat_External to_external_api(EncodingFormat_Internal const& val) {
    using SrcT = EncodingFormat_Internal;
    using TarT = EncodingFormat_External;
    switch (val) {
    case SrcT::DER:
        return TarT::DER;
    case SrcT::PEM:
        return TarT::PEM;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::evse_security::EncodingFormat_Internal");
}

GetCertificateInfoStatus_Internal to_internal_api(GetCertificateInfoStatus_External const& val) {
    using SrcT = GetCertificateInfoStatus_External;
    using TarT = GetCertificateInfoStatus_Internal;
    switch (val) {
    case SrcT::Accepted:
        return TarT::Accepted;
    case SrcT::Rejected:
        return TarT::Rejected;
    case SrcT::NotFound:
        return TarT::NotFound;
    case SrcT::NotFoundValid:
        return TarT::NotFoundValid;
    case SrcT::PrivateKeyNotFound:
        return TarT::PrivateKeyNotFound;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::evse_security::GetCertificateInfoStatus_External");
}

GetCertificateInfoStatus_External to_external_api(GetCertificateInfoStatus_Internal const& val) {
    using SrcT = GetCertificateInfoStatus_Internal;
    using TarT = GetCertificateInfoStatus_External;
    switch (val) {
    case SrcT::Accepted:
        return TarT::Accepted;
    case SrcT::Rejected:
        return TarT::Rejected;
    case SrcT::NotFound:
        return TarT::NotFound;
    case SrcT::NotFoundValid:
        return TarT::NotFoundValid;
    case SrcT::PrivateKeyNotFound:
        return TarT::PrivateKeyNotFound;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::evse_security::GetCertificateInfoStatus_Internal");
}

HashAlgorithm_Internal to_internal_api(HashAlgorithm_External const& val) {
    using SrcT = HashAlgorithm_External;
    using TarT = HashAlgorithm_Internal;
    switch (val) {
    case SrcT::SHA256:
        return TarT::SHA256;
    case SrcT::SHA384:
        return TarT::SHA384;
    case SrcT::SHA512:
        return TarT::SHA512;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::evse_security::HashAlgorithm_External");
}

HashAlgorithm_External to_external_api(HashAlgorithm_Internal const& val) {
    using SrcT = HashAlgorithm_Internal;
    using TarT = HashAlgorithm_External;
    switch (val) {
    case SrcT::SHA256:
        return TarT::SHA256;
    case SrcT::SHA384:
        return TarT::SHA384;
    case SrcT::SHA512:
        return TarT::SHA512;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::evse_security::HashAlgorithm_Internal");
}

CertificateHashData_Internal to_internal_api(CertificateHashData_External const& val) {
    CertificateHashData_Internal result;
    result.hash_algorithm = to_internal_api(val.hash_algorithm);
    result.issuer_name_hash = val.issuer_name_hash;
    result.issuer_key_hash = val.issuer_key_hash;
    result.serial_number = val.serial_number;
    return result;
}

CertificateHashData_External to_external_api(CertificateHashData_Internal const& val) {
    CertificateHashData_External result;
    result.hash_algorithm = to_external_api(val.hash_algorithm);
    result.issuer_name_hash = val.issuer_name_hash;
    result.issuer_key_hash = val.issuer_key_hash;
    result.serial_number = val.serial_number;
    return result;
}

CertificateOCSP_Internal to_internal_api(CertificateOCSP_External const& val) {
    CertificateOCSP_Internal result;
    result.hash = to_internal_api(val.hash);
    result.ocsp_path = val.ocsp_path;
    return result;
}

CertificateOCSP_External to_external_api(CertificateOCSP_Internal const& val) {
    CertificateOCSP_External result;
    result.hash = to_external_api(val.hash);
    result.ocsp_path = val.ocsp_path;
    return result;
}

CertificateInfo_Internal to_internal_api(CertificateInfo_External const& val) {
    CertificateInfo_Internal result;
    result.key = val.key;
    result.certificate_root = val.certificate_root;
    result.certificate = val.certificate;
    result.certificate_single = val.certificate_single;
    result.certificate_count = val.certificate_count;
    result.password = val.password;
    if (val.ocsp) {
        result.ocsp = vecToInternal(val.ocsp.value());
    }
    return result;
}

CertificateInfo_External to_external_api(CertificateInfo_Internal const& val) {
    CertificateInfo_External result;
    result.key = val.key;
    result.certificate_root = val.certificate_root;
    result.certificate = val.certificate;
    result.certificate_single = val.certificate_single;
    result.certificate_count = val.certificate_count;
    result.password = val.password;
    if (val.ocsp) {
        result.ocsp = vecToExternal(val.ocsp.value());
    }
    return result;
}

GetCertificateInfoResult_Internal to_internal_api(GetCertificateInfoResult_External const& val) {
    GetCertificateInfoResult_Internal result;
    result.status = to_internal_api(val.status);
    result.info = optToInternal(val.info);
    return result;
}

GetCertificateInfoResult_External to_external_api(GetCertificateInfoResult_Internal const& val) {
    GetCertificateInfoResult_External result;
    result.status = to_external_api(val.status);
    result.info = optToExternal(val.info);
    return result;
}

} // namespace evse_security
} // namespace everest::lib::API::V1_0::types
