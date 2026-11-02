// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <generated/types/energy.hpp>
#include <ocpp/common/types.hpp>
#include <ocpp/v2/types.hpp>

namespace ocpp_module_common {
namespace external_limits {

/// \brief The most restrictive of the per-phase values of a charging schedule period: the one with the smallest
/// magnitude, which serves limits, negative discharge limits and setpoints alike. types::energy::LimitsReq and
/// SetpointType carry one current for all phases, so per-phase currents collapse here into that one value. This drops
/// the per-phase information an ISO 15118-20 AC session could use. OCPP 2.1 K01.FR.147 prescribes the same collapse for
/// the fallback to Mode 3 PWM.
float most_restrictive_phase_value(float value, const std::optional<float>& value_L2,
                                   const std::optional<float>& value_L3);

/// \brief Total power in W of a charging schedule period value (limit, dischargeLimit or setpoint). With
/// chargingRateUnit W the value is the sum over all phases unless per-phase values are provided, in which case it is
/// phase L1 (OCPP 2.1 ChargingSchedulePeriodType) and the first \p number_phases phases are summed up. A phase without
/// a value takes the value of phase L1.
float total_power_W(float value, const std::optional<float>& value_L2, const std::optional<float>& value_L3,
                    const std::optional<std::int32_t>& number_phases);

/// \brief Converts a composite schedule into the limits for the energy sink of its EVSE: limits become import limits,
/// discharge limits export limits. Each period is stamped with \p start_time plus its startPeriod. Limits carry
/// \p source, setpoints the priority \p setpoint_priority.
types::energy::ExternalLimits to_external_limits(const ocpp::v2::EnhancedCompositeSchedule& composite_schedule,
                                                 const ocpp::DateTime& start_time, const std::string& source,
                                                 std::int32_t setpoint_priority);

} // namespace external_limits
} // namespace ocpp_module_common
