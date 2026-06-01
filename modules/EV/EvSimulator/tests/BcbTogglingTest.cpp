// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/states/BcbToggling.hpp"
#include "../main/FsmContext.hpp"
#include "../main/StateBase.hpp"
#include "../main/states/Charging.hpp"
#include "../main/states/Paused.hpp"
#include "../main/states/V2GNegotiating.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <everest/util/fsm/fsm.hpp>
#include <everest_api_types/ev_simulator/codec.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

namespace {

int count_substr(const std::vector<std::string>& records, const std::string& needle) {
    return static_cast<int>(std::count_if(records.begin(), records.end(),
                                          [&](const std::string& r) { return r.find(needle) != std::string::npos; }));
}

} // namespace

TEST_CASE("EvSimulator BcbToggling cycle B<->C * 3 rounds -> V2GNegotiating", "[evsim][group2]") {
    TestFixture fx;

    SECTION("enter sets CP=B, arms 250ms, publishes BcbToggling") {
        auto ctx = fx.make_ctx();
        ctx->vars.bcb_remaining = 6;
        BcbToggling s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));
        REQUIRE_FALSE(fx.timer.state_timer_arms.empty());
        CHECK(fx.timer.state_timer_arms.back() == std::chrono::milliseconds(250));
    }

    SECTION("enter defaults bcb_remaining to 6 when unset") {
        auto ctx = fx.make_ctx();
        ctx->vars.bcb_remaining = 0;
        BcbToggling s{*ctx};
        s.enter();
        CHECK(ctx->vars.bcb_remaining == 6);
    }

    SECTION("enter preserves pre-seeded bcb_remaining value") {
        auto ctx = fx.make_ctx();
        ctx->vars.bcb_remaining = 6;
        BcbToggling s{*ctx};
        s.enter();
        CHECK(ctx->vars.bcb_remaining == 6);
    }

    SECTION("6 StateDeadlines cycle B<->C and final one transitions to V2GNegotiating") {
        auto ctx = fx.make_ctx();
        ctx->vars.bcb_remaining = 6;
        BcbToggling s{*ctx};
        s.enter();
        fx.mocks.bsp.clear(); // clear the set_cp(B) from enter()

        // Cycle: 6 ticks. remaining starts at 6 → even → C; then 5 → odd → B; ...
        // Expected sequence in CP per tick: C, B, C, B, C, B (3 round-trips); last tick transitions.
        for (int i = 0; i < 5; ++i) {
            auto result = s.feed(Event{EventKind::StateDeadline});
            CHECK_FALSE(result.new_state);
        }
        // The 6th deadline performs the final edge and transitions.
        auto result = s.feed(Event{EventKind::StateDeadline});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::V2GNegotiating);

        // Verify cycle: 3 set_cp(C) and 3 set_cp(B) edges.
        CHECK(count_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=C)") == 3);
        CHECK(count_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)") == 3);
    }

    SECTION("After 5 StateDeadlines, bcb_remaining=1 and timer re-armed at 250ms") {
        auto ctx = fx.make_ctx();
        ctx->vars.bcb_remaining = 6;
        BcbToggling s{*ctx};
        for (int i = 0; i < 5; ++i) {
            s.feed(Event{EventKind::StateDeadline});
        }
        CHECK(ctx->vars.bcb_remaining == 1);
        // Each non-final tick re-arms the timer.
        CHECK(std::count(fx.timer.state_timer_arms.begin(), fx.timer.state_timer_arms.end(),
                         std::chrono::milliseconds(250)) >= 5);
    }

    SECTION("Paused.feed(ResumeSession) in ISO mode seeds bcb_remaining=6 and -> BcbToggling") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        Paused p{*ctx};
        auto result = p.feed(Event{EventKind::ResumeSession});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::BcbToggling);
        CHECK(ctx->vars.bcb_remaining == 6);
    }

    SECTION("Paused.feed(ResumeSession) in AcIec mode -> Charging directly (no BCB)") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        Paused p{*ctx};
        auto result = p.feed(Event{EventKind::ResumeSession});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Charging);
        CHECK(ctx->vars.bcb_remaining == 0);
    }

    SECTION("Paused.feed(ResumeSession) with no session -> BcbToggling (preserves prior behavior)") {
        auto ctx = fx.make_ctx();
        Paused p{*ctx};
        auto result = p.feed(Event{EventKind::ResumeSession});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::BcbToggling);
        CHECK(ctx->vars.bcb_remaining == 6);
    }

    SECTION("Paused.feed(BcbToggle) without count defaults to 3 round-trips (6 edges) and -> BcbToggling") {
        auto ctx = fx.make_ctx();
        Paused p{*ctx};
        Event ev{EventKind::BcbToggle};
        ev.payload = api::BcbToggleParams{std::nullopt};
        auto result = p.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::BcbToggling);
        CHECK(ctx->vars.bcb_remaining == 6);
    }

    SECTION("Paused.feed(BcbToggle{count=4}) seeds bcb_remaining=8 and -> BcbToggling") {
        auto ctx = fx.make_ctx();
        Paused p{*ctx};
        Event ev{EventKind::BcbToggle};
        ev.payload = api::BcbToggleParams{4};
        auto result = p.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::BcbToggling);
        CHECK(ctx->vars.bcb_remaining == 8);
    }

    SECTION("Paused.feed(BcbToggle{count=1}) seeds 1 round-trip (2 edges) reaching V2GNegotiating") {
        auto ctx = fx.make_ctx();
        Paused p{*ctx};
        Event ev{EventKind::BcbToggle};
        ev.payload = api::BcbToggleParams{1};
        auto result = p.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::BcbToggling);
        CHECK(ctx->vars.bcb_remaining == 2);

        // Drive the BcbToggling state to verify the count is respected end-to-end.
        BcbToggling s{*ctx};
        s.enter();
        // First deadline produces one edge, bcb_remaining -> 1, still in BcbToggling.
        auto r1 = s.feed(Event{EventKind::StateDeadline});
        CHECK_FALSE(r1.new_state);
        // Second deadline produces the final edge and transitions out.
        auto r2 = s.feed(Event{EventKind::StateDeadline});
        REQUIRE(r2.new_state);
        CHECK(r2.new_state->get_id() == api::FsmState::V2GNegotiating);
    }

    SECTION("Paused.feed(BcbToggle{count=0}) is rejected, stays in Paused") {
        auto ctx = fx.make_ctx();
        Paused p{*ctx};
        Event ev{EventKind::BcbToggle};
        ev.payload = api::BcbToggleParams{0};
        auto result = p.feed(ev);
        CHECK(result.new_state == nullptr);
    }

    SECTION("Paused.feed(BcbToggle{count=1001}) is rejected (above 1000 cap)") {
        // Guards `round_trips * 2` against signed-overflow UB and against a
        // pathologically long toggle loop. The cap is paranoia-only; 1001 is
        // the smallest value that must be rejected.
        auto ctx = fx.make_ctx();
        Paused p{*ctx};
        Event ev{EventKind::BcbToggle};
        ev.payload = api::BcbToggleParams{1001};
        auto result = p.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(ctx->vars.bcb_remaining == 0);
    }
}

