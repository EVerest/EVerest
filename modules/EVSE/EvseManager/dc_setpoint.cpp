// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "dc_setpoint.hpp"

#include <algorithm>

namespace module {

namespace {
double current_demand_current(double current, double ev_target_current, double evse_max_current,
                              std::optional<float> offered_minimum, std::optional<float> psu_minimum) {
    if (not offered_minimum.has_value()) {
        return current;
    }
    if (ev_target_current < offered_minimum.value() or evse_max_current < offered_minimum.value()) {
        return 0.0;
    }
    const double minimum = std::max(offered_minimum.value(), psu_minimum.value_or(0.0f));
    return std::max(current, minimum);
}
} // namespace

float offered_min_export_current_A(const types::power_supply_DC::Capabilities& hlc_caps) {
    return hlc_caps.nominal_min_export_current_A.value_or(hlc_caps.min_export_current_A);
}

std::optional<float> offered_min_import_current_A(const types::power_supply_DC::Capabilities& hlc_caps) {
    if (hlc_caps.nominal_min_import_current_A.has_value()) {
        return hlc_caps.nominal_min_import_current_A;
    }
    return hlc_caps.min_import_current_A;
}

double dc_export_setpoint_current(double current, double ev_target_current, double evse_max_current,
                                  bool current_demand_active, const types::power_supply_DC::Capabilities& caps,
                                  const types::power_supply_DC::Capabilities& hlc_caps) {
    current = std::min(current, static_cast<double>(caps.max_export_current_A));
    if (current_demand_active) {
        return current_demand_current(current, ev_target_current, evse_max_current,
                                      offered_min_export_current_A(hlc_caps), caps.min_export_current_A);
    }
    return std::max(current, static_cast<double>(caps.min_export_current_A));
}

double dc_import_setpoint_current(double current, double ev_target_current, double evse_max_current,
                                  const types::power_supply_DC::Capabilities& caps,
                                  const types::power_supply_DC::Capabilities& hlc_caps) {
    if (caps.max_import_current_A.has_value()) {
        current = std::min(current, static_cast<double>(caps.max_import_current_A.value()));
    }
    return current_demand_current(current, ev_target_current, evse_max_current, offered_min_import_current_A(hlc_caps),
                                  caps.min_import_current_A);
}

} // namespace module
