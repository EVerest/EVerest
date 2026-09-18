// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

// The MCS basic-signalling helpers: the safety-flag bit positions, the board-class gate, and the
// diagnostics spellings.
//
// The bit positions matter most. They used to be copy-pasted into evse_bsp_api.cpp and
// ev_bsp_api.cpp, and both copies stopped at bit 16 while the wire header grew bits 17 and 18 - so a
// CE or ID emergency reached neither interface, and the EV side printed "Relays can be switched on."
// because its log line came out empty. A single definition fixes the drift; these tests keep it
// pinned to the wire header rather than to itself.

#include <charge_bridge/mcs_bsp.hpp>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>

namespace {

using charge_bridge::ce_state_name;
using charge_bridge::id_state_name;
using charge_bridge::is_mcs_technology;
using charge_bridge::lock_state_name;
using charge_bridge::safety_error_mask;
using charge_bridge::to_bit;

// Set one named bitfield, read the raw word: that is the MCU's own encoding, so comparing the mask
// against it proves the mask agrees with the wire rather than with a copy of itself.
std::uint32_t raw_of_ce_fault() {
    SafetyErrorFlags f{};
    f.flags.ce_fault = 1;
    return f.raw;
}

std::uint32_t raw_of_id_fault() {
    SafetyErrorFlags f{};
    f.flags.id_fault = 1;
    return f.raw;
}

TEST(mcs_bsp, the_mcs_fault_masks_match_the_wire_bitfields) {
    EXPECT_EQ(to_bit(safety_error_mask::ce_fault), raw_of_ce_fault());
    EXPECT_EQ(to_bit(safety_error_mask::id_fault), raw_of_id_fault());
    // And they are the two bits the mask set was missing.
    EXPECT_EQ(to_bit(safety_error_mask::ce_fault), 1u << 17);
    EXPECT_EQ(to_bit(safety_error_mask::id_fault), 1u << 18);
}

TEST(mcs_bsp, every_mask_matches_its_wire_bitfield) {
    // Each entry sets exactly one named bitfield and compares. A mask that drifted from the header -
    // the failure this file exists to prevent - shows up here as a mismatch on that one line.
    auto check = [](safety_error_mask mask, std::uint32_t raw, char const* name) {
        EXPECT_EQ(to_bit(mask), raw) << name;
    };
#define CHECK_MASK(field)                                                                                              \
    do {                                                                                                               \
        SafetyErrorFlags f{};                                                                                          \
        f.flags.field = 1;                                                                                             \
        check(safety_error_mask::field, f.raw, #field);                                                                \
    } while (false)

    CHECK_MASK(cp_not_state_c);
    CHECK_MASK(pwm_not_enabled);
    CHECK_MASK(pp_invalid);
    CHECK_MASK(plug_temperature_too_high);
    CHECK_MASK(internal_temperature_too_high);
    CHECK_MASK(emergency_input_latched);
    CHECK_MASK(relay_health_latched);
    CHECK_MASK(vdd_3v3_out_of_range);
    CHECK_MASK(vdd_core_out_of_range);
    CHECK_MASK(vdd_12V_out_of_range);
    CHECK_MASK(vdd_N12V_out_of_range);
    CHECK_MASK(vdd_refint_out_of_range);
    CHECK_MASK(external_allow_power_on);
    CHECK_MASK(config_mem_error);
    CHECK_MASK(dc_hv_ov_emergency);
    CHECK_MASK(rcd_error);
    CHECK_MASK(ce_fault);
    CHECK_MASK(id_fault);
#undef CHECK_MASK
}

TEST(mcs_bsp, no_two_masks_share_a_bit) {
    constexpr safety_error_mask all[] = {
        safety_error_mask::cp_not_state_c,
        safety_error_mask::pwm_not_enabled,
        safety_error_mask::pp_invalid,
        safety_error_mask::plug_temperature_too_high,
        safety_error_mask::internal_temperature_too_high,
        safety_error_mask::emergency_input_latched,
        safety_error_mask::relay_health_latched,
        safety_error_mask::vdd_3v3_out_of_range,
        safety_error_mask::vdd_core_out_of_range,
        safety_error_mask::vdd_12V_out_of_range,
        safety_error_mask::vdd_N12V_out_of_range,
        safety_error_mask::vdd_refint_out_of_range,
        safety_error_mask::external_allow_power_on,
        safety_error_mask::config_mem_error,
        safety_error_mask::dc_hv_ov_emergency,
        safety_error_mask::rcd_error,
        safety_error_mask::ce_fault,
        safety_error_mask::id_fault,
    };
    std::uint32_t seen = 0;
    for (auto mask : all) {
        auto const bit = to_bit(mask);
        // Exactly one bit each, and never a bit another mask already claimed.
        EXPECT_NE(bit, 0u);
        EXPECT_EQ(bit & (bit - 1), 0u) << "mask covers more than one bit: " << bit;
        EXPECT_EQ(seen & bit, 0u) << "two masks share bit " << bit;
        seen |= bit;
    }
    // Bit 15 (dc_hv_ov_error) is deliberately absent - the OVM interface owns both HV bits.
    EXPECT_EQ(seen & (1u << 15), 0u);
}

// --- board class -----------------------------------------------------------------------------

TEST(mcs_bsp, only_the_spe_technology_is_an_mcs_board) {
    EXPECT_TRUE(is_mcs_technology(CB_LINK_TECH_SPE));
    EXPECT_FALSE(is_mcs_technology(CB_LINK_TECH_PLC));
    // Unknown must read as "not MCS": that is the pre-MCS behaviour, so a board whose heartbeat has
    // not answered yet keeps publishing PP exactly as it always did instead of going silent.
    EXPECT_FALSE(is_mcs_technology(CB_LINK_TECH_UNKNOWN));
    EXPECT_FALSE(is_mcs_technology(200));
}

// --- the board-class latch ---------------------------------------------------------------------

TEST(mcs_bsp, an_unlatched_technology_reads_as_not_mcs) {
    charge_bridge::technology_latch latch;
    EXPECT_FALSE(latch.latched());
    EXPECT_EQ(latch.value(), CB_LINK_TECH_UNKNOWN);
    // Which is what keeps a board whose heartbeat has not answered yet behaving as it always did.
    EXPECT_FALSE(is_mcs_technology(latch.value()));
}

TEST(mcs_bsp, unknown_never_latches_anything) {
    charge_bridge::technology_latch latch;
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(latch.offer(CB_LINK_TECH_UNKNOWN), charge_bridge::technology_latch::result::ignored);
        EXPECT_FALSE(latch.latched());
    }
    // A real value still lands afterwards.
    EXPECT_EQ(latch.offer(CB_LINK_TECH_SPE), charge_bridge::technology_latch::result::latched);
    EXPECT_TRUE(is_mcs_technology(latch.value()));
}

TEST(mcs_bsp, the_first_real_value_wins_and_a_later_unknown_cannot_clear_it) {
    charge_bridge::technology_latch latch;
    EXPECT_EQ(latch.offer(CB_LINK_TECH_SPE), charge_bridge::technology_latch::result::latched);
    // The reboot path synthesizes an all-zero link status; that must not un-latch the board class.
    EXPECT_EQ(latch.offer(CB_LINK_TECH_UNKNOWN), charge_bridge::technology_latch::result::ignored);
    EXPECT_EQ(latch.value(), CB_LINK_TECH_SPE);
    EXPECT_TRUE(is_mcs_technology(latch.value()));
}

TEST(mcs_bsp, repeating_the_latched_value_is_not_news) {
    charge_bridge::technology_latch latch;
    ASSERT_EQ(latch.offer(CB_LINK_TECH_PLC), charge_bridge::technology_latch::result::latched);
    // Every heartbeat repeats it; only the first one may be reported.
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(latch.offer(CB_LINK_TECH_PLC), charge_bridge::technology_latch::result::ignored);
    }
    EXPECT_EQ(latch.value(), CB_LINK_TECH_PLC);
}

