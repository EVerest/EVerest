// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

// The wire layout of the heartbeat reply, which now carries the link status as its last member.
//
// Host and MCU always ship together, so there is no version negotiation to catch a layout drift:
// the mirrored protocol headers are the only contract, and a field silently moving would be read as
// garbage. These tests pin the layout down from the outside - against a hand-built byte buffer, not
// against the struct that produced it - so a change on either side of the mirror fails here.
//
// KNOWN GAP - what this suite deliberately does not reach. heartbeat_service owns a udp_client and an
// event_fd, so nothing in handle_heartbeat_reply() can be exercised without a socket and an event
// loop, which is why the wire layout is asserted here and the behaviour around it is not. The
// following were verified by hand against a simulated ChargeBridge in a user+network namespace
// (unshare -Ur --net, a python UDP responder on the management port) and are NOT regression-protected:
//
//   - a reply one byte short is rejected by the exact-size check and never accepted as a heartbeat;
//   - the role mismatch is logged once per MCU boot, not once per reply;
//   - a reboot (uptime regression) re-arms that report;
//   - retargeting to another endpoint re-arms it too, and a different device's lower uptime is not
//     mistaken for a reboot;
//   - the remedy wording follows the reported link technology.
//
// Reproducing them needs that harness. If this ever gets a socket-capable test fixture, they are the
// cases to move in first.

#include <array>
#include <charge_bridge/cb_role.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <protocol/cb_management.h>
#include <utility>
#include <vector>

namespace {

using charge_bridge::cb_type_ev;

using wire_reply = CbManagementPacket<CbHeartbeatReplyPacket>;

// Every value CbStructType defines, with the wire number it must keep. These numbers are a frozen
// contract with the MCU, so the list is written out rather than derived: renumbering one, or
// repurposing an id a retired message used to hold, has to fail here.
constexpr std::array<std::pair<CbStructType, std::uint16_t>, 14> k_struct_types{{
    {CbStructType::CST_HostToCb_Heartbeat, 1},
    {CbStructType::CST_CbToHost_Heartbeat, 2},
    {CbStructType::CST_HostToCb_Gpio, 3},
    {CbStructType::CST_CbToHost_Io, 4},
    {CbStructType::CST_HostToCb_Ws28, 5},
    {CbStructType::CST_HostToCb_Ws28Anim, 6},
    {CbStructType::CST_CbToHost_DebugUart, 7},
    {CbStructType::CST_CbFirmwareReply, 0xFFF9},
    {CbStructType::CST_CbFirmwareStart, 0xFFFA},
    {CbStructType::CST_CbFirmwarePacket, 0xFFFB},
    {CbStructType::CST_CbFirmwareFinish, 0xFFFC},
    {CbStructType::CST_CbFirmwareUpdateCancel, 0xFFFD},
    {CbStructType::CST_CbFirmwarePing, 0xFFFE},
    {CbStructType::CST_CbFirmwareGetVersion, 0xFFFF},
}};

// Deliberately without a default arm: an enumerator added to CbStructType without being added to
// k_struct_types above makes this switch non-exhaustive, which -Wswitch reports and this target
// promotes to an error. That is the tripwire the list itself cannot provide - a plain list of values
// can be checked for renumbering, but nothing in it notices a brand new id appearing.
constexpr bool is_listed(CbStructType type) {
    switch (type) {
    case CbStructType::CST_HostToCb_Heartbeat:
    case CbStructType::CST_CbToHost_Heartbeat:
    case CbStructType::CST_HostToCb_Gpio:
    case CbStructType::CST_CbToHost_Io:
    case CbStructType::CST_HostToCb_Ws28:
    case CbStructType::CST_HostToCb_Ws28Anim:
    case CbStructType::CST_CbToHost_DebugUart:
    case CbStructType::CST_CbFirmwareReply:
    case CbStructType::CST_CbFirmwareStart:
    case CbStructType::CST_CbFirmwarePacket:
    case CbStructType::CST_CbFirmwareFinish:
    case CbStructType::CST_CbFirmwareUpdateCancel:
    case CbStructType::CST_CbFirmwarePing:
    case CbStructType::CST_CbFirmwareGetVersion:
        return true;
    }
    return false;
}

// Both the MCU and every supported host are little-endian, and the structs are packed, so the wire
// image is simply the fields in declaration order with no padding. The helpers below spell that out
// explicitly instead of memcpy-ing a struct, which would test nothing.
void append_le16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void append_le32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
}

