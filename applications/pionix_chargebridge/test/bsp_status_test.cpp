// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

// The wire layout of the BSP status packet, which this round moved every field of.
//
// SafetyErrorFlags went from 8 bytes to 4 (its bitfields were 33 bits wide against a 16-bit
// reserved, so the union rounded up and the top bit was unreachable through `raw`). Trimming it to
// exactly 32 shifted every field after error_flags down by four, and ce_state/id_state were appended
// at the same time. Both sides of the mirror moved together, which is precisely the situation where
// nothing on the wire can catch a mistake: the packet stayed a valid packet, it just means something
// different. So the layout is asserted from the outside, against hand-built buffers with a distinct
// value in every field - a field read from the wrong offset cannot then pass.
//
// KNOWN GAP. The consumers of this packet (bsp_bridge, evse_bsp_api, ev_bsp_api) need a UDP socket,
// an MQTT broker and an EVerest peer, so none of them is reachable from here. Verified by hand
// instead, against a simulated ChargeBridge plus a broker in a user+network namespace, and NOT
// regression-protected:
//
//   - a 19-byte frame is accepted and a 21-byte one (the previous layout) is rejected outright by
//     the exact-size gate, leaving the bridge unavailable;
//   - ce_fault and id_fault reach the error path and are named there, together and separately;
//   - PP-derived ampacity and PP faults are published on a CCS board and silent on an MCS one.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <protocol/evse_bsp_cb_to_host.h>
#include <protocol/evse_bsp_host_to_cb.h>
#include <vector>

namespace {

using bsp_status = struct evse_bsp_cb_to_host;

void append_le16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void append_le32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFF));
    }
}

TEST(bsp_status, the_wire_layout_is_frozen) {
    EXPECT_EQ(sizeof(SafetyErrorFlags), 4u);
    EXPECT_EQ(sizeof(bsp_status), 19u);
    // Every offset, so a field moving is a failure here rather than a misread value in the field.
    EXPECT_EQ(offsetof(bsp_status, reset_reason), 0u);
    EXPECT_EQ(offsetof(bsp_status, cp_state), 1u);
    EXPECT_EQ(offsetof(bsp_status, relay_state), 2u);
    EXPECT_EQ(offsetof(bsp_status, error_flags), 3u);
    EXPECT_EQ(offsetof(bsp_status, pp_state_type1), 7u);
    EXPECT_EQ(offsetof(bsp_status, pp_state_type2), 8u);
    EXPECT_EQ(offsetof(bsp_status, lock_state), 9u);
    EXPECT_EQ(offsetof(bsp_status, hv_mV), 10u);
    EXPECT_EQ(offsetof(bsp_status, stop_charging), 14u);
    EXPECT_EQ(offsetof(bsp_status, cp_duty_cycle), 15u);
    EXPECT_EQ(offsetof(bsp_status, ce_state), 17u);
    EXPECT_EQ(offsetof(bsp_status, id_state), 18u);
    // The host -> MCU direction was not touched.
    EXPECT_EQ(sizeof(struct evse_bsp_host_to_cb), 19u);
}

TEST(bsp_status, the_new_fault_bits_sit_where_the_mcu_puts_them) {
    // ce_fault and id_fault are the two named bits this round added. They were already visible
    // through the old union's first word, so their positions did NOT move - which is exactly why a
    // test has to state them: nothing else would notice if they did.
    SafetyErrorFlags flags{};
    flags.flags.ce_fault = 1;
    EXPECT_EQ(flags.raw, 1u << 17);
    flags.raw = 0;
    flags.flags.id_fault = 1;
    EXPECT_EQ(flags.raw, 1u << 18);

    // raw covers the whole union now: setting every named bit plus the reserved field must not
    // leave anything outside 32 bits, which is what the old 8-byte layout got wrong.
    flags.raw = 0xFFFFFFFFu;
    EXPECT_EQ(flags.flags.ce_fault, 1u);
    EXPECT_EQ(flags.flags.id_fault, 1u);
    EXPECT_EQ(flags.flags.reserved, (1u << 13) - 1u);
}

TEST(bsp_status, the_first_named_flag_bits_are_unchanged) {
    // The bits that existed before the trim must not have shifted: the MCU's meaning for them is
    // unchanged, so a renumbering here would silently remap live faults.
    SafetyErrorFlags flags{};
    flags.flags.cp_not_state_c = 1;
    EXPECT_EQ(flags.raw, 1u << 0);
    flags.raw = 0;
    flags.flags.rcd_error = 1;
    EXPECT_EQ(flags.raw, 1u << 16);
}

