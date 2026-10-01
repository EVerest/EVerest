// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <limits>

namespace charge_bridge::utilities {

// Window around the wrap point in which a decreasing uptime is attributed to the counter wrapping
// rather than to a restart. It has to comfortably exceed the largest plausible gap between two
// observed heartbeat replies - the ChargeBridge can be unreachable for a long time and come back
// without having rebooted - while every millisecond of it is also a window in which a genuine reboot
// is read as a wrap. One hour is orders of magnitude more than any heartbeat gap and a negligible
// slice of the ~49.7 day wrap period.
constexpr std::uint32_t mcu_uptime_wrap_margin_ms = 60u * 60u * 1000u;

// Did the MCU restart between two observed uptime readings?
//
// CbHeartbeatReplyPacket carries uptime_ms as an int32 that the host reads as a uint32, so the value
// counts up monotonically for 2^32 ms (~49.7 days) and then wraps to zero. Two cases must not be
// mistaken for a restart, because a false positive now drops the tap carrier (the reboot path
// synthesizes an all-zero link status) and costs a carrier flap plus ~1 s of duplicate address
// detection in the middle of a healthy session:
//
//   - a duplicated UDP reply, which reports the very same uptime again, and
//   - the counter wrapping past 2^32 ms on a device that has simply been up for 49.7 days.
//
// Everything else that decreases is a restart: an MCU that rebooted cannot report an uptime at or
// above the one already seen.
inline bool mcu_rebooted(std::uint32_t previous_uptime_ms, std::uint32_t current_uptime_ms) {
    if (current_uptime_ms >= previous_uptime_ms) {
        // Ordinary progress, or the identical value from a duplicated reply.
        return false;
    }
    // A decrease. It can only be the counter wrapping when the previous reading sat just below the
    // wrap point and this one sits just above it; any other decrease is a restart.
    const bool could_be_wrap =
        previous_uptime_ms > std::numeric_limits<std::uint32_t>::max() - mcu_uptime_wrap_margin_ms and
        current_uptime_ms < mcu_uptime_wrap_margin_ms;
    return not could_be_wrap;
}

} // namespace charge_bridge::utilities
