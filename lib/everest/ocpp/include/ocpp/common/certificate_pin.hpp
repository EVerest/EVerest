// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <string>

#include <ocpp/common/types.hpp>

namespace ocpp {

/// \brief Whether the PEM encoded \p certificate is the one \p expected identifies.
///
/// The hash data is computed exactly as the charging station computes it when it reports its
/// installed certificates, so the four values can be copied from a GetInstalledCertificateIdsResponse
/// straight into the configuration that names a certificate.
///
/// Only self-signed certificates, which is what a root certificate is, can be identified this way:
/// the hash data of anything else is defined in terms of its issuer's certificate, which is not
/// available here. Anything that cannot be identified answers false, so a caller using this to
/// decide whether to trust something fails closed.
bool certificate_matches(const std::string& certificate, const CertificateHashDataType& expected);

} // namespace ocpp