TEST(heartbeat_reply, the_wire_layout_is_frozen) {
    EXPECT_EQ(sizeof(CbLinkStatusPacket), 8u);
    EXPECT_EQ(sizeof(CbHeartbeatReplyPacket), 71u);
    // After the 53 bytes of measurements come the 9 bytes of session ownership (cb-session-v1:
    // session_status, owner_session_id, owner_ip_v4), then the link status, then the latched role.
    EXPECT_EQ(offsetof(CbHeartbeatReplyPacket, session_status), 53u);
    EXPECT_EQ(offsetof(CbHeartbeatReplyPacket, owner_session_id), 54u);
    EXPECT_EQ(offsetof(CbHeartbeatReplyPacket, owner_ip_v4), 58u);
    EXPECT_EQ(offsetof(CbHeartbeatReplyPacket, link_status), 62u);
    EXPECT_EQ(offsetof(CbHeartbeatReplyPacket, latched_cb_type), 70u);
    // 2 byte CbStructType + the payload: what a datagram has to measure to be accepted.
    EXPECT_EQ(sizeof(wire_reply), 73u);
}

TEST(heartbeat_reply, the_config_layout_is_frozen) {
    // The config grows by station_id and cb_type as its last fields, which is what the version bump
    // announces (together with the cb-session-v1 heartbeat trailer, see below).
    EXPECT_EQ(CB_CONFIG_VERSION, 6);
    EXPECT_EQ(sizeof(CbConfig), 226u);
    EXPECT_EQ(offsetof(CbConfig, station_id), 224u);
    EXPECT_EQ(offsetof(CbConfig, cb_type), 225u);
    // The heartbeat is the config plus session_id (4) and session_flags (1).
    EXPECT_EQ(offsetof(CbHeartbeatPacket, session_id), 226u);
    EXPECT_EQ(offsetof(CbHeartbeatPacket, session_flags), 230u);
    EXPECT_EQ(sizeof(CbHeartbeatPacket), 231u);
}

TEST(heartbeat_reply, the_message_ids_are_frozen) {
    for (auto const& [type, expected] : k_struct_types) {
        EXPECT_EQ(static_cast<std::uint16_t>(type), expected);
        EXPECT_TRUE(is_listed(type));
    }
}

TEST(heartbeat_reply, no_message_claims_the_retired_link_status_id) {
    // The link status is not a message of its own any more, so id 8 is free. Repurposing it is a
    // decision to make deliberately, not to discover in the field: an MCU built against a header that
    // still sends 8 would silently feed whatever new message claimed it.
    for (auto const& [type, value] : k_struct_types) {
        EXPECT_NE(value, 8u) << "id 8 is retired, but an enumerator claims it";
        (void)type;
    }
}