// EV-initiated pause then resume, driven through the real fsm::v2 driver so
// every transition's enter()/leave() runs as in production.
//
// The proven EvManager resume sequence is
//   iso_start_bcb_toggle 3; iso_wait_pwm_is_running; iso_start_v2g_session ...;
//   iso_wait_pwr_ready; iso_draw_power_regulated ...
// i.e. after the BCB wake-up the EV WAITS for the SECC to re-apply the CP PWM
// (PWM-is-running) before it re-issues start_charging. EvSimulator must mirror
// this: V2GNegotiating reached via a resume must NOT call iso_start_charging
// (nor arm the 60 s deadline) until a PWM-running BspMeasurement arrives.
// Issuing start_charging immediately races the SECC's wake-up and is the source
// of the unstable resume (start_charging dropped -> no IsoPowerReady -> 60 s
// V2G timeout).
TEST_CASE("EvSimulator resume waits for PWM-running before iso_start_charging", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::AcIso2);
    ensure_session(*ctx).payment = api::PaymentOption::ExternalPayment;
    ctx->vars.three_phases = true;
    ctx->vars.departure_time_s = 7200;
    ctx->vars.e_amount_wh = 25000;

    // Mid-session: root at Charging, pause, then resume into BcbToggling.
    fsm::v2::FSM<StateBase> fsm{std::make_unique<Charging>(*ctx)};
    REQUIRE(fsm.get_current_state_id() == api::FsmState::Charging);
    fsm.feed(Event{EventKind::PauseSession});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::Paused);

    fsm.feed(Event{EventKind::ResumeSession});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::BcbToggling);
    fx.mocks.iso.clear();
    fx.timer.clear();

    // Drive the six BCB edges; the last reaches V2GNegotiating.
    for (int i = 0; i < 6; ++i) {
        fsm.feed(Event{EventKind::StateDeadline});
    }
    REQUIRE(fsm.get_current_state_id() == api::FsmState::V2GNegotiating);

    // On the resume path V2GNegotiating must defer start_charging until the
    // SECC re-applies the CP PWM. Immediately after the BCB toggle, neither
    // start_charging nor the 60 s deadline should have fired yet.
    CHECK_FALSE(contains_substr(fx.mocks.iso.records, "start_charging"));
    CHECK_FALSE(std::any_of(fx.timer.state_timer_arms.begin(), fx.timer.state_timer_arms.end(),
                            [](std::chrono::milliseconds ms) { return ms == std::chrono::seconds(60); }));

    // The SECC re-applies the CP PWM (PWM-is-running). Now the EV re-issues
    // start_charging with the resumed session parameters and arms the deadline.
    Event pwm{EventKind::BspMeasurement};
    pwm.payload = BspMeasurementPayload{50.0f, std::nullopt, ::types::board_support_common::ProximityPilot{}};
    fsm.feed(pwm);
    REQUIRE(fsm.get_current_state_id() == api::FsmState::V2GNegotiating);
    CHECK(contains_substr(fx.mocks.iso.records, "start_charging(mode=AC_three_phase_core,"));
    CHECK(contains_substr(fx.mocks.iso.records, "departure_time=7200"));
    CHECK(contains_substr(fx.mocks.iso.records, "e_amount=25000"));
    CHECK(std::any_of(fx.timer.state_timer_arms.begin(), fx.timer.state_timer_arms.end(),
                      [](std::chrono::milliseconds ms) { return ms == std::chrono::seconds(60); }));

    // SECC signals power-ready: the EV re-enters the charge loop.
    fx.mocks.bsp.clear();
    fsm.feed(Event{EventKind::IsoPowerReady});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::Charging);
    CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=C)"));
    CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=true)"));
}
