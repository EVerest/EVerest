// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace everest::lib::evse_control {

// Control-state telemetry sourced from EvseManager's session lifecycle and
// authorization/payment configuration. Consumed by downstream telemetry
// subscribers (e.g. the Tritium ccu_rtm75_everest rte shim).
struct EvseControlStatus {
    bool authorisation_finished{false};
    bool emergency_stop{false};
    bool error_stop{false};
    bool normal_stop{false};
    bool contract_payment_enabled{false};
    bool free_charging_enabled{false};
};

void to_json(nlohmann::json& j, EvseControlStatus const&) noexcept;
void from_json(nlohmann::json const& j, EvseControlStatus&);

std::string serialize(EvseControlStatus const&);
EvseControlStatus deserialize(std::string const&);

} // namespace everest::lib::evse_control
