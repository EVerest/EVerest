// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>
#include <string>

namespace module {

/// \brief Where the DC voltage on the EV side of the EVSE output contactors is measured, see
/// ev_side_voltage_source.
enum class EvSideVoltageSource {
    PowermeterCarSide,
    OverVoltageMonitor,
    IsolationMonitor,
    PowerSupply,
};

std::optional<EvSideVoltageSource> ev_side_voltage_source_from_string(const std::string& s);
std::string to_string(EvSideVoltageSource source);

struct EvSideVoltageSources {
    bool powermeter_car_side{false};
    bool over_voltage_monitor{false};
    bool isolation_monitor{false};
};

/// \brief The configured source if connected, else the next connected one after it in the order of
/// EvSideVoltageSource, skipping the isolation monitor, which only an explicit choice selects. The power
/// supply is always available.
EvSideVoltageSource resolve_ev_side_voltage_source(EvSideVoltageSource configured,
                                                   const EvSideVoltageSources& connected);

} // namespace module