TEST(heartbeat_reply, the_embedded_link_status_is_read_from_the_right_offset) {
    // A full reply built byte by byte, with a distinct value in every field so a misread offset
    // cannot accidentally pass.
    std::vector<std::uint8_t> buffer;
    append_le16(buffer, static_cast<std::uint16_t>(CbStructType::CST_CbToHost_Heartbeat));
    append_le32(buffer, 1001);                               // cp_hi_mV
    append_le32(buffer, static_cast<std::uint32_t>(-1002));  // cp_lo_mV
    append_le32(buffer, 1003);                               // vdd_core
    append_le32(buffer, 3300);                               // vdd_3v3
    append_le32(buffer, 1005);                               // vdd_refint
    append_le32(buffer, 12000);                              // vdd_12V
    append_le32(buffer, static_cast<std::uint32_t>(-12000)); // vdd_N12V
    append_le32(buffer, 1008);                               // pp_mOhm
    append_le32(buffer, 1009);                               // pp_voltage_mV
    buffer.push_back(1);                                     // relay_state_feedback[0]
    buffer.push_back(0);                                     // relay_state_feedback[1]
    buffer.push_back(1);                                     // relay_state_feedback[2]
    append_le16(buffer, 42);                                 // temperature_mcu_C
    append_le16(buffer, 43);                                 // temperature_pcb_C
    append_le16(buffer, 44);                                 // temperature_modem_C
    append_le16(buffer, 45);                                 // temperature_PT1000_C[0]
    append_le16(buffer, 46);                                 // temperature_PT1000_C[1]
    append_le32(buffer, 123456);                             // uptime_ms
    buffer.push_back(0);                                     // session_status (CBSS_Accepted)
    append_le32(buffer, 0xA5A5F00D);                         // owner_session_id
    append_le32(buffer, 0xC0A80101);                         // owner_ip_v4 (192.168.1.1)
    // CbLinkStatusPacket
    buffer.push_back(CB_LINK_TECH_SPE); // technology
    buffer.push_back(1);                // phy_operational
    buffer.push_back(1);                // plca_engaged
    buffer.push_back(0);                // reserved
    append_le32(buffer, 7);             // transition_count
    buffer.push_back(cb_type_ev);       // latched_cb_type

    ASSERT_EQ(buffer.size(), sizeof(wire_reply));

    wire_reply reply{};
    std::memcpy(&reply, buffer.data(), sizeof(reply));

    EXPECT_EQ(reply.type, CbStructType::CST_CbToHost_Heartbeat);
    // A couple of measurement fields, so a shift anywhere before the link status is caught too.
    EXPECT_EQ(reply.data.cp_hi_mV, 1001);
    EXPECT_EQ(reply.data.cp_lo_mV, -1002);
    EXPECT_EQ(reply.data.vdd_N12V, -12000);
    EXPECT_EQ(reply.data.temperature_mcu_C, 42);
    EXPECT_EQ(reply.data.temperature_PT1000_C[1], 46);
    EXPECT_EQ(reply.data.uptime_ms, 123456);
    // The session-ownership trailer that sits between the measurements and the link status.
    EXPECT_EQ(reply.data.session_status, static_cast<uint8_t>(CbSessionStatus::CBSS_Accepted));
    EXPECT_EQ(reply.data.owner_session_id, 0xA5A5F00Du);
    EXPECT_EQ(reply.data.owner_ip_v4, 0xC0A80101u);
    // The payload this feature exists for.
    EXPECT_EQ(reply.data.link_status.technology, CB_LINK_TECH_SPE);
    EXPECT_EQ(reply.data.link_status.phy_operational, 1);
    EXPECT_EQ(reply.data.link_status.plca_engaged, 1);
    EXPECT_EQ(reply.data.link_status.reserved, 0);
    EXPECT_EQ(reply.data.link_status.transition_count, 7u);
    // The role the MCU latched, read from behind the link status.
    EXPECT_EQ(reply.data.latched_cb_type, cb_type_ev);
}

TEST(heartbeat_reply, a_plc_board_reports_its_technology_with_zero_flags) {
    // What every HomePlug board now sends with every reply. The bridge must not derive a carrier from
    // it; these are the bytes its fail-open branch keys on.
    CbLinkStatusPacket status{};
    status.technology = CB_LINK_TECH_PLC;
    EXPECT_EQ(status.phy_operational, 0);
    EXPECT_EQ(status.plca_engaged, 0);
    EXPECT_EQ(status.transition_count, 0u);
}

TEST(heartbeat_reply, a_default_constructed_link_status_is_the_synthesized_reboot_report) {
    // The uptime-regression path publishes this. All-zero means technology UNKNOWN, which the
    // bridge's technology latch deliberately ignores, and cleared flags, which it does apply.
    CbLinkStatusPacket status{};
    EXPECT_EQ(status.technology, CB_LINK_TECH_UNKNOWN);
    EXPECT_EQ(status.phy_operational, 0);
    EXPECT_EQ(status.plca_engaged, 0);
    EXPECT_EQ(status.transition_count, 0u);
}

} // namespace
