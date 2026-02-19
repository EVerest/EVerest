// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once
#include <optional>
#include <string>
#include <vector>

namespace everest::lib::API::V1_0::types::entrypoint {

enum class ApiTypeEnum {
    auth_consumer,
    auth_token_provider,
    auth_token_validator,
    dc_external_derate_consumer,
    display_message,
    error_history_consumer,
    evse_board_support,
    evse_manager_consumer,
    external_energy_limits_consumer,
    generic_error_raiser,
    isolation_monitor,
    ocpp_consumer,
    over_voltage_monitor,
    powermeter,
    power_supply_DC,
    session_cost,
    session_cost_consumer,
    slac,
    system
};

struct ApiParameter {
    ApiTypeEnum type;
    std::string module_id;
    std::optional<int32_t> version;
};

struct ApiDiscoverResponse {
    std::vector<ApiParameter> apis;
};

} // namespace everest::lib::API::V1_0::types::entrypoint
