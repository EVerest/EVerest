// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>

namespace module {

/// \brief Maps the non-negative \p random_value onto a delay below \p max_duration. A \p max_duration of zero or less
///        means no delay.
inline std::chrono::seconds random_delay_duration(std::chrono::seconds max_duration, int random_value) {
    if (max_duration.count() <= 0) {
        return std::chrono::seconds(0);
    }
    return std::chrono::seconds(random_value % max_duration.count());
}

} // namespace module
