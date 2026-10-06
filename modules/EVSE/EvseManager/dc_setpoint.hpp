// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>

#include <generated/types/power_supply_DC.hpp>

namespace module {

/// \brief Minimum charging current offered to the EV in ChargeParameterDiscovery: the nominal minimum in
///        \p hlc_caps if reported, else the minimum. EvseV2G and Evse15118D20 select the same value.
float offered_min_export_current_A(const types::power_supply_DC::Capabilities& hlc_caps);

/// \brief Minimum discharging current offered to the EV, selected like offered_min_export_current_A().
std::optional<float> offered_min_import_current_A(const types::power_supply_DC::Capabilities& hlc_caps);

/// \brief Charging current to set on the power supply for the ramped \p current towards \p ev_target_current.
///        \p evse_max_current is the EVSE maximum current limit from energy management, \p caps are the power
///        supply capabilities, \p hlc_caps the capabilities offered to the EV.
///        During current demand: 0 A if \p ev_target_current or \p evse_max_current is below the offered minimum
///        (IEC 61851-23:2023 CC.5.5.7), otherwise at least the offered minimum and the power supply minimum.
///        Otherwise: at least the power supply minimum.
double dc_export_setpoint_current(double current, double ev_target_current, double evse_max_current,
                                  bool current_demand_active, const types::power_supply_DC::Capabilities& caps,
                                  const types::power_supply_DC::Capabilities& hlc_caps);

/// \brief Discharging current to set on the power supply during current demand, same rules as
///        dc_export_setpoint_current() with the EVSE maximum discharge current limit \p evse_max_current.
double dc_import_setpoint_current(double current, double ev_target_current, double evse_max_current,
                                  const types::power_supply_DC::Capabilities& caps,
                                  const types::power_supply_DC::Capabilities& hlc_caps);

} // namespace module
