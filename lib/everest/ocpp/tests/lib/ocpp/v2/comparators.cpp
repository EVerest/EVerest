// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <comparators.hpp>

namespace ocpp {

bool operator==(const CertificateHashDataType& a, const CertificateHashDataType& b) {
    return a.serialNumber == b.serialNumber && a.issuerKeyHash == b.issuerKeyHash &&
           a.issuerNameHash == b.issuerNameHash && a.hashAlgorithm == b.hashAlgorithm;
}

} // namespace ocpp

namespace ocpp::v2 {

bool operator==(const GetCertificateStatusRequest& a, const GetCertificateStatusRequest& b) {
    return a.ocspRequestData.serialNumber == b.ocspRequestData.serialNumber &&
           a.ocspRequestData.issuerKeyHash == b.ocspRequestData.issuerKeyHash &&
           a.ocspRequestData.issuerNameHash == b.ocspRequestData.issuerNameHash &&
           a.ocspRequestData.hashAlgorithm == b.ocspRequestData.hashAlgorithm &&
           a.ocspRequestData.responderURL == b.ocspRequestData.responderURL;
}

bool operator==(const ChargingProfile& a, const ChargingProfile& b) {
    return a.chargingProfileKind == b.chargingProfileKind && a.chargingProfilePurpose == b.chargingProfilePurpose &&
           a.id == b.id && a.stackLevel == b.stackLevel;
}

} // namespace ocpp::v2
