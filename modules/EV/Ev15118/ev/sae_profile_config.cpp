// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include "sae_profile_config.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <ios>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include <iso15118/enum_names.hpp>
#include <iso15118/message/ac_der_sae_names.hpp>
#include <iso15118/sae_modes.hpp>

namespace module {

namespace {

using nlohmann::json;
using Profile = iso15118::ev::SaeInverterProfile;
namespace sae_types = iso15118::message_20::datatypes::sae;

bool fail(const std::string& key, const std::string& problem, std::string& error) {
    error = "SAE inverter profile key " + key + ": " + problem;
    return false;
}

bool read_field(const json& value, const std::string& key, std::string& out, std::string& error) {
    if (not value.is_string()) {
        return fail(key, "must be a JSON string", error);
    }
    out = value.get<std::string>();
    return true;
}

bool read_field(const json& value, const std::string& key, std::optional<std::string>& out, std::string& error) {
    std::string parsed;
    if (not read_field(value, key, parsed, error)) {
        return false;
    }
    out = std::move(parsed);
    return true;
}

bool read_field(const json& value, const std::string& key, bool& out, std::string& error) {
    if (not value.is_boolean()) {
        return fail(key, "must be a JSON boolean", error);
    }
    out = value.get<bool>();
    return true;
}

// Integers are accepted too. A double beyond float range would be undefined to narrow.
bool read_field(const json& value, const std::string& key, float& out, std::string& error) {
    if (not value.is_number()) {
        return fail(key, "must be a JSON number", error);
    }
    const auto raw = value.get<double>();
    if (std::fabs(raw) > std::numeric_limits<float>::max()) {
        return fail(key, "is outside the float range", error);
    }
    out = static_cast<float>(raw);
    return true;
}

template <typename UnsignedT, std::enable_if_t<std::is_unsigned_v<UnsignedT>, int> = 0>
bool read_field(const json& value, const std::string& key, UnsignedT& out, std::string& error) {
    // A value above uint64 max parses as a float, so it fails here too.
    if (not value.is_number_unsigned()) {
        return fail(key, "must be a non-negative JSON integer", error);
    }
    const auto raw = value.get<std::uint64_t>();
    if (raw > std::numeric_limits<UnsignedT>::max()) {
        return fail(key, "exceeds the maximum of " + std::to_string(std::numeric_limits<UnsignedT>::max()), error);
    }
    out = static_cast<UnsignedT>(raw);
    return true;
}

template <typename EnumT, std::enable_if_t<std::is_enum_v<EnumT>, int> = 0>
bool read_field(const json& value, const std::string& key, EnumT& out, std::string& error) {
    static_assert(std::is_same_v<std::underlying_type_t<EnumT>, std::uint8_t>, "the name scan covers uint8_t only");
    constexpr std::size_t value_count = std::size_t{std::numeric_limits<std::uint8_t>::max()} + 1;
    const auto name_of = [](EnumT candidate) { return sae_types::to_string(candidate); };
    std::string name;
    if (not read_field(value, key, name, error)) {
        return false;
    }
    if (const auto parsed = iso15118::enum_from_name<EnumT, value_count>(name, name_of)) {
        out = parsed.value();
        return true;
    }
    std::string expected;
    iso15118::for_each_enum_value<EnumT, value_count>(name_of, [&expected, name_of](EnumT legal) {
        expected += expected.empty() ? "" : ", ";
        expected += name_of(legal);
    });
    return fail(key, "has illegal value \"" + name + "\", expected one of " + expected, error);
}

bool read_supported_modes(const json& value, const std::string& key, std::uint32_t& out, std::string& error) {
    if (not value.is_array()) {
        return fail(key, "must be a JSON array of SAE function names", error);
    }
    std::uint32_t modes = 0;
    for (const auto& element : value) {
        if (not element.is_string()) {
            return fail(key, "must contain only strings", error);
        }
        const auto& name = element.get_ref<const std::string&>();
        const auto function = iso15118::sae::parse_sae_function_name(name);
        if (not function.has_value()) {
            return fail(key, "names an unknown SAE function \"" + name + "\"", error);
        }
        modes |= iso15118::sae::sae_function_bit(function.value());
    }
    out = modes;
    return true;
}

using KeyHandler = std::function<bool(const json&, Profile&, std::string&)>;
using KeyHandlers = std::unordered_map<std::string, KeyHandler>;

template <typename FieldT> void add(KeyHandlers& handlers, const char* key, FieldT Profile::*field) {
    handlers.emplace(key, [key = std::string(key), field](const json& value, Profile& profile, std::string& error) {
        return read_field(value, key, profile.*field, error);
    });
}

// Any key outside this table fails the parse.
const KeyHandlers& key_handlers() {
    static const KeyHandlers handlers = [] {
        KeyHandlers map;
        add(map, "inverter_sw_version", &Profile::inverter_sw_version);
        add(map, "inverter_hw_version", &Profile::inverter_hw_version);
        add(map, "inverter_manufacturer", &Profile::inverter_manufacturer);
        add(map, "inverter_model", &Profile::inverter_model);
        add(map, "inverter_serial_number", &Profile::inverter_serial_number);

        map.emplace("supported_modes", [](const json& value, Profile& profile, std::string& error) {
            return read_supported_modes(value, "supported_modes", profile.supported_modes, error);
        });

        add(map, "max_apparent_power_charging_var_absorption_va",
            &Profile::max_apparent_power_charging_var_absorption_va);
        add(map, "max_apparent_power_charging_var_injection_va",
            &Profile::max_apparent_power_charging_var_injection_va);
        add(map, "max_apparent_power_discharging_var_absorption_va",
            &Profile::max_apparent_power_discharging_var_absorption_va);
        add(map, "max_apparent_power_discharging_var_injection_va",
            &Profile::max_apparent_power_discharging_var_injection_va);

        add(map, "max_var_absorption_charging_var", &Profile::max_var_absorption_charging_var);
        add(map, "max_var_injection_charging_var", &Profile::max_var_injection_charging_var);
        add(map, "max_var_absorption_discharging_var", &Profile::max_var_absorption_discharging_var);
        add(map, "max_var_injection_discharging_var", &Profile::max_var_injection_discharging_var);
        add(map, "reactive_susceptance_s", &Profile::reactive_susceptance_s);

        add(map, "over_excited_power_factor", &Profile::over_excited_power_factor);
        add(map, "over_excited_discharge_power_w", &Profile::over_excited_discharge_power_w);
        add(map, "under_excited_power_factor", &Profile::under_excited_power_factor);
        add(map, "under_excited_discharge_power_w", &Profile::under_excited_discharge_power_w);

        add(map, "nominal_voltage_v", &Profile::nominal_voltage_v);
        add(map, "maximum_voltage_v", &Profile::maximum_voltage_v);
        add(map, "minimum_voltage_v", &Profile::minimum_voltage_v);
        add(map, "nominal_voltage_offset_v", &Profile::nominal_voltage_offset_v);
        add(map, "nominal_frequency_hz", &Profile::nominal_frequency_hz);

        add(map, "ieee1547_normal_category", &Profile::ieee1547_normal_category);
        add(map, "ieee1547_abnormal_category", &Profile::ieee1547_abnormal_category);
        add(map, "j3072_certified", &Profile::j3072_certified);
        add(map, "j3072_certification_date", &Profile::j3072_certification_date);

        add(map, "useable_watt_hours", &Profile::useable_watt_hours);
        add(map, "minimum_charging_duration_s", &Profile::minimum_charging_duration_s);
        add(map, "duration_maximum_charge_rate_s", &Profile::duration_maximum_charge_rate_s);
        add(map, "duration_maximum_discharge_rate_s", &Profile::duration_maximum_discharge_rate_s);

        add(map, "operational_state", &Profile::operational_state);
        add(map, "connection_status", &Profile::connection_status);
        return map;
    }();
    return handlers;
}

} // namespace

std::optional<iso15118::ev::SaeInverterProfile> parse_sae_inverter_profile(const std::string& path,
                                                                           std::string& error) {
    error.clear();

    Profile profile{};
    if (path.empty()) {
        return profile;
    }

    std::ifstream file(path);
    if (not file.is_open()) {
        error = "cannot open SAE inverter profile " + path;
        return std::nullopt;
    }

    // nlohmann keeps the last value of a repeated key, so top-level repeats are recorded here.
    std::unordered_set<std::string> seen_keys;
    std::optional<std::string> repeated_key;
    const auto track_keys = [&](int depth, json::parse_event_t event, json& parsed) {
        if (event == json::parse_event_t::key and depth == 1 and
            not seen_keys.insert(parsed.get<std::string>()).second) {
            repeated_key = parsed.get<std::string>();
        }
        return true;
    };

    json document;
    try {
        document = json::parse(file, track_keys);
    } catch (const json::exception& e) {
        error = "SAE inverter profile " + path + " is not valid JSON: " + e.what();
        return std::nullopt;
    } catch (const std::ios_base::failure& e) {
        // Raised by the file buffer, for example when path names a directory.
        error = "cannot read SAE inverter profile " + path + ": " + e.what();
        return std::nullopt;
    }
    if (not document.is_object()) {
        error = "SAE inverter profile " + path + " must be a JSON object";
        return std::nullopt;
    }
    if (repeated_key.has_value()) {
        error = "SAE inverter profile has repeated key " + repeated_key.value();
        return std::nullopt;
    }

    const auto& handlers = key_handlers();
    for (const auto& [key, value] : document.items()) {
        const auto handler = handlers.find(key);
        if (handler == handlers.end()) {
            error = "SAE inverter profile has unknown key " + key;
            return std::nullopt;
        }
        if (not handler->second(value, profile, error)) {
            return std::nullopt;
        }
    }
    return profile;
}

} // namespace module
