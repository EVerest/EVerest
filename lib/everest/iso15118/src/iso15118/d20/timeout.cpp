// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/timeout.hpp>

#include <algorithm>
#include <utility>

#include <iso15118/detail/helper.hpp>

namespace iso15118::d20 {

void Timeouts::start_timeout(TimeoutType type, uint32_t timeout_ms) {
    const auto type_u8 = to_underlying_value(type);
    if (timeouts.at(type_u8).has_value()) {
        logf_warning("Timeout %u already started", type_u8);
        return;
    }
    timeouts.at(type_u8).emplace(Timeout(timeout_ms));
}

void Timeouts::stop_timeout(TimeoutType type) {
    const auto type_u8 = to_underlying_value(type);
    if (not timeouts.at(type_u8).has_value()) {
        logf_warning("Timeout %u is not started", type_u8);
    }
    timeouts.at(type_u8).reset();
}

void Timeouts::reset_timeout(TimeoutType type) {
    const auto type_u8 = to_underlying_value(type);
    timeouts.at(type_u8).reset();
}

std::vector<TimeoutType> Timeouts::check() const {
    std::vector<std::pair<TimePoint, TimeoutType>> reached;
    reached.reserve(TIMEOUT_TYPE_SIZE);

    for (uint8_t i = 0; i < TIMEOUT_TYPE_SIZE; i++) {
        const auto& timeout = timeouts.at(i);
        if (timeout.has_value() and timeout->is_reached()) {
            reached.emplace_back(timeout->get_timeout_point(), static_cast<TimeoutType>(i));
        }
    }

    std::stable_sort(reached.begin(), reached.end(),
                     [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    std::vector<TimeoutType> result;
    result.reserve(reached.size());
    for (const auto& [_, type] : reached) {
        result.push_back(type);
    }
    return result;
}

} // namespace iso15118::d20