TEST(mcs_bsp, a_disagreeing_value_is_refused_and_reported_exactly_once) {
    // The property that matters: a swapped board, or one garbled heartbeat, must not be able to move
    // the class from SPE back to PLC and re-enable PP publication on an MCS connector.
    charge_bridge::technology_latch latch;
    ASSERT_EQ(latch.offer(CB_LINK_TECH_SPE), charge_bridge::technology_latch::result::latched);

    EXPECT_EQ(latch.offer(CB_LINK_TECH_PLC), charge_bridge::technology_latch::result::conflict);
    EXPECT_EQ(latch.value(), CB_LINK_TECH_SPE) << "the latched class must stand";
    EXPECT_TRUE(is_mcs_technology(latch.value()));

    // Reported once, then silent - the disagreement repeats on every heartbeat.
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(latch.offer(CB_LINK_TECH_PLC), charge_bridge::technology_latch::result::ignored);
        EXPECT_EQ(latch.value(), CB_LINK_TECH_SPE);
    }
}

TEST(mcs_bsp, the_conflict_holds_in_the_other_direction_too) {
    charge_bridge::technology_latch latch;
    ASSERT_EQ(latch.offer(CB_LINK_TECH_PLC), charge_bridge::technology_latch::result::latched);
    EXPECT_EQ(latch.offer(CB_LINK_TECH_SPE), charge_bridge::technology_latch::result::conflict);
    // A CCS board does not become an MCS board either: PP keeps being published.
    EXPECT_EQ(latch.value(), CB_LINK_TECH_PLC);
    EXPECT_FALSE(is_mcs_technology(latch.value()));
}

