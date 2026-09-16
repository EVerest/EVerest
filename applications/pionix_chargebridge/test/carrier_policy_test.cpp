// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

// Decision table and failure modes of the tap carrier policy. decide_carrier() is a pure function
// on purpose, so all of this runs without a kernel, a tap device or a ChargeBridge.

#include <charge_bridge/carrier_policy.hpp>
#include <gtest/gtest.h>

namespace {

using charge_bridge::carrier_fallback;
using charge_bridge::carrier_inputs;
using charge_bridge::carrier_mode;
using charge_bridge::decide_carrier;

// A fully up MCS link: the only combination that may raise the carrier.
carrier_inputs mcs_link_up() {
    carrier_inputs in;
    in.mode = carrier_mode::firmware;
    in.cb_connected = true;
    in.phy_operational = true;
    in.plca_engaged = true;
    in.technology = CB_LINK_TECH_SPE;
    return in;
}

// --- mode none: the PLC / default setting ---------------------------------------------------

TEST(carrier_policy, mode_none_never_touches_the_device) {
    // Not even for a fully reported SPE link: the ioctl must never be issued in this mode, which is
    // what makes the default byte-for-byte compatible with every existing installation.
    for (bool connected : {false, true}) {
        for (bool phy : {false, true}) {
            for (bool plca : {false, true}) {
                for (std::uint8_t tech : {CB_LINK_TECH_UNKNOWN, CB_LINK_TECH_PLC, CB_LINK_TECH_SPE}) {
                    carrier_inputs in;
                    in.mode = carrier_mode::none;
                    in.cb_connected = connected;
                    in.phy_operational = phy;
                    in.plca_engaged = plca;
                    in.technology = tech;
                    auto const out = decide_carrier(in);
                    EXPECT_FALSE(out.apply);
                    EXPECT_FALSE(out.refuse_bridge);
                    EXPECT_FALSE(out.plc_fail_open);
                }
            }
        }
    }
}

TEST(carrier_policy, mode_none_warns_only_about_an_spe_board) {
    carrier_inputs in;
    in.mode = carrier_mode::none;

    in.technology = CB_LINK_TECH_PLC;
    EXPECT_FALSE(decide_carrier(in).technology_mismatch);

    // Before the first configured heartbeat the reports can be all-zero. That is not a mismatch.
    in.technology = CB_LINK_TECH_UNKNOWN;
    EXPECT_FALSE(decide_carrier(in).technology_mismatch);

    in.technology = CB_LINK_TECH_SPE;
    EXPECT_TRUE(decide_carrier(in).technology_mismatch);
}

// --- mode firmware: the MCS setting ---------------------------------------------------------

TEST(carrier_policy, firmware_raises_the_carrier_only_when_everything_is_up) {
    auto const out = decide_carrier(mcs_link_up());
    EXPECT_TRUE(out.apply);
    EXPECT_TRUE(out.carrier);
    EXPECT_FALSE(out.technology_mismatch);
    EXPECT_FALSE(out.plc_fail_open);
    EXPECT_FALSE(out.refuse_bridge);
}

TEST(carrier_policy, firmware_carrier_is_the_conjunction_of_all_three_inputs) {
    for (bool connected : {false, true}) {
        for (bool phy : {false, true}) {
            for (bool plca : {false, true}) {
                auto in = mcs_link_up();
                in.cb_connected = connected;
                in.phy_operational = phy;
                in.plca_engaged = plca;
                auto const out = decide_carrier(in);
                EXPECT_TRUE(out.apply);
                EXPECT_EQ(out.carrier, connected and phy and plca)
                    << "connected=" << connected << " phy=" << phy << " plca=" << plca;
            }
        }
    }
}

TEST(carrier_policy, firmware_before_the_first_report_keeps_the_carrier_down) {
    // The default-constructed inputs are the state right after construction, and the all-zero report
    // an MCU sends before it has seen a configured heartbeat produces the same thing.
    carrier_inputs in;
    in.mode = carrier_mode::firmware;
    auto const out = decide_carrier(in);
    EXPECT_TRUE(out.apply);
    EXPECT_FALSE(out.carrier);
    EXPECT_FALSE(out.technology_mismatch);
}

TEST(carrier_policy, losing_the_chargebridge_drops_the_carrier) {
    // Failure row: heartbeat timeout. A stale carrier-on must be structurally impossible.
    auto in = mcs_link_up();
    in.cb_connected = false;
    auto const out = decide_carrier(in);
    EXPECT_TRUE(out.apply);
    EXPECT_FALSE(out.carrier);
}

TEST(carrier_policy, a_firmware_reboot_drops_the_carrier) {
    // Failure row: the uptime-regression path synthesizes an all-zero report. The connection is still
    // considered up at that moment, so it is the zeroed flags that must pull the carrier down.
    carrier_inputs in;
    in.mode = carrier_mode::firmware;
    in.cb_connected = true;
    auto const out = decide_carrier(in);
    EXPECT_TRUE(out.apply);
    EXPECT_FALSE(out.carrier);
}

// --- failure modes --------------------------------------------------------------------------

TEST(carrier_policy, a_plc_board_in_firmware_mode_fails_open) {
    // Failure row: technology mismatch. A HomePlug board reports PLC with all flags zero; applying
    // those would strangle SLAC, which needs to cross the tap before any link exists.
    auto in = mcs_link_up();
    in.technology = CB_LINK_TECH_PLC;
    in.phy_operational = false;
    in.plca_engaged = false;
    auto const out = decide_carrier(in);
    EXPECT_TRUE(out.apply);
    EXPECT_TRUE(out.carrier);
    EXPECT_TRUE(out.plc_fail_open);
    EXPECT_TRUE(out.technology_mismatch);
    EXPECT_FALSE(out.refuse_bridge);
}

TEST(carrier_policy, a_plc_board_in_firmware_mode_ignores_the_connection_state_too) {
    // Fail-open means fail-open: not even a lost heartbeat may pull the carrier down on a PLC board,
    // or a reconnect would have to race SLAC.
    auto in = mcs_link_up();
    in.technology = CB_LINK_TECH_PLC;
    in.cb_connected = false;
    auto const out = decide_carrier(in);
    EXPECT_TRUE(out.apply);
    EXPECT_TRUE(out.carrier);
    EXPECT_TRUE(out.plc_fail_open);
}

TEST(carrier_policy, unsupported_kernel_with_the_fail_policy_refuses_the_bridge) {
    // Failure row: kernel without TUNSETCARRIER. The default refuses rather than silently degrading
    // link supervision to a permanent "up".
    auto in = mcs_link_up();
    in.carrier_unsupported = true;
    in.fallback = carrier_fallback::fail;
    auto const out = decide_carrier(in);
    EXPECT_TRUE(out.refuse_bridge);
    EXPECT_FALSE(out.apply);
}

TEST(carrier_policy, unsupported_kernel_with_the_warn_policy_keeps_bridging) {
    auto in = mcs_link_up();
    in.carrier_unsupported = true;
    in.fallback = carrier_fallback::warn;
    auto const out = decide_carrier(in);
    EXPECT_FALSE(out.refuse_bridge);
    // No ioctl is attempted again, and the carrier stays where the kernel left it: on.
    EXPECT_FALSE(out.apply);
    EXPECT_TRUE(out.carrier);
}

TEST(carrier_policy, unsupported_kernel_still_reports_a_technology_mismatch) {
    // The cross-check is independent of the ioctl: a misconfigured installation must be told even
    // when the carrier cannot be driven at all.
    auto in = mcs_link_up();
    in.carrier_unsupported = true;
    in.fallback = carrier_fallback::warn;
    in.technology = CB_LINK_TECH_PLC;
    EXPECT_TRUE(decide_carrier(in).technology_mismatch);
}

// The technology latch. plc_bridge only ever updates its remembered technology from a report that
// carries a real one, so the all-zero packet the reboot path synthesizes cannot clear it. These pin
// down what that latch buys, expressed the way plc_bridge feeds decide_carrier().
TEST(carrier_policy, a_latched_plc_technology_keeps_the_carrier_on_across_a_reboot_report) {
    // The MCU rebooted: the synthesized report zeroes the flags but carries technology = UNKNOWN.
    // With the latch the technology stays PLC, so the fail-open branch holds and the carrier stays ON.
    // Without it the decision would fall through to the MCS rule and drop the carrier for up to a
    // heartbeat - a transient carrier-off on a HomePlug board, which is what strangles SLAC.
    auto in = mcs_link_up();
    in.technology = CB_LINK_TECH_PLC;
    in.phy_operational = false;
    in.plca_engaged = false;
    auto const out = decide_carrier(in);
    EXPECT_TRUE(out.apply);
    EXPECT_TRUE(out.carrier);
    EXPECT_TRUE(out.plc_fail_open);

    // The same inputs with the latch cleared - what the bug did - drop the carrier.
    in.technology = CB_LINK_TECH_UNKNOWN;
    auto const unlatched = decide_carrier(in);
    EXPECT_TRUE(unlatched.apply);
    EXPECT_FALSE(unlatched.carrier);
}

TEST(carrier_policy, a_latched_spe_technology_still_drops_the_carrier_on_a_reboot_report) {
    // The other half of the latch: on a genuine MCS link the zeroed flags must take effect.
    auto in = mcs_link_up();
    in.phy_operational = false;
    in.plca_engaged = false;
    auto const out = decide_carrier(in);
    EXPECT_TRUE(out.apply);
    EXPECT_FALSE(out.carrier);
    EXPECT_FALSE(out.plc_fail_open);
    EXPECT_FALSE(out.technology_mismatch);
}

// --- the basic-signalling gate (plc.carrier_gate: ce_mated) -----------------------------------

TEST(carrier_policy, ce_state_mated_is_the_table_cc103_mated_set) {
    using charge_bridge::ce_state_mated;
    // Mated: every state a vehicle's presence produces, B0 through EC. B0 must be in (the data link
    // is established after mating and BEFORE S S3 closes, and S S3-close is gated on the PHY - a
    // gate on B would deadlock), and EC must be in (the segment has to survive an EVSE emergency
    // and the CC.5.2.3.2 restart guard).
    for (std::uint8_t mated :
         {CeState_B0, CeState_B0_Aux, CeState_B, CeState_B_Aux, CeState_C, CeState_C_Aux, CeState_EC, CeState_EC_Aux}) {
        EXPECT_TRUE(ce_state_mated(mated)) << "state " << static_cast<int>(mated);
    }
    // Unmated, and everything unknown: fail-safe is link-down.
    for (std::uint8_t unmated : {CeState_NotApplicable, CeState_A, CeState_E, CeState_Invalid}) {
        EXPECT_FALSE(ce_state_mated(unmated)) << "state " << static_cast<int>(unmated);
    }
    EXPECT_FALSE(ce_state_mated(0xFF));
}

TEST(carrier_policy, the_gate_holds_the_carrier_down_until_mated) {
    // A fully up MCS link, gate configured, no vehicle: the carrier stays down. This is the whole
    // point - the link exists exactly while a vehicle is physically present.
    auto in = mcs_link_up();
    in.gate = charge_bridge::carrier_gate::ce_mated;
    in.ce_mated = false;
    auto const down = decide_carrier(in);
    EXPECT_TRUE(down.apply);
    EXPECT_FALSE(down.carrier);

    in.ce_mated = true;
    auto const up = decide_carrier(in);
    EXPECT_TRUE(up.apply);
    EXPECT_TRUE(up.carrier);
}

TEST(carrier_policy, the_gate_never_raises_a_carrier_the_phy_rule_would_not) {
    // The gate only ever narrows the PHY rule: with any of the three PHY inputs down, a mated CE
    // state must not raise the carrier.
    for (bool connected : {false, true}) {
        for (bool phy : {false, true}) {
            for (bool plca : {false, true}) {
                auto in = mcs_link_up();
                in.cb_connected = connected;
                in.phy_operational = phy;
                in.plca_engaged = plca;
                in.gate = charge_bridge::carrier_gate::ce_mated;
                in.ce_mated = true;
                EXPECT_EQ(decide_carrier(in).carrier, connected and phy and plca);
            }
        }
    }
}

TEST(carrier_policy, gate_none_ignores_the_ce_input_entirely) {
    // Compatibility: without the config key, the reported CE state must not be able to influence
    // the carrier - this is today's PHY-only behavior, whatever the BSP happens to report.
    for (bool mated : {false, true}) {
        auto in = mcs_link_up();
        in.ce_mated = mated;
        EXPECT_TRUE(decide_carrier(in).carrier);
    }
}

TEST(carrier_policy, the_gate_cannot_strangle_a_plc_board) {
    // Misconfiguration row: carrier_gate on a board that reports PLC. Fail-open wins - a HomePlug
    // board has no CE and would read permanently unmated, and SLAC must cross the tap regardless.
    auto in = mcs_link_up();
    in.technology = CB_LINK_TECH_PLC;
    in.gate = charge_bridge::carrier_gate::ce_mated;
    in.ce_mated = false;
    auto const out = decide_carrier(in);
    EXPECT_TRUE(out.apply);
    EXPECT_TRUE(out.carrier);
    EXPECT_TRUE(out.plc_fail_open);
}

TEST(carrier_policy, the_gate_is_irrelevant_in_mode_none) {
    // Parse-level validation refuses this combination, but the policy must be safe against it too:
    // mode none never touches the device, gate or no gate.
    carrier_inputs in;
    in.mode = carrier_mode::none;
    in.gate = charge_bridge::carrier_gate::ce_mated;
    in.ce_mated = false;
    EXPECT_FALSE(decide_carrier(in).apply);
}

TEST(carrier_policy, an_unsupported_kernel_is_irrelevant_in_mode_none) {
    // The ioctl is never issued in this mode, so its availability cannot refuse anything - a PLC
    // installation on an ancient kernel keeps working untouched.
    carrier_inputs in;
    in.mode = carrier_mode::none;
    in.carrier_unsupported = true;
    in.fallback = carrier_fallback::fail;
    auto const out = decide_carrier(in);
    EXPECT_FALSE(out.refuse_bridge);
    EXPECT_FALSE(out.apply);
}

} // namespace
