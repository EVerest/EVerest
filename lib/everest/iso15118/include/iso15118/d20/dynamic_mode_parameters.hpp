// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

namespace iso15118::d20 {

struct UpdateDynamicModeParameters {
    // NOTE: departure time needs to be in SECC time [V2G20-1529]:
    // epoch seconds since UNIX time (UTC)
    std::optional<std::uint64_t> departure_time;
    std::optional<std::uint8_t> target_soc;
    std::optional<std::uint8_t> min_soc;
};

} // namespace iso15118::d20