TEST(mcs_bsp, a_retarget_re_opens_the_latch) {
    // A new endpoint may legitimately be a different board, so reset() has to make the class
    // learnable again - and re-arm the once-only report with it.
    charge_bridge::technology_latch latch;
    ASSERT_EQ(latch.offer(CB_LINK_TECH_SPE), charge_bridge::technology_latch::result::latched);
    ASSERT_EQ(latch.offer(CB_LINK_TECH_PLC), charge_bridge::technology_latch::result::conflict);

    latch.reset();
    EXPECT_FALSE(latch.latched());
    EXPECT_EQ(latch.value(), CB_LINK_TECH_UNKNOWN);

    // The board the new endpoint reports is adopted, whichever it is...
    EXPECT_EQ(latch.offer(CB_LINK_TECH_PLC), charge_bridge::technology_latch::result::latched);
    EXPECT_FALSE(is_mcs_technology(latch.value()));
    // ...and a disagreement on the new endpoint is reported again rather than swallowed.
    EXPECT_EQ(latch.offer(CB_LINK_TECH_SPE), charge_bridge::technology_latch::result::conflict);
}

// --- diagnostics spellings -------------------------------------------------------------------

TEST(mcs_bsp, every_ce_state_has_a_name) {
    EXPECT_STREQ(ce_state_name(CeState_NotApplicable), "n/a");
    EXPECT_STREQ(ce_state_name(CeState_A), "A");
    EXPECT_STREQ(ce_state_name(CeState_B0), "B0");
    EXPECT_STREQ(ce_state_name(CeState_B0_Aux), "B0_Aux");
    EXPECT_STREQ(ce_state_name(CeState_B), "B");
    EXPECT_STREQ(ce_state_name(CeState_B_Aux), "B_Aux");
    EXPECT_STREQ(ce_state_name(CeState_C), "C");
    EXPECT_STREQ(ce_state_name(CeState_C_Aux), "C_Aux");
    EXPECT_STREQ(ce_state_name(CeState_EC), "EC");
    EXPECT_STREQ(ce_state_name(CeState_EC_Aux), "EC_Aux");
    EXPECT_STREQ(ce_state_name(CeState_E), "E");
    EXPECT_STREQ(ce_state_name(CeState_Invalid), "Invalid");
    // A value the enum does not define must not borrow another state's name.
    EXPECT_STREQ(ce_state_name(12), "unknown");
    EXPECT_STREQ(ce_state_name(255), "unknown");
}

TEST(mcs_bsp, every_id_state_has_a_name_including_the_three_ev_view_values) {
    EXPECT_STREQ(id_state_name(IdState_NotApplicable), "n/a");
    EXPECT_STREQ(id_state_name(IdState_Unmated), "Unmated");
    EXPECT_STREQ(id_state_name(IdState_Mated_EV), "Mated_EV");
    EXPECT_STREQ(id_state_name(IdState_Mated_EVAux), "Mated_EVAux");
    EXPECT_STREQ(id_state_name(IdState_Mated_EVSEAux), "Mated_EVSEAux");
    EXPECT_STREQ(id_state_name(IdState_Invalid), "Invalid");
    // The values only an EV can report - it drives the 5 V source itself.
    EXPECT_STREQ(id_state_name(IdState_Inlet_Not_Present), "Inlet_Not_Present");
    EXPECT_STREQ(id_state_name(IdState_Mated_NoEVSE), "Mated_NoEVSE");
    EXPECT_STREQ(id_state_name(IdState_Mated_EVSE), "Mated_EVSE");
    EXPECT_STREQ(id_state_name(9), "unknown");
}

TEST(mcs_bsp, the_id_state_names_are_all_distinct) {
    // Mated_EV and Mated_EVSE differ by two characters and mean opposite things: one is an EVSE
    // seeing a car, the other a car seeing a charger. A collision here would be unreadable.
    constexpr std::uint8_t values[] = {IdState_NotApplicable,     IdState_Unmated,       IdState_Mated_EV,
                                       IdState_Mated_EVAux,       IdState_Mated_EVSEAux, IdState_Invalid,
                                       IdState_Inlet_Not_Present, IdState_Mated_NoEVSE,  IdState_Mated_EVSE};
    for (auto a : values) {
        for (auto b : values) {
            if (a == b) {
                continue;
            }
            EXPECT_STRNE(id_state_name(a), id_state_name(b))
                << "values " << static_cast<int>(a) << " and " << static_cast<int>(b);
        }
    }
}

TEST(mcs_bsp, lock_state_names_keep_undefined_distinct_from_unlocked) {
    // UNDEFINED means "no lock hardware", not "open". Rendering both as the same word would tell an
    // operator the connector is confirmed open when nothing is measuring it.
    EXPECT_STREQ(lock_state_name(LockState_UNDEFINED), "n/a");
    EXPECT_STREQ(lock_state_name(LockState_UNLOCKED), "unlocked");
    EXPECT_STREQ(lock_state_name(LockState_LOCKED), "locked");
    EXPECT_STRNE(lock_state_name(LockState_UNDEFINED), lock_state_name(LockState_UNLOCKED));
    EXPECT_STREQ(lock_state_name(3), "unknown");
}

} // namespace
