// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/helpers/helpers.hpp>
#include <everest/tls/openssl_util.hpp>

#include <unordered_map>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <fmt/format.h>

#include <generated/types/authorization.hpp>
#include <generated/types/powermeter.hpp>

namespace everest::helpers {
std::string redact(const std::string& token) {
    auto hash = std::hash<std::string>{}(token);
    return fmt::format("[redacted] hash: {:X}", hash);
}

types::authorization::ProvidedIdToken redact(const types::authorization::ProvidedIdToken& token) {
    types::authorization::ProvidedIdToken redacted_token = token;
    redacted_token.id_token.value = redact(redacted_token.id_token.value);
    if (redacted_token.parent_id_token.has_value()) {
        auto& parent_id_token = redacted_token.parent_id_token.value();
        parent_id_token.value = redact(parent_id_token.value);
    }
    return redacted_token;
}

std::string escape_html(const std::string& html) {
    std::string escaped_html;
    escaped_html.reserve(html.size());
    for (const auto& character : html) {
        switch (character) {
        case '\"':
            escaped_html.append("&quot;");
            break;
        case '\'':
            escaped_html.append("&apos;");
            break;
        case '&':
            escaped_html.append("&amp;");
            break;
        case '<':
            escaped_html.append("&lt;");
            break;
        case '>':
            escaped_html.append("&gt;");
            break;
        default:
            escaped_html.push_back(character);
            break;
        }
    }
    return escaped_html;
}

bool is_equal_case_insensitive(const std::string& str1, const std::string& str2) {
    if (str1.length() != str2.length()) {
        return false;
    }
    return std::equal(str1.begin(), str1.end(), str2.begin(),
                      [](char a, char b) { return std::tolower(a) == std::tolower(b); });
}

bool is_equal_case_insensitive(const types::authorization::IdToken& token1,
                               const types::authorization::IdToken& token2) {
    return is_equal_case_insensitive(token1.value, token2.value) && token1.type == token2.type;
}

bool is_equal_case_insensitive(const types::authorization::ProvidedIdToken& token1,
                               const types::authorization::ProvidedIdToken& token2) {
    return is_equal_case_insensitive(token1.id_token.value, token2.id_token.value) &&
           token1.id_token.type == token2.id_token.type;
}

namespace {
boost::uuids::random_generator& get_rng() {
    static thread_local boost::uuids::random_generator rng;
    return rng;
}
} // namespace

std::string get_uuid() {
    return boost::uuids::to_string(get_rng()()); // 36 characters
}

std::string get_base64_uuid() {
    boost::uuids::uuid uuid = get_rng()();
    std::string encoded = openssl::base64_encode(uuid.data, uuid.size(), false);
    encoded.erase(std::remove(encoded.begin(), encoded.end(), '='), encoded.end()); // remove padding
    return encoded;                                                                 // 22 characters
}

std::string get_base64_id() {
    std::array<std::uint8_t, 12> random_bytes;
    boost::uuids::uuid uuid = get_rng()();
    std::memcpy(random_bytes.data(), uuid.data, random_bytes.size());
    std::string encoded = openssl::base64_encode(random_bytes.data(), random_bytes.size(), false);
    return encoded; // 16 characters
}

namespace {

enum class PhaseRotation {
    RST, ///< no rotation, the reported phases already match the grid phases
    TRS, ///< reported L1/L2/L3 are grid L3/L1/L2
    STR, ///< reported L1/L2/L3 are grid L2/L3/L1
};

/// \returns the PhaseRotation for the given OCPP-style notation, RST for any unknown value
PhaseRotation parse_phase_rotation(const std::string& phase_rotation) {
    if (phase_rotation == "TRS") {
        return PhaseRotation::TRS;
    }
    if (phase_rotation == "STR") {
        return PhaseRotation::STR;
    }
    return PhaseRotation::RST;
}

/// Remaps the L1/L2/L3 members of a single per-phase measurement according to \p rotation .
/// Members that are invariant under a phase rotation (total, DC, N) are left untouched.
template <typename T> void rotate_phases(T& measurement, PhaseRotation rotation) {
    const auto l1 = measurement.L1;
    const auto l2 = measurement.L2;
    const auto l3 = measurement.L3;
    switch (rotation) {
    case PhaseRotation::TRS:
        measurement.L1 = l2;
        measurement.L2 = l3;
        measurement.L3 = l1;
        break;
    case PhaseRotation::STR:
        measurement.L1 = l3;
        measurement.L2 = l1;
        measurement.L3 = l2;
        break;
    case PhaseRotation::RST:
        break;
    }
}

/// Remaps every given optional per-phase measurement, skipping the ones that are not set
template <typename... T> void rotate_optional_phases(PhaseRotation rotation, std::optional<T>&... measurements) {
    ((measurements.has_value() ? rotate_phases(*measurements, rotation) : void()), ...);
}

} // namespace

types::powermeter::Powermeter apply_phase_rotation(types::powermeter::Powermeter powermeter,
                                                   const std::string& phase_rotation) {
    const auto rotation = parse_phase_rotation(phase_rotation);
    if (rotation == PhaseRotation::RST) {
        return powermeter;
    }

    rotate_phases(powermeter.energy_Wh_import, rotation);
    rotate_optional_phases(rotation, powermeter.energy_Wh_export, powermeter.power_W, powermeter.voltage_V,
                           powermeter.VAR, powermeter.current_A, powermeter.energy_Wh_import_signed,
                           powermeter.energy_Wh_export_signed, powermeter.power_W_signed, powermeter.voltage_V_signed,
                           powermeter.VAR_signed, powermeter.current_A_signed);

    return powermeter;
}

} // namespace everest::helpers
