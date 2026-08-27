// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <evse_security/evse_types.hpp>

/// \file types.hpp
/// Basic vocabulary of the OPCP (Open Plug&Charge Protocol) client: which ISO 15118 profile a SECC leaf
/// belongs to, and the root certificate categories of the Root Certificate Pool (RCP).
namespace opcp {

/// ISO 15118 profile a SECC leaf certificate is issued for. The two profiles use different key algorithms
/// (prime256v1 for -2, secp521r1 for -20) and hence different EST endpoints and CPO sub-CA chains.
enum class IsoVersion {
    ISO15118_2,
    ISO15118_20,
};

/// Static properties of an ISO version relevant for enrollment.
struct IsoVersionInfo {
    IsoVersion version;
    /// Human readable name, e.g. "ISO 15118-2"
    const char* name;
    /// Path segment used by the EST endpoints: "ISO15118-2" / "ISO15118-20"
    const char* est_segment;
    /// Curve name expected by the `/cacerts/{iso-version}/{algorithm}/` endpoint
    const char* cacerts_algorithm;
    /// XSD message definition namespace used by the RCP metadata
    const char* xsd_namespace;
    /// Value of `evseISOversion` in the Hubject end entity registration: "15118-2" / "15118-20"
    const char* vra_iso_version;
    /// Leaf certificate type in the evse_security store
    evse_security::LeafCertificateType leaf_type;
};

const IsoVersionInfo& info(IsoVersion version);

/// Parses "2", "20", "ISO15118-2", "ISO15118-20", "15118-2", "15118-20" (case-insensitive)
std::optional<IsoVersion> iso_version_from_string(const std::string& value);

/// Root certificate categories published by the RCP
enum class RootType {
    V2G,
    MO,
    OEM,
};

/// Query parameter value of `GET /v1/root/rootCerts?rootType=` ("v2g", "mo", "oem")
const char* root_type_query_value(RootType type);
std::optional<RootType> root_type_from_string(const std::string& value);
const char* root_type_name(RootType type);

/// Which evse_security CA bundle a root type is installed to. OEM roots have no bundle in the store yet.
std::optional<evse_security::CaCertificateType> ca_certificate_type_for(RootType type);

/// Splits a comma separated list ("v2g, mo") into root types; unknown entries are returned in \p rejected
std::vector<RootType> parse_root_type_list(const std::string& list, std::vector<std::string>& rejected);

} // namespace opcp
