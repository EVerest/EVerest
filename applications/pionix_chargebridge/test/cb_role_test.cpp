// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

// charge_bridge.type: the station_id it derives and the cross-check against the role the MCU reports
// it has latched. Both are pure functions, so this needs neither a config file nor a ChargeBridge.

#include <charge_bridge/cb_role.hpp>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>

namespace {

using charge_bridge::cb_role;
using charge_bridge::cb_type_ev;
using charge_bridge::cb_type_evse;
using charge_bridge::cb_type_name;
using charge_bridge::cb_type_not_latched;
using charge_bridge::cb_type_unspecified;
using charge_bridge::decide_station_id;
using charge_bridge::evaluate_role_latch;
using charge_bridge::is_fatal;
using charge_bridge::normalize_cb_type;
using charge_bridge::role_latch_state;
using charge_bridge::role_mismatch_remedy;
using charge_bridge::station_id_issue;
using charge_bridge::to_wire;

// --- wire encoding ---------------------------------------------------------------------------

TEST(cb_role, the_wire_encoding_is_frozen) {
    // These three numbers are the contract with the MCU. EVSE is 0 so a zeroed config means the
    // fail-safe role, and 255 is far away from both so a truncated or uninitialised byte cannot read
    // as a valid role by accident.
    EXPECT_EQ(cb_type_evse, 0u);
    EXPECT_EQ(cb_type_ev, 1u);
    EXPECT_EQ(cb_type_unspecified, 2u);
    EXPECT_EQ(cb_type_not_latched, 255u);
    EXPECT_EQ(to_wire(cb_role::evse), cb_type_evse);
    EXPECT_EQ(to_wire(cb_role::ev), cb_type_ev);
    EXPECT_EQ(to_wire(cb_role::unspecified), cb_type_unspecified);
}

TEST(cb_role, an_unknown_wire_value_is_not_given_a_role) {
    EXPECT_STREQ(cb_type_name(cb_type_evse), "EVSE");
    EXPECT_STREQ(cb_type_name(cb_type_ev), "EV");
    EXPECT_STREQ(cb_type_name(cb_type_unspecified), "not configured");
    EXPECT_STREQ(cb_type_name(cb_type_not_latched), "not yet latched");
    // Anything else must not be mapped onto a role: the host may not invent one it was not told.
    EXPECT_STREQ(cb_type_name(3), "unknown");
    EXPECT_STREQ(cb_type_name(7), "unknown");
    EXPECT_STREQ(cb_type_name(254), "unknown");
}

// --- station_id derivation -------------------------------------------------------------------

TEST(cb_role, an_absent_station_id_comes_from_the_role) {
    auto const evse = decide_station_id(cb_role::evse, std::nullopt);
    EXPECT_EQ(evse.station_id, 0);
    EXPECT_TRUE(evse.derived);
    EXPECT_EQ(evse.issue, station_id_issue::none);

    auto const ev = decide_station_id(cb_role::ev, std::nullopt);
    EXPECT_EQ(ev.station_id, 1);
    EXPECT_TRUE(ev.derived);
    EXPECT_EQ(ev.issue, station_id_issue::none);
}

TEST(cb_role, an_explicit_station_id_is_never_reported_as_derived) {
    for (int value = -1; value <= 7; ++value) {
        EXPECT_FALSE(decide_station_id(cb_role::evse, value).derived) << "value " << value;
        EXPECT_FALSE(decide_station_id(cb_role::ev, value).derived) << "value " << value;
    }
}

TEST(cb_role, collision_detection_is_a_legitimate_choice_for_both_roles) {
    // -1 is an explicit "no PLCA": the operator asked for collision detection, which says nothing
    // about the role and must not be second-guessed.
    for (auto role : {cb_role::evse, cb_role::ev}) {
        auto const out = decide_station_id(role, -1);
        EXPECT_EQ(out.station_id, -1);
        EXPECT_EQ(out.issue, station_id_issue::none);
    }
}

TEST(cb_role, an_evse_off_station_zero_is_warned_about) {
    // The SECC is the PLCA coordinator, station 0 (ISO 15118-10). The value is still honoured.
    EXPECT_EQ(decide_station_id(cb_role::evse, 0).issue, station_id_issue::none);
    for (int value = 1; value <= 7; ++value) {
        auto const out = decide_station_id(cb_role::evse, value);
        EXPECT_EQ(out.station_id, value);
        EXPECT_EQ(out.issue, station_id_issue::evse_not_coordinator) << "value " << value;
    }
}

TEST(cb_role, an_ev_on_a_drop_node_is_silent_but_on_station_zero_is_not) {
    // 2..7 are ordinary drop nodes and say nothing; 1 is the derived default; 0 is the EVSE's seat.
    auto const coordinator = decide_station_id(cb_role::ev, 0);
    EXPECT_EQ(coordinator.station_id, 0);
    EXPECT_EQ(coordinator.issue, station_id_issue::ev_is_coordinator);

    for (int value = 1; value <= 7; ++value) {
        auto const out = decide_station_id(cb_role::ev, value);
        EXPECT_EQ(out.station_id, value);
        EXPECT_EQ(out.issue, station_id_issue::none) << "value " << value;
    }
}

TEST(cb_role, an_id_that_is_no_node_id_is_fatal) {
    // A PLCA node id is three bits. There is no safe correction: the guess for an EVSE would be
    // station 0, the coordinator seat, which is exactly what a typo must not be able to hand out.
    // So it is refused, at the same severity charge_bridge.type gives a value it does not recognise.
    for (int value : {8, 9, 127, 200, -2, -100}) {
        for (auto role : {cb_role::evse, cb_role::ev, cb_role::unspecified}) {
            auto const out = decide_station_id(role, value);
            EXPECT_EQ(out.issue, station_id_issue::out_of_range) << "value " << value;
            EXPECT_TRUE(is_fatal(out.issue)) << "value " << value;
            // Reported back as-is so the message can name it; it never reaches the MCU.
            EXPECT_EQ(out.station_id, value) << "value " << value;
        }
    }
}

TEST(cb_role, only_an_out_of_range_id_is_fatal) {
    EXPECT_FALSE(is_fatal(station_id_issue::none));
    EXPECT_FALSE(is_fatal(station_id_issue::evse_not_coordinator));
    EXPECT_FALSE(is_fatal(station_id_issue::ev_is_coordinator));
    EXPECT_TRUE(is_fatal(station_id_issue::out_of_range));
}

TEST(cb_role, every_station_id_that_is_actually_forwarded_fits_the_wire_type) {
    // station_id goes out as an int8_t. Only non-fatal decisions are forwarded, so those are the ones
    // that have to fit - a fatal one aborts startup before the narrowing cast.
    for (auto role : {cb_role::evse, cb_role::ev, cb_role::unspecified}) {
        for (int value : {-1, 0, 1, 2, 7}) {
            auto const out = decide_station_id(role, value);
            ASSERT_FALSE(is_fatal(out.issue)) << "value " << value;
            EXPECT_GE(out.station_id, -128);
            EXPECT_LE(out.station_id, 127);
        }
        auto const derived = decide_station_id(role, std::nullopt);
        EXPECT_FALSE(is_fatal(derived.issue));
        EXPECT_GE(derived.station_id, -128);
        EXPECT_LE(derived.station_id, 127);
    }
}

// --- role latch cross-check ------------------------------------------------------------------

TEST(cb_role, an_unlatched_mcu_is_not_a_mismatch) {
    // Before its first config heartbeat the MCU has nothing latched. That resolves on its own - the
    // first config always applies - so it must not raise a marker.
    EXPECT_EQ(evaluate_role_latch(cb_type_evse, cb_type_not_latched), role_latch_state::not_latched);
    EXPECT_EQ(evaluate_role_latch(cb_type_ev, cb_type_not_latched), role_latch_state::not_latched);
}

TEST(cb_role, a_latched_role_that_agrees_is_a_match) {
    EXPECT_EQ(evaluate_role_latch(cb_type_evse, cb_type_evse), role_latch_state::matched);
    EXPECT_EQ(evaluate_role_latch(cb_type_ev, cb_type_ev), role_latch_state::matched);
}

TEST(cb_role, a_latched_role_that_disagrees_is_a_mismatch) {
    // Only a reboot can change what the MCU latched, so this is the state that needs an operator.
    EXPECT_EQ(evaluate_role_latch(cb_type_evse, cb_type_ev), role_latch_state::mismatched);
    EXPECT_EQ(evaluate_role_latch(cb_type_ev, cb_type_evse), role_latch_state::mismatched);
}

TEST(cb_role, a_ccs_board_contradicting_the_configured_type_reads_as_a_mismatch) {
    // On a CCS board the role is strapping-coded and the MCU reports what the straps say. Asking for
    // EV on a strapped-EVSE board therefore arrives here as an ordinary mismatch, which is what makes
    // the hardware contradiction visible without a second check.
    EXPECT_EQ(evaluate_role_latch(to_wire(cb_role::ev), cb_type_evse), role_latch_state::mismatched);
}

TEST(cb_role, an_unexpected_latched_value_is_treated_as_a_mismatch) {
    // A value the enum does not define cannot be the configured role, so it is a mismatch rather than
    // something to quietly accept.
    EXPECT_EQ(evaluate_role_latch(cb_type_evse, 2), role_latch_state::mismatched);
    EXPECT_EQ(evaluate_role_latch(cb_type_ev, 200), role_latch_state::mismatched);
}

TEST(cb_role, an_mcs_board_only_reports_not_latched_before_its_first_accepted_config) {
    // A CCS board reports its strapped role from the start, so 255 belongs to MCS boards alone. The
    // host cannot tell the board apart here and does not need to: 255 means "nothing to compare
    // against" either way, and it is the only value that must not raise a marker.
    for (std::uint8_t configured : {cb_type_evse, cb_type_ev}) {
        EXPECT_EQ(evaluate_role_latch(configured, cb_type_not_latched), role_latch_state::not_latched);
        EXPECT_NE(evaluate_role_latch(configured, cb_type_not_latched), role_latch_state::mismatched);
    }
}

// --- normalisation ---------------------------------------------------------------------------

TEST(cb_role, the_configured_value_is_normalised_the_way_the_mcu_normalises_it) {
    // The MCU treats a cb_type the encoding does not define as unspecified and says nothing about it.
    // The parser cannot produce such a value, but the whole point of this function is to agree with
    // the MCU - including on staying quiet.
    EXPECT_EQ(normalize_cb_type(cb_type_evse), cb_type_evse);
    EXPECT_EQ(normalize_cb_type(cb_type_ev), cb_type_ev);
    // unspecified is a defined instruction, not a fallback: it must survive normalisation, or an
    // unconfigured type would silently become a claim to be an EVSE.
    EXPECT_EQ(normalize_cb_type(cb_type_unspecified), cb_type_unspecified);
    for (std::uint8_t value : {3, 42, 254, 255}) {
        EXPECT_EQ(normalize_cb_type(value), cb_type_unspecified) << "value " << static_cast<int>(value);
    }
}

TEST(cb_role, an_out_of_range_configured_value_neither_invents_nor_hides_a_mismatch) {
    // A value neither side can interpret expresses no preference, and the MCU says nothing about it.
    // So it cannot produce a phantom marker against any reported role...
    for (std::uint8_t latched : {cb_type_evse, cb_type_ev, cb_type_not_latched}) {
        auto const state = evaluate_role_latch(42, latched);
        EXPECT_EQ(state, role_latch_state::not_configured) << "latched " << static_cast<int>(latched);
        EXPECT_NE(state, role_latch_state::mismatched) << "latched " << static_cast<int>(latched);
    }

    // ...and it hides nothing either, because there is no real disagreement to hide: a garbage value
    // is not a request for a role. The values that ARE requests still compare exactly - see
    // normalisation_never_hides_a_real_role_disagreement.
    EXPECT_EQ(evaluate_role_latch(cb_type_evse, cb_type_ev), role_latch_state::mismatched);
    EXPECT_EQ(evaluate_role_latch(cb_type_ev, cb_type_evse), role_latch_state::mismatched);
}

// --- an unconfigured type ---------------------------------------------------------------------

TEST(cb_role, an_unconfigured_type_never_mismatches) {
    // The host expressed no preference, so no reported role can contradict it. This is the finding
    // that mattered: treating absent as EVSE raised a permanent, unfixable alarm on a strapped CCS-EV
    // board whose shipped config simply has no type key.
    for (std::uint8_t latched : {cb_type_evse, cb_type_ev, cb_type_not_latched}) {
        auto const state = evaluate_role_latch(cb_type_unspecified, latched);
        EXPECT_EQ(state, role_latch_state::not_configured) << "latched " << static_cast<int>(latched);
        EXPECT_NE(state, role_latch_state::mismatched) << "latched " << static_cast<int>(latched);
    }
    // Including values the encoding does not define - still no preference, still no disagreement.
    for (std::uint8_t latched : {3, 42, 254}) {
        EXPECT_EQ(evaluate_role_latch(cb_type_unspecified, latched), role_latch_state::not_configured)
            << "latched " << static_cast<int>(latched);
    }
}

TEST(cb_role, the_shipped_ev_config_shape_produces_no_marker) {
    // config-CB-EVAL-EV.yaml has no type key and runs on a strapped CCS-EV board, so the MCU reports
    // EV. Absent type vs reported EV must be a quiet not_configured, not a mismatch.
    EXPECT_EQ(evaluate_role_latch(to_wire(cb_role::unspecified), cb_type_ev), role_latch_state::not_configured);
    // The mirror image - no type key on a strapped EVSE board - has to be just as quiet.
    EXPECT_EQ(evaluate_role_latch(to_wire(cb_role::unspecified), cb_type_evse), role_latch_state::not_configured);
}

TEST(cb_role, deleting_the_type_key_from_an_already_latched_ev_board_is_quiet) {
    // An MCS board that latched EV, then a config with the type key removed. The MCU refuses the
    // unspecified value quietly - deleting a key is not a request for the other role - so the host
    // must be equally quiet: no marker, and the operator still sees the EV the board is running plus
    // the fact that nothing is configured. Rendering keys on not_configured, so this is the state that
    // produces "Role: EV (type not configured)".
    EXPECT_EQ(evaluate_role_latch(cb_type_unspecified, cb_type_ev), role_latch_state::not_configured);
    // The same with the roles swapped, and with a board that has not latched anything yet.
    EXPECT_EQ(evaluate_role_latch(cb_type_unspecified, cb_type_evse), role_latch_state::not_configured);
    EXPECT_EQ(evaluate_role_latch(cb_type_unspecified, cb_type_not_latched), role_latch_state::not_configured);
}

TEST(cb_role, an_unconfigured_type_derives_and_warns_like_an_evse) {
    // The MCU's own pre-config behaviour is EVSE semantics, so the derived station id follows it.
    auto const derived = decide_station_id(cb_role::unspecified, std::nullopt);
    EXPECT_EQ(derived.station_id, 0);
    EXPECT_TRUE(derived.derived);
    EXPECT_EQ(derived.issue, station_id_issue::none);

    // And an explicit non-zero id is questioned the same way it would be for an explicit EVSE.
    EXPECT_EQ(decide_station_id(cb_role::unspecified, 0).issue, station_id_issue::none);
    EXPECT_EQ(decide_station_id(cb_role::unspecified, 3).issue, station_id_issue::evse_not_coordinator);
    EXPECT_EQ(decide_station_id(cb_role::unspecified, -1).issue, station_id_issue::none);
}

// --- remedy wording --------------------------------------------------------------------------

TEST(cb_role, the_remedy_names_whichever_side_owns_the_role) {
    // A CCS board takes its role from strapping resistors: telling anyone to reboot would be false
    // advice, because a reboot cannot change a strap.
    std::string const ccs = role_mismatch_remedy(CB_LINK_TECH_PLC);
    EXPECT_NE(ccs.find("strapping-coded"), std::string::npos);
    EXPECT_EQ(ccs.find("reboot"), std::string::npos);

    // An MCS board latches the role from the first config after boot, so a reboot is exactly the fix.
    std::string const mcs = role_mismatch_remedy(CB_LINK_TECH_SPE);
    EXPECT_NE(mcs.find("reboot"), std::string::npos);
    EXPECT_EQ(mcs.find("strapping"), std::string::npos);
}

TEST(cb_role, an_unknown_technology_names_both_possibilities_and_commits_to_neither) {
    // A board whose plc block is disabled never reports a technology, so UNKNOWN is a lasting state,
    // not a transient one - and on a CCS board the MCS advice would be confidently wrong. The wording
    // must therefore cover both and borrow neither.
    std::string const unknown = role_mismatch_remedy(CB_LINK_TECH_UNKNOWN);
    EXPECT_STRNE(unknown.c_str(), role_mismatch_remedy(CB_LINK_TECH_SPE));
    EXPECT_STRNE(unknown.c_str(), role_mismatch_remedy(CB_LINK_TECH_PLC));
    // Both routes are named, so neither is presented as the answer.
    EXPECT_NE(unknown.find("CCS"), std::string::npos);
    EXPECT_NE(unknown.find("MCS"), std::string::npos);
    EXPECT_NE(unknown.find("charge_bridge.type"), std::string::npos);
    EXPECT_NE(unknown.find("reboot"), std::string::npos);
}

TEST(cb_role, normalisation_never_hides_a_real_role_disagreement) {
    // The two values the parser can actually emit must keep comparing exactly.
    EXPECT_EQ(evaluate_role_latch(to_wire(cb_role::evse), cb_type_ev), role_latch_state::mismatched);
    EXPECT_EQ(evaluate_role_latch(to_wire(cb_role::ev), cb_type_evse), role_latch_state::mismatched);
    EXPECT_EQ(evaluate_role_latch(to_wire(cb_role::evse), cb_type_evse), role_latch_state::matched);
    EXPECT_EQ(evaluate_role_latch(to_wire(cb_role::ev), cb_type_ev), role_latch_state::matched);
}

} // namespace
