// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>

#include <generated/types/power_supply_DC.hpp>
#include <generated/types/powermeter.hpp>

namespace module {

/// \brief Raises the minimum current limits of the power supply capabilities \p caps according to the minimum
///        currents the power meter reported in \p meter_capabilities.
///        \p log_warnings should be disabled by callers in hot paths: the min-exceeds-max warnings are then
///        left to the (input-change deduplicated) capability update paths instead of repeating every call.
types::power_supply_DC::Capabilities
apply_powermeter_limits(types::power_supply_DC::Capabilities caps,
                        const std::optional<types::powermeter::Capabilities>& meter_capabilities,
                        bool log_warnings = true);

} // namespace module
