// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/opcp/types.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

namespace opcp {

namespace {
constexpr std::array<IsoVersionInfo, 2> ISO_VERSIONS{{
    {IsoVersion::ISO15118_2, "ISO 15118-2", "ISO15118-2", "secp256r1", "urn:iso:15118:2:2013:MsgDef", "15118-2",
     evse_security::LeafCertificateType::V2G},
    {IsoVersion::ISO15118_20, "ISO 15118-20", "ISO15118-20", "secp521r1", "urn:iso:15118:20:2022:MsgDef", "15118-20",
     evse_security::LeafCertificateType::V2G20},
}};

std::string lowercase_trimmed(const std::string& value) {
    std::string out;
    for (const char c : value) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}
} // namespace

const IsoVersionInfo& info(IsoVersion version) {
    for (const auto& entry : ISO_VERSIONS) {
        if (entry.version == version) {
            return entry;
        }
    }
    return ISO_VERSIONS.front();
}

std::optional<IsoVersion> iso_version_from_string(const std::string& value) {
    const std::string v = lowercase_trimmed(value);
    if (v == "2" || v == "-2" || v == "iso15118-2" || v == "15118-2" || v == "iso15118_2" || v == "iso2") {
        return IsoVersion::ISO15118_2;
    }
    if (v == "20" || v == "-20" || v == "iso15118-20" || v == "15118-20" || v == "iso15118_20" || v == "iso20") {
        return IsoVersion::ISO15118_20;
    }
    return std::nullopt;
}

const char* root_type_query_value(RootType type) {
    switch (type) {
    case RootType::V2G:
        return "v2g";
    case RootType::MO:
        return "mo";
    case RootType::OEM:
        return "oem";
    }
    return "v2g";
}

const char* root_type_name(RootType type) {
    switch (type) {
    case RootType::V2G:
        return "V2G";
    case RootType::MO:
        return "MO";
    case RootType::OEM:
        return "OEM";
    }
    return "V2G";
}

std::optional<RootType> root_type_from_string(const std::string& value) {
    const std::string v = lowercase_trimmed(value);
    if (v == "v2g") {
        return RootType::V2G;
    }
    if (v == "mo") {
        return RootType::MO;
    }
    if (v == "oem") {
        return RootType::OEM;
    }
    return std::nullopt;
}

std::optional<evse_security::CaCertificateType> ca_certificate_type_for(RootType type) {
    switch (type) {
    case RootType::V2G:
        return evse_security::CaCertificateType::V2G;
    case RootType::MO:
        return evse_security::CaCertificateType::MO;
    case RootType::OEM:
        // The evse_security store has no OEM bundle; the SECC never validates OEM provisioning chains itself
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<RootType> parse_root_type_list(const std::string& list, std::vector<std::string>& rejected) {
    std::vector<RootType> out;
    std::stringstream stream(list);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (lowercase_trimmed(item).empty()) {
            continue;
        }
        const auto type = root_type_from_string(item);
        if (!type.has_value()) {
            rejected.push_back(item);
            continue;
        }
        if (std::find(out.begin(), out.end(), *type) == out.end()) {
            out.push_back(*type);
        }
    }
    return out;
}

} // namespace opcp
