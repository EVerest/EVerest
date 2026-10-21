// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstddef>
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
        float power_w{0.0f};                                 // total over all phases
        std::optional<std::array<float, 3>> phase_power_w{}; // L1, L2, L3 if sent per phase [V2G20-1818]

        // [V2G20-1817]: a total without per phase values is spread evenly over the three phases.
        bool exceeds(const Entry& limit) const {
            if (not phase_power_w.has_value() and not limit.phase_power_w.has_value()) {
                return power_w > limit.power_w;
            }
            const auto demand = phases();
            const auto allowed = limit.phases();
            for (std::size_t i = 0; i < demand.size(); ++i) {
                if (demand[i] > allowed[i]) {
                    return true;
                }
            }
            return false;
        }

    private:
        std::array<float, 3> phases() const {
            return phase_power_w.value_or(std::array<float, 3>{power_w / 3, power_w / 3, power_w / 3});
        }
    };

    uint64_t time_anchor_s{0}; // seconds since the epoch
    std::vector<Entry> entries{};

    // The message's TimeAnchor is in microseconds for an EVPowerProfile (Table 103) and milliseconds for a
    // PowerSchedule (Table 112), so the caller converts it.
    template <typename Entries> static PowerTimeline from(uint64_t time_anchor_s, const Entries& entries) {
        PowerTimeline timeline;
        timeline.time_anchor_s = time_anchor_s;
        for (const auto& entry : entries) {
            const auto power_w = message_20::datatypes::from_RationalNumber(entry.power);
            if (not entry.power_l2.has_value() and not entry.power_l3.has_value()) {
                timeline.entries.push_back({entry.duration, power_w, std::nullopt});
                continue;
            }
            const auto phase = [](const auto& power) {
                return power.has_value() ? message_20::datatypes::from_RationalNumber(*power) : 0.0f;
            };
            const std::array<float, 3> phases{power_w, phase(entry.power_l2), phase(entry.power_l3)};
            timeline.entries.push_back({entry.duration, phases[0] + phases[1] + phases[2], phases});
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

    // Whether \p demand over [\p start_s, \p end_s) exceeds the power of any entry it overlaps. Time no entry
    // covers is unconstrained.
    bool exceeded_by(uint64_t start_s, uint64_t end_s, const Entry& demand) const {
        uint64_t slot_start = time_anchor_s;
        for (const auto& entry : entries) {
            const uint64_t slot_end = slot_start + entry.duration_s;
            if (start_s < slot_end and slot_start < end_s and demand.exceeds(entry)) {
                return true;
            }
            slot_start = slot_end;
        }
        return false;
    }
};

} // namespace iso15118::d20
