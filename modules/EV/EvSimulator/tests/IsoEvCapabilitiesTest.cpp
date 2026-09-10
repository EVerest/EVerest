// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// The three EV-side ISO 15118 capabilities EvSimulator reports to its `ev`
// peer: cp_state_changed, update_present_values and abort_charging.
//
// All three publish state EvSimulator already holds, so what these tests pin
// down is the wiring and the defaults, not new arithmetic:
//   * every CP state the FSM applies reaches the HLC stack, which has no
//     board support connection of its own to learn it from;
//   * the reported present values default to what the EVSE delivered, and an
//     override replaces one field without zeroing the other. A present value
//     that is never reported goes onto the wire as zero and reads at the SECC
//     as a measured zero, so "leaves the other alone" is the property that
//     matters, not merely "sets the one asked for";
//   * teardown aborts only when the scenario asked for it.

#include "../main/FsmContext.hpp"
#include "../main/SocIntegrator.hpp"
#include "../main/states/Unplugged.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

namespace {

// A DC fixture whose delivered voltage and current are both live, so the
// reported present values come from measurement rather than from the
// open-loop configured fallback.
TestFixture dc_fixture() {
    TestFixture fx;
    fx.cfg.tick_interval_ms = 100;
    fx.cfg.dc_target_voltage = 400;
    fx.cfg.dc_target_current = 125;
    fx.cfg.dc_max_current_limit = 300;
    return fx;
}

} // namespace

TEST_CASE("cp_state_changed reports every applied CP state to the ISO peer", "[evsim][iso][cp]") {
    struct CpCase {
        ::types::ev_board_support::EvCpState applied;
        const char* reported;
    };
    // EvCpState has no F, so these five are the whole domain: a new member
    // would fail to compile in to_iso_cp_state rather than fall through here.
    auto tc = GENERATE(values<CpCase>({
        {::types::ev_board_support::EvCpState::A, "cp_state_changed(cp_state=A)"},
        {::types::ev_board_support::EvCpState::B, "cp_state_changed(cp_state=B)"},
        {::types::ev_board_support::EvCpState::C, "cp_state_changed(cp_state=C)"},
        {::types::ev_board_support::EvCpState::D, "cp_state_changed(cp_state=D)"},
        {::types::ev_board_support::EvCpState::E, "cp_state_changed(cp_state=E)"},
    }));
    CAPTURE(tc.reported);

    TestFixture fx;
    auto ctx = fx.make_ctx();
    fx.mocks.bsp.clear();
    fx.mocks.iso.clear();

    ctx->set_cp(tc.applied);

    CHECK(contains_substr(fx.mocks.iso.records, tc.reported));
    // The BSP still gets the same state; the ISO report is an addition, not a
    // redirection.
    CHECK(fx.mocks.bsp.records.size() == 1);
}

TEST_CASE("the CP report and the BSP push are guarded independently", "[evsim][iso][cp]") {
    SECTION("no ISO peer: the BSP still sees the CP state") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        ctx->peer_actions.iso = IsoPeer{}; // absent peer
        fx.mocks.bsp.clear();
        fx.mocks.iso.clear();

        ctx->set_cp(::types::ev_board_support::EvCpState::C);

        CHECK(fx.mocks.bsp.records.size() == 1);
        CHECK(fx.mocks.iso.records.empty());
    }

    SECTION("no BSP peer: the ISO peer still gets the CP report") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        ctx->peer_actions.bsp = BspPeer{}; // absent peer
        fx.mocks.bsp.clear();
        fx.mocks.iso.clear();

        ctx->set_cp(::types::ev_board_support::EvCpState::C);

        CHECK(fx.mocks.bsp.records.empty());
        CHECK(contains_substr(fx.mocks.iso.records, "cp_state_changed(cp_state=C)"));
    }
}

