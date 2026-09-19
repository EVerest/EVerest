// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

// EXPERIMENTAL - host->board keepalive for the PLC channel.
//
// The ChargeBridge firmware forwards frames from the PLC/SPE wire to the host only while it knows
// the host's UDP endpoint for the PLC channel, and it learns that endpoint solely from frames the
// host sends to it (cb-plc/*/plc_*_state.cpp: host_address/host_port are set in the UDP receive
// action and zeroed whenever the PLC state machine restarts - chip reset after sustained errors,
// terminate, reconfiguration). A SECC never transmits on the tap while idle, so after such a restart
// the board silently drops the EV's SDP multicasts and a woken ISO 15118 session never starts
// (bench 2026-09-16). Until the firmware learns the endpoint from the heartbeat, the host re-teaches
// it by sending a small frame every few seconds. A zero-length datagram would not do: the firmware
// ignores empty payloads when learning (`if (udp_res && len != 0)`), so the frame does go onto the
// wire. It is a broadcast with the IEEE 802 "local experimental" EtherType 0x88B5 (the one the
// bring-up link echo uses) and a fixed magic: every peer's kernel drops it at protocol dispatch.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace charge_bridge {

constexpr std::uint16_t plc_keepalive_ethertype = 0x88B5;
constexpr std::size_t plc_keepalive_frame_size = 60; // minimum Ethernet frame without FCS
constexpr char plc_keepalive_magic[] = "CB-PLC-KEEPALIVE";

using mac_address = std::array<std::uint8_t, 6>;
using plc_keepalive_frame = std::array<std::uint8_t, plc_keepalive_frame_size>;

inline plc_keepalive_frame make_plc_keepalive_frame(mac_address const& source) {
    plc_keepalive_frame frame{};
    std::memset(frame.data(), 0xFF, 6);
    std::memcpy(frame.data() + 6, source.data(), 6);
    frame[12] = static_cast<std::uint8_t>(plc_keepalive_ethertype >> 8);
    frame[13] = static_cast<std::uint8_t>(plc_keepalive_ethertype & 0xFF);
    std::memcpy(frame.data() + 14, plc_keepalive_magic, sizeof(plc_keepalive_magic) - 1);
    return frame;
}

// "aa:bb:cc:dd:ee:ff" -> bytes; trailing whitespace (sysfs ends the line with '\n') is allowed,
// anything else after the sixth byte is a formatting error.
inline bool parse_mac_address(char const* text, mac_address& out) {
    unsigned values[6];
    int consumed = 0;
    if (std::sscanf(text, "%x:%x:%x:%x:%x:%x%n", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5],
                    &consumed) != 6) {
        return false;
    }
    for (char const* rest = text + consumed; *rest != 0; ++rest) {
        if (*rest != ' ' and *rest != '\n' and *rest != '\r' and *rest != '\t') {
            return false;
        }
    }
    for (std::size_t i = 0; i < 6; ++i) {
        if (values[i] > 0xFF) {
            return false;
        }
        out[i] = static_cast<std::uint8_t>(values[i]);
    }
    return true;
}

} // namespace charge_bridge
