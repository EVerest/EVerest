// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "powermeter_limits.hpp"

#include <algorithm>

#include <everest/logging.hpp>
#include <everest/util/math/comparison.hpp>

namespace module {

types::power_supply_DC::Capabilities
apply_powermeter_limits(types::power_supply_DC::Capabilities caps,
                        const std::optional<types::powermeter::Capabilities>& meter_capabilities, bool log_warnings) {
    if (not meter_capabilities.has_value()) {
        return caps;
    }

    const auto& meter = meter_capabilities.value();

    if (meter.min_import_current_A.has_value()) {
        const float meter_min = meter.min_import_current_A.value();
        caps.min_export_current_A = std::max(caps.min_export_current_A, meter_min);
        // Only raise a nominal minimum the power supply actually reported: fabricating one here
        // would bypass the consumers' fallback to the (clamped) regular minimum.
        if (caps.nominal_min_export_current_A.has_value()) {
            caps.nominal_min_export_current_A = std::max(caps.nominal_min_export_current_A.value(), meter_min);
        }

        if (caps.min_export_current_A > caps.max_export_current_A) {
            if (log_warnings) {
                EVLOG_warning << "Power meter minimum current in charging direction (" << meter_min
                              << " A) exceeds power supply maximum (" << caps.max_export_current_A
                              << " A), clamping minimum to maximum";
            }
            caps.min_export_current_A = caps.max_export_current_A;
        }
        if (caps.nominal_min_export_current_A.has_value() and caps.nominal_max_export_current_A.has_value() and
            caps.nominal_min_export_current_A.value() > caps.nominal_max_export_current_A.value()) {
            caps.nominal_min_export_current_A = caps.nominal_max_export_current_A;
        }
    }

    if (meter.min_export_current_A.has_value()) {
        const float meter_min = meter.min_export_current_A.value();
        caps.min_import_current_A = everest::lib::util::max_optional(caps.min_import_current_A, meter_min);
        if (caps.nominal_min_import_current_A.has_value()) {
            caps.nominal_min_import_current_A = std::max(caps.nominal_min_import_current_A.value(), meter_min);
        }

        if (caps.max_import_current_A.has_value() and
            caps.min_import_current_A.value() > caps.max_import_current_A.value()) {
            if (log_warnings) {
                EVLOG_warning << "Power meter minimum current in discharge direction (" << meter_min
                              << " A) exceeds power supply maximum (" << caps.max_import_current_A.value()
                              << " A), clamping minimum to maximum";
            }
            caps.min_import_current_A = caps.max_import_current_A;
        }
        if (caps.nominal_min_import_current_A.has_value() and caps.nominal_max_import_current_A.has_value() and
            caps.nominal_min_import_current_A.value() > caps.nominal_max_import_current_A.value()) {
            caps.nominal_min_import_current_A = caps.nominal_max_import_current_A;
        }
    }

    return caps;
}

} // namespace module