TEST_CASE("update_present_values defaults to the delivered values", "[evsim][iso][present-values]") {
    SECTION("DC reports the delivered voltage") {
        auto fx = dc_fixture();
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        // Delivered, not configured: both differ from cfg so a fallback would
        // be visible in the recorded value.
        ctx->vars.dc_present_voltage_v = 402.0f;
        ctx->vars.evse_dc_present_current_a = 120.0f;
        fx.mocks.iso.clear();

        soc_step(*ctx);

        CHECK(contains_substr(fx.mocks.iso.records, "update_present_values(present_voltage=402"));
        // 402 V * 120 A = 48240 W of delivered power.
        CHECK(contains_substr(fx.mocks.iso.records, "present_active_power=48240"));
    }

    SECTION("AC reports the active power and no voltage") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 100;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        ctx->vars.charging_current_a = 16.0f;
        ctx->vars.three_phases = true;
        fx.mocks.iso.clear();

        soc_step(*ctx);

        // 16 A * 230 V * 3 phases = 11040 W. AC puts the power on the wire,
        // not a DC bus voltage.
        CHECK(contains_substr(fx.mocks.iso.records, "present_active_power=11040"));
        CHECK(contains_substr(fx.mocks.iso.records, "present_voltage=unset"));
    }

    SECTION("nothing is reported outside a session") {
        auto fx = dc_fixture();
        auto ctx = fx.make_ctx();
        clear_session(*ctx);
        fx.mocks.iso.clear();

        soc_step(*ctx);

        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "update_present_values"));
    }
}

TEST_CASE("a present-value override replaces one field and leaves the other echoing",
          "[evsim][iso][present-values]") {
    SECTION("overriding the voltage keeps the delivered power") {
        auto fx = dc_fixture();
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.dc_present_voltage_v = 402.0f;
        ctx->vars.evse_dc_present_current_a = 120.0f;
        // The charger delivered 402 V; the EV claims to measure 350 V. This is
        // the disagreement a SECC has obligations about.
        ctx->vars.present_voltage_override = 350.0f;
        fx.mocks.iso.clear();

        soc_step(*ctx);

        CHECK(contains_substr(fx.mocks.iso.records, "update_present_values(present_voltage=350"));
        // Still the delivered power, NOT zero: overriding one field must not
        // silently report the other as a measured zero.
        CHECK(contains_substr(fx.mocks.iso.records, "present_active_power=48240"));
    }

    SECTION("overriding the power keeps the delivered voltage") {
        auto fx = dc_fixture();
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.dc_present_voltage_v = 402.0f;
        ctx->vars.evse_dc_present_current_a = 120.0f;
        ctx->vars.present_active_power_override = 1000.0f;
        fx.mocks.iso.clear();

        soc_step(*ctx);

        CHECK(contains_substr(fx.mocks.iso.records, "update_present_values(present_voltage=402"));
        CHECK(contains_substr(fx.mocks.iso.records, "present_active_power=1000"));
    }

    SECTION("a zero override is reported, not treated as absent") {
        auto fx = dc_fixture();
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.dc_present_voltage_v = 402.0f;
        ctx->vars.evse_dc_present_current_a = 120.0f;
        // A deliberate zero is a measurement the test asked for; it must reach
        // the peer rather than fall back to the delivered value.
        ctx->vars.present_voltage_override = 0.0f;
        fx.mocks.iso.clear();

        soc_step(*ctx);

        CHECK(contains_substr(fx.mocks.iso.records, "update_present_values(present_voltage=0"));
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "present_voltage=402"));
    }
}

TEST_CASE("the scenario chooses abort over a clean stop", "[evsim][iso][abort]") {
    SECTION("the default teardown is the clean stop") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        fx.mocks.iso.clear();

        ctx->iso_end_session();

        CHECK(contains_substr(fx.mocks.iso.records, "stop_charging()"));
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "abort_charging()"));
    }

    SECTION("a latched abort terminates the session instead") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        ctx->vars.abort_on_stop = true;
        fx.mocks.iso.clear();

        ctx->iso_end_session();

        CHECK(contains_substr(fx.mocks.iso.records, "abort_charging()"));
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "stop_charging()"));
    }
}

TEST_CASE("a fresh session inherits neither the teardown selector nor an override",
          "[evsim][iso][session-boundary]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->vars.abort_on_stop = true;
    ctx->vars.present_voltage_override = 350.0f;
    ctx->vars.present_active_power_override = 1000.0f;

    Unplugged unplugged{*ctx};
    unplugged.enter();

    // Otherwise a scenario that staged one abrupt teardown would keep deciding
    // how later sessions end, including the ones torn down by the charger.
    CHECK_FALSE(ctx->vars.abort_on_stop);
    CHECK_FALSE(ctx->vars.present_voltage_override.has_value());
    CHECK_FALSE(ctx->vars.present_active_power_override.has_value());
}
