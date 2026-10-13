// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <iso15118/message/common_types.hpp>

namespace iso15118::d20 {

constexpr uint64_t MICROSECONDS_PER_SECOND = 1'000'000;
constexpr uint64_t MILLISECONDS_PER_SECOND = 1'000;

// Charge power over consecutive time slots from an anchor. Both the schedule tuples the SECC offers in
// ScheduleExchangeRes and the EVPowerProfile of the EV's PowerDeliveryReq are kept in this form.
struct PowerTimeline {
    struct Entry {
        uint32_t duration_s{0};
        float power_w{0.0f}; // total over all phases
    };

    uint64_t time_anchor_s{0}; // seconds since the epoch
    std::vector<Entry> entries{};

    // The message's TimeAnchor is in microseconds for an EVPowerProfile (Table 103) and milliseconds for a
    // PowerSchedule (Table 112), so the caller converts it.
    template <typename Entries> static PowerTimeline from(uint64_t time_anchor_s, const Entries& entries) {
        PowerTimeline timeline;
        timeline.time_anchor_s = time_anchor_s;
        for (const auto& entry : entries) {
            auto power_w = message_20::datatypes::from_RationalNumber(entry.power);
            if (entry.power_l2.has_value()) {
                power_w += message_20::datatypes::from_RationalNumber(*entry.power_l2);
            }
            if (entry.power_l3.has_value()) {
                power_w += message_20::datatypes::from_RationalNumber(*entry.power_l3);
            }
            timeline.entries.push_back({entry.duration, power_w});
        }
        return timeline;
    }

    // The power of the entry applied at \p now_s. No entry is applied before the anchor or after the last entry.
    std::optional<float> power_at(uint64_t now_s) const {
        if (now_s < time_anchor_s) {
            return std::nullopt;
        }
        uint64_t slot_start = time_anchor_s;
        for (const auto& entry : entries) {
            if (now_s < slot_start + entry.duration_s) {
                return entry.power_w;
            }
            slot_start += entry.duration_s;
        }
        return std::nullopt;
    }

    // Whether a demand of \p power_w over [\p start_s, \p end_s) exceeds the power of any entry it overlaps. Time
    // no entry covers is unconstrained.
    bool exceeded_by(uint64_t start_s, uint64_t end_s, float power_w) const {
        uint64_t slot_start = time_anchor_s;
        for (const auto& entry : entries) {
            const uint64_t slot_end = slot_start + entry.duration_s;
            if (start_s < slot_end and slot_start < end_s and power_w > entry.power_w) {
                return true;
            }
            slot_start = slot_end;
        }
        return false;
    }
};

} // namespace iso15118::d20
