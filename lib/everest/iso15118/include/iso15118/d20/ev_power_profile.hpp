// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <vector>

#include <iso15118/message/common_types.hpp>

namespace iso15118::d20 {

// The EVPowerProfile of the EV's latest PowerDeliveryReq, reduced to what the SECC needs to tell whether the applied
// entry is 0 kW ([V2G20-1198]).
struct EvPowerProfile {
    struct Entry {
        uint32_t duration_s;
        bool zero_power;
    };

    uint64_t time_anchor_s{0}; // seconds since the epoch
    std::vector<Entry> entries;

    template <typename Entries> static EvPowerProfile from(uint64_t time_anchor, const Entries& entries) {
        EvPowerProfile profile;
        profile.time_anchor_s = time_anchor;
        for (const auto& entry : entries) {
            const auto is_zero = [](const auto& power) {
                return message_20::datatypes::from_RationalNumber(power) == 0;
            };
            const bool zero = is_zero(entry.power) and (not entry.power_l2.has_value() or is_zero(*entry.power_l2)) and
                              (not entry.power_l3.has_value() or is_zero(*entry.power_l3));
            profile.entries.push_back({entry.duration, zero});
        }
        return profile;
    }

    // Whether the entry applied at \p now_s (seconds since the epoch) is 0 kW. False before the anchor and after the
    // last entry: no entry is applied there.
    bool zero_power_at(uint64_t now_s) const {
        if (now_s < time_anchor_s) {
            return false;
        }
        uint64_t start = time_anchor_s;
        for (const auto& entry : entries) {
            if (now_s < start + entry.duration_s) {
                return entry.zero_power;
            }
            start += entry.duration_s;
        }
        return false;
    }
};

} // namespace iso15118::d20
