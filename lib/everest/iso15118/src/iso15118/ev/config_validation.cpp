// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/config_validation.hpp>

#include <cctype>
#include <string>

namespace iso15118::ev {

namespace {

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
