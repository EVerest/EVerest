// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/config_validation.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include <iso15118/sae_modes.hpp>

namespace iso15118::ev {

namespace {

void check_non_negative(std::vector<std::string>& problems, const char* name, float value) {
    if (value < 0.0f) {
        problems.emplace_back(std::string{name} + " must not be negative (is " + std::to_string(value) + ")");
    }
}

// Callers pass the valid range, so NaN fails.
bool require_profile_value(std::vector<std::string>& problems, const char* name, float value, bool valid,
                           const char* rule) {
    if (not valid) {
        problems.emplace_back(std::string{"sae_profile "} + name + " must be " + rule + " (is " +
                              std::to_string(value) + ")");
    }
    return valid;
}

void check_finite(std::vector<std::string>& problems, const char* name, float value) {
    require_profile_value(problems, name, value, std::isfinite(value), "finite");
}

void check_finite_non_negative(std::vector<std::string>& problems, const char* name, float value) {
    require_profile_value(problems, name, value, std::isfinite(value) and value >= 0.0f, "finite and not negative");
}

bool check_finite_positive(std::vector<std::string>& problems, const char* name, float value) {
    return require_profile_value(problems, name, value, std::isfinite(value) and value > 0.0f, "finite and positive");
}

void check_power_factor(std::vector<std::string>& problems, const char* name, float value) {
    require_profile_value(problems, name, value, std::isfinite(value) and value > 0.0f and value <= 1.0f, "in (0, 1]");
}

std::string hex(std::uint32_t bits) {
    char text[11];
    std::snprintf(text, sizeof(text), "0x%08X", static_cast<unsigned int>(bits));
    return text;
}

// AMD1 XSD evInverter*Type: maxLength 32, no minLength. cbv2g encodes ASCII only, one byte each.
void check_inverter_string(std::vector<std::string>& problems, const char* name, const std::string& value) {
    constexpr std::size_t max_length = 32;
    if (value.size() > max_length) {
        problems.emplace_back(std::string{"sae_profile "} + name + " must be at most " + std::to_string(max_length) +
                              " bytes (is " + std::to_string(value.size()) + ")");
    }
}

void check_sae_profile(std::vector<std::string>& problems, const SaeInverterProfile& profile) {
    check_inverter_string(problems, "inverter_sw_version", profile.inverter_sw_version);
    if (profile.inverter_hw_version) {
        check_inverter_string(problems, "inverter_hw_version", *profile.inverter_hw_version);
    }
    check_inverter_string(problems, "inverter_manufacturer", profile.inverter_manufacturer);
    check_inverter_string(problems, "inverter_model", profile.inverter_model);
    check_inverter_string(problems, "inverter_serial_number", profile.inverter_serial_number);

    const auto modes = profile.supported_modes;
    if ((modes & ~sae::SAE_MODE_BITMAP_MASK) != 0) {
        problems.emplace_back("sae_profile supported_modes sets unused bits " +
                              hex(modes & ~sae::SAE_MODE_BITMAP_MASK));
    }
    // AMD1 Table M.6: bits 0 and 1 are inherent to AC DER and always set to 1.
    constexpr auto inherent = sae::sae_function_bit(sae::DerBitMapFunctions::ChargeFunction) |
                              sae::sae_function_bit(sae::DerBitMapFunctions::DischargeFunction);
    if ((modes & inherent) != inherent) {
        problems.emplace_back("sae_profile supported_modes must set ChargeFunction and DischargeFunction (is " +
                              hex(modes) + ")");
    }

    const bool nominal_valid = check_finite_positive(problems, "nominal_voltage_v", profile.nominal_voltage_v);
    const bool min_valid = check_finite_positive(problems, "minimum_voltage_v", profile.minimum_voltage_v);
    const bool max_valid = check_finite_positive(problems, "maximum_voltage_v", profile.maximum_voltage_v);
    if (nominal_valid and min_valid and max_valid) {
        const auto min = std::to_string(profile.minimum_voltage_v);
        const auto max = std::to_string(profile.maximum_voltage_v);
        if (not(profile.maximum_voltage_v > profile.minimum_voltage_v)) {
            problems.emplace_back("sae_profile maximum_voltage_v (" + max + ") must exceed minimum_voltage_v (" + min +
                                  ")");
        } else if (not(profile.nominal_voltage_v >= profile.minimum_voltage_v and
                       profile.nominal_voltage_v <= profile.maximum_voltage_v)) {
            problems.emplace_back("sae_profile nominal_voltage_v (" + std::to_string(profile.nominal_voltage_v) +
                                  ") must lie within [minimum_voltage_v, maximum_voltage_v] = [" + min + ", " + max +
                                  "]");
        }
    }
    check_finite(problems, "nominal_voltage_offset_v", profile.nominal_voltage_offset_v);
    check_finite_positive(problems, "nominal_frequency_hz", profile.nominal_frequency_hz);

    check_power_factor(problems, "over_excited_power_factor", profile.over_excited_power_factor);
    check_power_factor(problems, "under_excited_power_factor", profile.under_excited_power_factor);

    // Totals split per phase.
    check_finite_non_negative(problems, "max_apparent_power_charging_var_absorption_va",
                              profile.max_apparent_power_charging_var_absorption_va);
    check_finite_non_negative(problems, "max_apparent_power_charging_var_injection_va",
                              profile.max_apparent_power_charging_var_injection_va);
    check_finite_non_negative(problems, "max_apparent_power_discharging_var_absorption_va",
                              profile.max_apparent_power_discharging_var_absorption_va);
    check_finite_non_negative(problems, "max_apparent_power_discharging_var_injection_va",
                              profile.max_apparent_power_discharging_var_injection_va);
    check_finite_non_negative(problems, "max_var_absorption_charging_var", profile.max_var_absorption_charging_var);
    check_finite_non_negative(problems, "max_var_injection_charging_var", profile.max_var_injection_charging_var);
    check_finite_non_negative(problems, "max_var_absorption_discharging_var",
                              profile.max_var_absorption_discharging_var);
    check_finite_non_negative(problems, "max_var_injection_discharging_var", profile.max_var_injection_discharging_var);
    check_finite_non_negative(problems, "reactive_susceptance_s", profile.reactive_susceptance_s);
    check_finite_non_negative(problems, "over_excited_discharge_power_w", profile.over_excited_discharge_power_w);
    check_finite_non_negative(problems, "under_excited_discharge_power_w", profile.under_excited_discharge_power_w);
}

void check_min_not_above_max(std::vector<std::string>& problems, const char* min_name, float min_value,
                             const char* max_name, float max_value) {
    if (min_value > max_value) {
        problems.emplace_back(std::string{min_name} + " (" + std::to_string(min_value) + ") must not exceed " +
                              max_name + " (" + std::to_string(max_value) + ")");
    }
}

} // namespace

std::vector<std::string> validate_config(const EvConfig& config) {
    std::vector<std::string> problems;

    if (config.response_timeout.count() < 0) {
        problems.emplace_back("response_timeout must not be negative (is " +
                              std::to_string(config.response_timeout.count()) + " ms)");
    }

    // -20 identifierType: 1..255 characters. A MAC string is one valid form of it.
    if (config.evcc_id.empty() or config.evcc_id.size() > 255) {
        problems.emplace_back("evcc_id must be 1 to 255 characters (is '" + config.evcc_id + "')");
    }

    if (config.supported_protocols.empty() and config.advertised_app_protocols.empty()) {
        problems.emplace_back("supported_protocols is empty; nothing to offer in SupportedAppProtocolReq");
    }

    // Without SDP there is no security byte to reject, so the configured one has to satisfy the policy.
    if (config.tls.enforce_tls and not config.enable_sdp and config.direct_security != io::v2gtp::Security::TLS) {
        problems.emplace_back("enforce_tls is set but direct_security is not TLS");
    }

    if (config.cpd_rounds == 0) {
        problems.emplace_back("cpd_rounds must be positive (is 0)");
    }

    if (config.energy_service == message_20::datatypes::ServiceCategory::AC_DER_SAE) {
        check_sae_profile(problems, config.sae_profile);
    }

    return problems;
}

std::vector<std::string> validate_ac_charge_params(const AcChargeParams& params) {
    std::vector<std::string> problems;

    if (params.phase_count != 1 and params.phase_count != 3) {
        problems.emplace_back("ac phase_count must be 1 or 3 (is " + std::to_string(params.phase_count) + ")");
    }

    check_non_negative(problems, "ac max_charge_power", params.max_charge_power);
    check_non_negative(problems, "ac min_charge_power", params.min_charge_power);
    check_non_negative(problems, "ac max_discharge_power", params.max_discharge_power);
    check_non_negative(problems, "ac min_discharge_power", params.min_discharge_power);

    check_min_not_above_max(problems, "ac min_charge_power", params.min_charge_power, "ac max_charge_power",
                            params.max_charge_power);
    check_min_not_above_max(problems, "ac min_discharge_power", params.min_discharge_power, "ac max_discharge_power",
                            params.max_discharge_power);

    return problems;
}

std::vector<std::string> validate_dc_charge_params(const DcChargeParams& params) {
    std::vector<std::string> problems;

    check_non_negative(problems, "dc max_charge_power", params.max_charge_power);
    check_non_negative(problems, "dc max_charge_current", params.max_charge_current);
    check_non_negative(problems, "dc max_discharge_power", params.max_discharge_power);
    check_non_negative(problems, "dc min_discharge_power", params.min_discharge_power);
    check_non_negative(problems, "dc max_discharge_current", params.max_discharge_current);
    check_non_negative(problems, "dc max_voltage", params.max_voltage);
    check_non_negative(problems, "dc min_voltage", params.min_voltage);
    check_non_negative(problems, "dc energy_capacity", params.energy_capacity);

    check_min_not_above_max(problems, "dc min_discharge_power", params.min_discharge_power, "dc max_discharge_power",
                            params.max_discharge_power);
    check_min_not_above_max(problems, "dc min_voltage", params.min_voltage, "dc max_voltage", params.max_voltage);

    return problems;
}

} // namespace iso15118::ev