TEST(bsp_status, the_id_state_values_are_frozen) {
    // Mated_EV was called Mated before this round. The numbering matters more than the name: the MCU
    // and the host agree by value, and three EV-only values were appended after Invalid rather than
    // inserted, so nothing existing renumbered.
    EXPECT_EQ(IdState_NotApplicable, 0);
    EXPECT_EQ(IdState_Unmated, 1);
    EXPECT_EQ(IdState_Mated_EV, 2);
    EXPECT_EQ(IdState_Mated_EVAux, 3);
    EXPECT_EQ(IdState_Mated_EVSEAux, 4);
    EXPECT_EQ(IdState_Invalid, 5);
    EXPECT_EQ(IdState_Inlet_Not_Present, 6);
    EXPECT_EQ(IdState_Mated_NoEVSE, 7);
    EXPECT_EQ(IdState_Mated_EVSE, 8);
    EXPECT_EQ(sizeof(IdState), 1u);
}

TEST(bsp_status, the_ce_state_values_are_frozen) {
    EXPECT_EQ(CeState_NotApplicable, 0);
    EXPECT_EQ(CeState_A, 1);
    EXPECT_EQ(CeState_B0, 2);
    EXPECT_EQ(CeState_B0_Aux, 3);
    EXPECT_EQ(CeState_B, 4);
    EXPECT_EQ(CeState_B_Aux, 5);
    EXPECT_EQ(CeState_C, 6);
    EXPECT_EQ(CeState_C_Aux, 7);
    EXPECT_EQ(CeState_EC, 8);
    EXPECT_EQ(CeState_EC_Aux, 9);
    EXPECT_EQ(CeState_E, 10);
    EXPECT_EQ(CeState_Invalid, 11);
    EXPECT_EQ(sizeof(CeState), 1u);
}

TEST(bsp_status, lock_state_keeps_its_values_after_moving_headers) {
    // LockState moved to cb_common.h so the secure partition can use it without including a wire
    // header. The values are the contract, and they did not move with it.
    EXPECT_EQ(LockState_UNDEFINED, 0);
    EXPECT_EQ(LockState_UNLOCKED, 1);
    EXPECT_EQ(LockState_LOCKED, 2);
}

TEST(bsp_status, every_field_is_read_from_the_offset_the_mcu_wrote_it_to) {
    // The field-shift check. A distinct value per field, built byte by byte in declaration order,
    // so reading any field from a stale offset produces a wrong value rather than a lucky match.
    std::vector<std::uint8_t> buffer;
    buffer.push_back(ResetReason_WATCHDOG);          // reset_reason  @0
    buffer.push_back(CpState_C);                     // cp_state      @1
    buffer.push_back(RelayState_Closed);             // relay_state   @2
    append_le32(buffer, (1u << 17) | (1u << 18));    // error_flags   @3  ce_fault + id_fault
    buffer.push_back(PpState_Type1_STATE_Connected); // pp_state_type1@7
    buffer.push_back(PpState_Type2_STATE_32A);       // pp_state_type2@8
    buffer.push_back(LockState_LOCKED);              // lock_state    @9
    append_le32(buffer, 850000);                     // hv_mV         @10
    buffer.push_back(1);                             // stop_charging @14
    append_le16(buffer, 970);                        // cp_duty_cycle @15
    buffer.push_back(CeState_C);                     // ce_state      @17
    buffer.push_back(IdState_Mated_EV);              // id_state      @18

    ASSERT_EQ(buffer.size(), sizeof(bsp_status));

    bsp_status status{};
    std::memcpy(&status, buffer.data(), sizeof(status));

    EXPECT_EQ(status.reset_reason, ResetReason_WATCHDOG);
    EXPECT_EQ(status.cp_state, CpState_C);
    EXPECT_EQ(status.relay_state, RelayState_Closed);
    EXPECT_EQ(status.error_flags.flags.ce_fault, 1u);
    EXPECT_EQ(status.error_flags.flags.id_fault, 1u);
    EXPECT_EQ(status.error_flags.flags.cp_not_state_c, 0u);
    EXPECT_EQ(status.pp_state_type1, PpState_Type1_STATE_Connected);
    EXPECT_EQ(status.pp_state_type2, PpState_Type2_STATE_32A);
    EXPECT_EQ(status.lock_state, LockState_LOCKED);
    EXPECT_EQ(status.hv_mV, 850000u);
    EXPECT_EQ(status.stop_charging, 1);
    EXPECT_EQ(status.cp_duty_cycle, 970);
    EXPECT_EQ(status.ce_state, CeState_C);
    EXPECT_EQ(status.id_state, IdState_Mated_EV);
}

TEST(bsp_status, a_ccs_board_reports_the_mcs_fields_as_not_applicable) {
    // What a CCS board puts in the two new bytes. An all-zero tail is the CCS case, so a host that
    // forgets to check the board class sees "no MCS signalling" rather than a bogus CE state.
    bsp_status status{};
    EXPECT_EQ(status.ce_state, CeState_NotApplicable);
    EXPECT_EQ(status.id_state, IdState_NotApplicable);
    EXPECT_EQ(status.error_flags.flags.ce_fault, 0u);
    EXPECT_EQ(status.error_flags.flags.id_fault, 0u);
}

} // namespace
