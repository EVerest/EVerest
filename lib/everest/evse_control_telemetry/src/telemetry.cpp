// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/evse_control/telemetry.hpp>
#include <nlohmann/json.hpp>

namespace everest::lib::evse_control {

using json = nlohmann::json;

namespace {

template <typename T>
void load(json const& j, char const* key, T& dst) {
    if (j.contains(key)) {
        dst = j.at(key).get<T>();
    }
}

} // namespace

void to_json(json& j, EvseControlStatus const& k) noexcept {
    j["authorisation_finished"] = k.authorisation_finished;
    j["emergency_stop"] = k.emergency_stop;
    j["error_stop"] = k.error_stop;
    j["normal_stop"] = k.normal_stop;
    j["contract_payment_enabled"] = k.contract_payment_enabled;
    j["free_charging_enabled"] = k.free_charging_enabled;
}

void from_json(json const& j, EvseControlStatus& k) {
    load(j, "authorisation_finished", k.authorisation_finished);
    load(j, "emergency_stop", k.emergency_stop);
    load(j, "error_stop", k.error_stop);
    load(j, "normal_stop", k.normal_stop);
    load(j, "contract_payment_enabled", k.contract_payment_enabled);
    load(j, "free_charging_enabled", k.free_charging_enabled);
}

std::string serialize(EvseControlStatus const& v) {
    return json(v).dump(0);
}

EvseControlStatus deserialize(std::string const& s) {
    return json::parse(s).get<EvseControlStatus>();
}

} // namespace everest::lib::evse_control
