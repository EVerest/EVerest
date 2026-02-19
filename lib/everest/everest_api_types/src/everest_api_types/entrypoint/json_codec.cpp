// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "entrypoint/json_codec.hpp"
#include "entrypoint/API.hpp"
#include "entrypoint/codec.hpp"

#include "nlohmann/json.hpp"

#include <iostream>

namespace everest::lib::API::V1_0::types::entrypoint {

void to_json(json& j, ApiTypeEnum const& k) noexcept {
    switch (k) {
    case ApiTypeEnum::auth_consumer:
        j = "auth_consumer";
        return;
    case ApiTypeEnum::auth_token_provider:
        j = "auth_token_provider";
        return;
    case ApiTypeEnum::auth_token_validator:
        j = "auth_token_validator";
        return;
    case ApiTypeEnum::dc_external_derate_consumer:
        j = "dc_external_derate_consumer";
        return;
    case ApiTypeEnum::display_message:
        j = "display_message";
        return;
    case ApiTypeEnum::error_history_consumer:
        j = "error_history_consumer";
        return;
    case ApiTypeEnum::evse_board_support:
        j = "evse_board_support";
        return;
    case ApiTypeEnum::evse_manager_consumer:
        j = "evse_manager_consumer";
        return;
    case ApiTypeEnum::external_energy_limits_consumer:
        j = "external_energy_limits_consumer";
        return;
    case ApiTypeEnum::generic_error_raiser:
        j = "generic_error_raiser";
        return;
    case ApiTypeEnum::isolation_monitor:
        j = "isolation_monitor";
        return;
    case ApiTypeEnum::ocpp_consumer:
        j = "ocpp_consumer";
        return;
    case ApiTypeEnum::over_voltage_monitor:
        j = "over_voltage_monitor";
        return;
    case ApiTypeEnum::powermeter:
        j = "powermeter";
        return;
    case ApiTypeEnum::power_supply_DC:
        j = "power_supply_DC";
        return;
    case ApiTypeEnum::session_cost:
        j = "session_cost";
        return;
    case ApiTypeEnum::session_cost_consumer:
        j = "session_cost_consumer";
        return;
    case ApiTypeEnum::slac:
        j = "slac";
        return;
    case ApiTypeEnum::system:
        j = "system";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::entrypoint::ApiTypeEnum";
}

void from_json(const json& j, ApiTypeEnum& k) {
    std::string s = j;

    if (s == "auth_consumer") {
        k = ApiTypeEnum::auth_consumer;
        return;
    }
    if (s == "auth_token_provider") {
        k = ApiTypeEnum::auth_token_provider;
        return;
    }
    if (s == "auth_token_validator") {
        k = ApiTypeEnum::auth_token_validator;
        return;
    }
    if (s == "dc_external_derate_consumer") {
        k = ApiTypeEnum::dc_external_derate_consumer;
        return;
    }
    if (s == "display_message") {
        k = ApiTypeEnum::display_message;
        return;
    }
    if (s == "error_history_consumer") {
        k = ApiTypeEnum::error_history_consumer;
        return;
    }
    if (s == "evse_board_support") {
        k = ApiTypeEnum::evse_board_support;
        return;
    }
    if (s == "evse_manager_consumer") {
        k = ApiTypeEnum::evse_manager_consumer;
        return;
    }
    if (s == "external_energy_limits_consumer") {
        k = ApiTypeEnum::external_energy_limits_consumer;
        return;
    }
    if (s == "generic_error_raiser") {
        k = ApiTypeEnum::generic_error_raiser;
        return;
    }
    if (s == "isolation_monitor") {
        k = ApiTypeEnum::isolation_monitor;
        return;
    }
    if (s == "ocpp_consumer") {
        k = ApiTypeEnum::ocpp_consumer;
        return;
    }
    if (s == "over_voltage_monitor") {
        k = ApiTypeEnum::over_voltage_monitor;
        return;
    }
    if (s == "powermeter") {
        k = ApiTypeEnum::powermeter;
        return;
    }
    if (s == "power_supply_DC") {
        k = ApiTypeEnum::power_supply_DC;
        return;
    }
    if (s == "session_cost") {
        k = ApiTypeEnum::session_cost;
        return;
    }
    if (s == "session_cost_consumer") {
        k = ApiTypeEnum::session_cost_consumer;
        return;
    }
    if (s == "slac") {
        k = ApiTypeEnum::slac;
        return;
    }
    if (s == "system") {
        k = ApiTypeEnum::system;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::entrypoint::ApiTypeEnum");
}

void to_json(json& j, ApiParameter const& k) noexcept {
    j = json{
        {"type", k.type},
        {"module_id", k.module_id},
    };
    if (k.version) {
        j["version"] = k.version.value();
    }
}

void from_json(const json& j, ApiParameter& k) {
    k.type = j.at("type");
    k.module_id = j.at("module_id");
    if (j.contains("version")) {
        k.version.emplace(j.at("version"));
    }
}

void to_json(json& j, ApiDiscoverResponse const& k) noexcept {
    j = json::array();
    for (auto val : k.apis) {
        j.push_back(val);
    }
}

void from_json(const json& j, ApiDiscoverResponse& k) {
    k.apis.clear();
    for (auto val : j) {
        k.apis.push_back(val);
    }
}

} // namespace everest::lib::API::V1_0::types::entrypoint
