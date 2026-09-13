// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/config_validation.hpp>

#include <cctype>
#include <string>

namespace iso15118::ev {

namespace {

constexpr std::size_t MAC_LENGTH = 17;

bool is_hex_digit(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

// "aa:bb:cc:dd:ee:ff", either case: six colon-separated hex pairs.
bool is_mac_formatted(const std::string& value) {
    if (value.size() != MAC_LENGTH) {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        const bool separator_position = (i % 3) == 2;
        if (separator_position) {
            if (value[i] != ':') {
                return false;
            }
        } else if (not is_hex_digit(value[i])) {
            return false;
        }
    }
    return true;
}

void check_non_negative(std::vector<std::string>& problems, const char* name, float value) {
    if (value < 0.0f) {
        problems.emplace_back(std::string{name} + " must not be negative (is " + std::to_string(value) + ")");
    }
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

    if (config.response_timeout.count() <= 0) {
        problems.emplace_back("response_timeout must be positive (is " +
                              std::to_string(config.response_timeout.count()) + " ms)");
    }

    if (not is_mac_formatted(config.evcc_id)) {
        problems.emplace_back("evcc_id must be MAC-formatted, six colon-separated hex pairs (is '" + config.evcc_id +
                              "')");
    }

    return problems;
}

std::vector<std::string> validate_ac_charge_params(const AcChargeParams& params) {
    std::vector<std::string> problems;

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
