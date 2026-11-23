// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "offered_limits.hpp"

namespace ocpp_multi {

OfferedLimits to_offered_limits(const types::energy::LimitsRes& limits,
                                const std::function<float()>& supply_voltage_V) {
    OfferedLimits offered;
    if (limits.ac_max_current_A) {
        offered.max_current_A = limits.ac_max_current_A->value;
    }
    if (limits.total_power_W) {
        offered.max_power_W = limits.total_power_W->value;
        return offered;
    }
    if (limits.ac_max_current_A and limits.ac_max_phase_count) {
        offered.max_power_W =
            limits.ac_max_current_A->value * static_cast<float>(limits.ac_max_phase_count->value) * supply_voltage_V();
    }
    return offered;
}

std::optional<OfferedLimits> OfferedLimitsCache::update(std::int32_t evse_id, const types::energy::LimitsRes& limits,
                                                        const std::function<float()>& supply_voltage_V) {
    {
        auto entries = m_entries.handle();
        const auto it = entries->find(evse_id);
        if (it != entries->end() and it->second.limits == limits) {
            return std::nullopt;
        }
    }
    // not under the lock: reading the supply voltage calls into libocpp
    const auto offered = to_offered_limits(limits, supply_voltage_V);
    m_entries.handle()->insert_or_assign(evse_id, Entry{limits, offered});
    return offered;
}

OfferedLimits OfferedLimitsCache::get(std::int32_t evse_id) {
    auto entries = m_entries.handle();
    const auto it = entries->find(evse_id);
    return it != entries->end() ? it->second.offered : OfferedLimits{};
}

void OfferedLimitsCache::clear() {
    m_entries.handle()->clear();
}

} // namespace ocpp_multi
