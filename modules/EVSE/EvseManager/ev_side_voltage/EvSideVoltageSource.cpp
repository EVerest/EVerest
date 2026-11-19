// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "EvSideVoltageSource.hpp"

#include <array>
#include <utility>

namespace module {

namespace {
constexpr std::array<std::pair<EvSideVoltageSource, const char*>, 4> SOURCE_NAMES{{
    {EvSideVoltageSource::PowermeterCarSide, "powermeter_car_side"},
    {EvSideVoltageSource::OverVoltageMonitor, "over_voltage_monitor"},
    {EvSideVoltageSource::IsolationMonitor, "isolation_monitor"},
    {EvSideVoltageSource::PowerSupply, "power_supply"},
}};

bool is_connected(EvSideVoltageSource source, const EvSideVoltageSources& connected) {
    switch (source) {
    case EvSideVoltageSource::PowermeterCarSide:
        return connected.powermeter_car_side;
    case EvSideVoltageSource::OverVoltageMonitor:
        return connected.over_voltage_monitor;
    case EvSideVoltageSource::IsolationMonitor:
        return connected.isolation_monitor;
    case EvSideVoltageSource::PowerSupply:
        return true;
    }
    return false;
}
} // namespace

std::optional<EvSideVoltageSource> ev_side_voltage_source_from_string(const std::string& s) {
    for (const auto& [source, name] : SOURCE_NAMES) {
        if (s == name) {
            return source;
        }
    }
    return std::nullopt;
}

std::string to_string(EvSideVoltageSource source) {
    for (const auto& [candidate, name] : SOURCE_NAMES) {
        if (candidate == source) {
            return name;
        }
    }
    return "unknown";
}

EvSideVoltageSource resolve_ev_side_voltage_source(EvSideVoltageSource configured,
                                                   const EvSideVoltageSources& connected) {
    if (is_connected(configured, connected)) {
        return configured;
    }
    bool reached = false;
    for (const auto& [source, name] : SOURCE_NAMES) {
        reached = reached or source == configured;
        // Only an explicit choice selects the isolation monitor, which deviates from IEC 61851-23 CC.9 t103.
        if (reached and source != EvSideVoltageSource::IsolationMonitor and is_connected(source, connected)) {
            return source;
        }
    }
    return EvSideVoltageSource::PowerSupply;
}

} // namespace module
