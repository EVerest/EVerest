// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#ifndef TESTS_OCPP_COMPARATORS_H
#define TESTS_OCPP_COMPARATORS_H

#include <ocpp/common/types.hpp>
#include <ocpp/v2/messages/GetCertificateStatus.hpp>
#include <ocpp/v2/types.hpp>

namespace ocpp {

bool operator==(const CertificateHashDataType& a, const CertificateHashDataType& b);

} // namespace ocpp

namespace ocpp::v2 {

bool operator==(const GetCertificateStatusRequest& a, const GetCertificateStatusRequest& b);
bool operator==(const ChargingProfile& a, const ChargingProfile& b);

} // namespace ocpp::v2

#endif // TESTS_OCPP_COMPARATORS_H
