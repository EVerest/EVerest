// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include <date/tz.h>
#include <generated/types/energy.hpp>
#include <utils/date.hpp>

namespace module {

/// \brief An entry gives no energy when one of its limits is zero.
inline bool schedule_entry_without_energy(const types::energy::ScheduleResEntry& entry) {
    const auto is_zero = [](const std::optional<types::energy::NumberWithSource>& limit) {
        return limit.has_value() and std::fabs(limit.value().value) < 0.001f;
    };
    return is_zero(entry.limits_to_root.total_power_W) or is_zero(entry.limits_to_root.ac_max_current_A);
}

/// \brief How long \p schedule shows no energy from \p now on: up to its next entry with energy, or up to its last
///        entry if no later one has energy. Empty if there is energy at \p now or the schedule does not look ahead.
inline std::optional<std::chrono::seconds>
schedule_no_energy_duration(const std::vector<types::energy::ScheduleResEntry>& schedule,
                            date::utc_clock::time_point now) {
    if (schedule.empty()) {
        return std::nullopt;
    }

    // The first entry is active already, even if its timestamp is in the future
    std::size_t active = 0;
    for (std::size_t i = 1; i < schedule.size(); ++i) {
        if (Everest::Date::from_rfc3339(schedule[i].timestamp) > now) {
            break;
        }
        active = i;
    }

    if (not schedule_entry_without_energy(schedule[active])) {
        return std::nullopt;
    }

    std::size_t until = active;
    for (std::size_t i = active + 1; i < schedule.size(); ++i) {
        until = i;
        if (not schedule_entry_without_energy(schedule[i])) {
            break;
        }
    }

    if (until == active) {
        return std::nullopt;
    }

    return std::chrono::duration_cast<std::chrono::seconds>(Everest::Date::from_rfc3339(schedule[until].timestamp) -
                                                            now);
}

} // namespace module
