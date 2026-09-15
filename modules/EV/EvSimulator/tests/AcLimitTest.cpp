// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Exercises the EVSE-communicated AC limit handling: an ISO AC session must
// clamp the applied charge current to the most recent ac_evse_max_current /
// ac_evse_target_power value, mirroring the EvManager reference behavior.

#include "../main/FsmContext.hpp"
#include "../main/states/Charging.hpp"
#include "../main/states/Plugged.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <generated/types/iso15118.hpp>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

TEST_CASE("EvSimulator AC EVSE limit clamping", "[evsim][aclimit]") {
    TestFixture fx;
    fx.cfg.ac_nominal_voltage = 230.0;
    fx.cfg.three_phases = true;

    SECTION("Charging: IsoAcMaxCurrent=0 clamps applied current to 0") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIso2);
        ctx->vars.charging_current_a = 16.0f;
        Charging s{*ctx};
        fx.mocks.bsp.clear();

        Event ev{IsoAcMaxCurrentEvt{/*max_current_a=*/0.0f}};
        auto result = s.feed(ev);

        CHECK(result.new_state == nullptr);
        // The EVSE limit (0 A) is below the configured 16 A, so the applied
        // current is clamped to the limit.
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=0)"));
        CHECK(ctx->vars.charging_current_a == 16.0f);
    }

    SECTION("Charging: IsoAcTargetPower derives a per-phase current and clamps") {
        // 3-phase target_active_power=3450 W at 230 V => 5 A per phase, which
        // is below the configured 16 A, so the applied current is 5 A.
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIso2);
        ctx->vars.charging_current_a = 16.0f;
        Charging s{*ctx};
        fx.mocks.bsp.clear();

        ::types::iso15118::AcTargetPower tp{};
        tp.target_active_power = 3450.0f;
        tp.target_active_power_L2 = 3450.0f;
        tp.target_active_power_L3 = 3450.0f;
        Event ev{IsoAcTargetPowerEvt{tp}};
        auto result = s.feed(ev);

        CHECK(result.new_state == nullptr);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=5)"));
        CHECK(ctx->vars.charging_current_a == 16.0f);
    }

    SECTION("Charging: SetChargingCurrent re-clamps against the most recent EVSE limit") {
        // After the EVSE communicates a 10 A ceiling, a SetChargingCurrent
        // request for 16 A must apply the 10 A ceiling rather than 16 A.
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        ctx->vars.evse_ac_max_current_a = 10.0f;
        Charging s{*ctx};
        fx.mocks.bsp.clear();

        Event ev{EventKind::SetChargingCurrent};
        ev.payload = api::SetChargingCurrentParams{16.0f, true};
        auto result = s.feed(ev);

        CHECK(result.new_state == nullptr);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=10)"));
        CHECK(ctx->vars.charging_current_a == 16.0f);
    }

    SECTION("Charging: SetChargingCurrent below the EVSE limit applies verbatim") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        ctx->vars.evse_ac_max_current_a = 10.0f;
        Charging s{*ctx};
        fx.mocks.bsp.clear();

        Event ev{EventKind::SetChargingCurrent};
        ev.payload = api::SetChargingCurrentParams{6.0f, true};
        auto result = s.feed(ev);

        CHECK(result.new_state == nullptr);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=6)"));
        CHECK(ctx->vars.charging_current_a == 6.0f);
    }

    SECTION("Plugged: BeginSession clamps the AC params against an earlier EVSE limit") {
        // An EVSE limit received while still in Plugged (e.g. before Charging
        // is entered) must clamp the AC params applied at BeginSession.
        auto ctx = fx.make_ctx();
        ctx->configured_session = api::SessionConfigParams{api::AcIso2SessionParams{}};
        // Configured charging_current defaults to cfg.max_current_a (16 A).
        ctx->vars.evse_ac_max_current_a = 8.0f;
        Plugged s{*ctx};
        fx.mocks.bsp.clear();

        auto result = s.feed(Event{BeginSessionEvt{}});

        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=8)"));
        CHECK(ctx->vars.charging_current_a == 16.0f);
    }

    SECTION("Charging::enter clamps the first applied current to the EVSE limit") {
        // A limit received during Plugged must already constrain the current
        // applied on Charging::enter.
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        ctx->vars.charging_current_a = 16.0f;
        ctx->vars.three_phases = true;
        ctx->vars.evse_ac_max_current_a = 12.0f;
        Charging s{*ctx};
        fx.mocks.bsp.clear();

        s.enter();

        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=12)"));
        CHECK(ctx->vars.charging_current_a == 16.0f);
    }

    SECTION("Charging: raising IsoAcMaxCurrent restores applied current toward desired") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIso2);
        ctx->vars.charging_current_a = 16.0f; // EV desired
        ctx->vars.three_phases = true;
        Charging s{*ctx};
        fx.mocks.bsp.clear();

        // Transient EVSE ceiling of 0 A clamps applied to 0 but must NOT destroy desired.
        s.feed(Event{IsoAcMaxCurrentEvt{/*max_current_a=*/0.0f}});
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=0)"));
        CHECK(ctx->vars.charging_current_a == 16.0f);

        // Raising the ceiling to 32 A recovers the applied current to the desired 16 A (min(16,32)).
        fx.mocks.bsp.clear();
        s.feed(Event{IsoAcMaxCurrentEvt{/*max_current_a=*/32.0f}});
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=16)"));
        CHECK(ctx->vars.charging_current_a == 16.0f);
    }
}
