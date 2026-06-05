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
// until a PWM-running BspMeasurement arrives. Issuing start_charging
// immediately races the SECC's wake-up and is the source of the unstable
// resume (start_charging dropped -> no IsoPowerReady -> 60 s V2G timeout). The
// bounding deadline IS armed on entry, so a SECC that never re-applies PWM
// faults with V2GTimeout instead of hanging.
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

    // An AC pause tears the SLAC link down (D-LINK_TERMINATE -> UNMATCHED), so
    // the resume re-matches SLAC. apply_passthrough_vars sets slac_unmatched from
    // that event in production; set it directly here.
    ctx->vars.slac_unmatched = true;

    fsm.feed(Event{EventKind::ResumeSession});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::BcbToggling);
    fx.mocks.iso.clear();
    fx.timer.clear();

    // Drive the six BCB edges; the last routes into SlacMatching because the AC
    // pause tore the link down. The SECC re-matches and the EV advances to
    // V2GNegotiating, carrying the deferred-start contract through the detour.
    for (int i = 0; i < 6; ++i) {
        fsm.feed(Event{EventKind::StateDeadline});
    }
    REQUIRE(fsm.get_current_state_id() == api::FsmState::SlacMatching);
    fsm.feed(Event{SlacStatePayload{"MATCHED"}});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::V2GNegotiating);

    // On the resume path V2GNegotiating must defer start_charging until the
    // SECC re-applies the CP PWM: immediately after the BCB toggle start_charging
    // has NOT fired. The bounding deadline IS armed, though, so a SECC that
    // never re-applies PWM faults (V2GTimeout) instead of hanging.
    CHECK_FALSE(contains_substr(fx.mocks.iso.records, "start_charging"));
    CHECK(std::any_of(fx.timer.state_timer_arms.begin(), fx.timer.state_timer_arms.end(),
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

// DC variant of the resume test: on a DC resume the SECC re-runs CableCheck and
// waits for the EV contactor to close within its
// cable_check_relays_closed_timeout_s window. But the contactor must NOT close
// the instant V2GNegotiating is entered: the SECC has just been woken by the BCB
// edges and has not yet switched its CP PWM off / back on for the pause window.
// While the SECC's PWM is off it runs a 6 s C1 timer ("EV did not go back to
// state B after PWM was switched off") that force-powers-off under load if it
// sees the EV still at CP=C. So the EV holds CP=B through the pause + BCB wake +
// SLAC re-match AND through the PWM-running BspMeasurement (which only re-issues
// start_charging); it asserts CP=C only at ev_power_ready (ISO_POWER_READY),
// mirroring EvManager which holds CP=B until pwr_ready and then closes the
// contactor for the resume CableCheck. dc_power_on then enters Charging.
TEST_CASE("EvSimulator DC resume closes contactor at IsoPowerReady, charges on IsoDcPowerOn", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ensure_session(*ctx).payment = api::PaymentOption::ExternalPayment;
    ctx->vars.departure_time_s = 7200;
    ctx->vars.e_amount_wh = 25000;

    // Mid-session: root at Charging, pause, then resume into BcbToggling.
    fsm::v2::FSM<StateBase> fsm{std::make_unique<Charging>(*ctx)};
    REQUIRE(fsm.get_current_state_id() == api::FsmState::Charging);
    fsm.feed(Event{EventKind::PauseSession});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::Paused);

    fsm.feed(Event{EventKind::ResumeSession});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::BcbToggling);
    fx.timer.clear();

    // A DC pause is a clean D-LINK_PAUSE: the SLAC link survives (slac_unmatched
    // stays false), so the resume routes straight to V2GNegotiating without
    // re-matching SLAC. Drive five BCB edges (still in BcbToggling), clear the
    // mocks, then the sixth edge transitions to V2GNegotiating so the assertions
    // below see only its enter().
    REQUIRE_FALSE(ctx->vars.slac_unmatched);
    for (int i = 0; i < 5; ++i) {
        fsm.feed(Event{EventKind::StateDeadline});
    }
    REQUIRE(fsm.get_current_state_id() == api::FsmState::BcbToggling);
    fx.mocks.bsp.clear();
    fx.mocks.iso.clear();
    fsm.feed(Event{EventKind::StateDeadline});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::V2GNegotiating);

    // On entry the EV must hold CP=B, NOT CP=C: while the SECC's pause-window PWM
    // is off, presenting CP=C trips its 6 s C1 "powering off under load" timer and
    // it force-powers-off, so the resume hangs. start_charging is deferred until a
    // PWM-running BspMeasurement; CP=C is deferred until ev_power_ready.
    CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=C)"));
    CHECK_FALSE(contains_substr(fx.mocks.iso.records, "start_charging"));

    // The SECC re-applies the charging PWM (PWM-is-running). The EV re-issues
    // start_charging, but STILL holds CP=B: the contactor must stay open until
    // the SECC signals power-ready, mirroring EvManager (CP=B until pwr_ready).
    Event pwm{EventKind::BspMeasurement};
    pwm.payload = BspMeasurementPayload{50.0f, std::nullopt, ::types::board_support_common::ProximityPilot{}};
    fsm.feed(pwm);
    REQUIRE(fsm.get_current_state_id() == api::FsmState::V2GNegotiating);
    CHECK(contains_substr(fx.mocks.iso.records, "start_charging"));
    CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=C)"));

    // ev_power_ready (ISO_POWER_READY): the EV closes the contactor by presenting
    // CP=C and holds it through CableCheck/PreCharge -- but does NOT enter
    // Charging yet (premature would drop CP mid-CableCheck on pause).
    fsm.feed(Event{EventKind::IsoPowerReady});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::V2GNegotiating);
    REQUIRE(index_of_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=C)") >= 0);
    CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=true)"));

    // dc_power_on (PreCharge complete): NOW the EV enters the charge loop.
    fsm.feed(Event{EventKind::IsoDcPowerOn});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::Charging);
}

// AC EV-initiated resume must re-establish SLAC before re-negotiating. On AC the
// pause produces a D-LINK_TERMINATE (SLAC UNMATCHED); the resume path
// Paused -> BcbToggling -> V2GNegotiating then bypasses SlacMatching, so
// dlink_ready stays false on the SECC side and josev discards the EV's SDP
// requests -- the session hangs at PrepareCharging. EvManager re-matched SLAC on
// resume (car_simulation.cpp iso_wait_slac_matched: on UNMATCHED, reset +
// trigger_matching). EvSimulator must mirror that: when the SLAC link was torn
// down (vars.slac_unmatched), the resume routes through SlacMatching (whose
// enter calls slac_trigger_matching) before reaching V2GNegotiating.
TEST_CASE("EvSimulator AC resume re-matches SLAC when link was torn down", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::AcIso2);
    ensure_session(*ctx).payment = api::PaymentOption::ExternalPayment;
    ctx->vars.three_phases = true;

    // Mid-session: root at Charging, pause, then resume into BcbToggling.
    fsm::v2::FSM<StateBase> fsm{std::make_unique<Charging>(*ctx)};
    REQUIRE(fsm.get_current_state_id() == api::FsmState::Charging);
    fsm.feed(Event{EventKind::PauseSession});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::Paused);

    // The AC pause tears the SLAC link down: the SECC emits a D-LINK_TERMINATE,
    // observed as a SLAC UNMATCHED state. EvSimRuntime::apply_passthrough_vars
    // sets vars.slac_unmatched from that event before the FSM feed; emulate that
    // passthrough here (the FSM is driven directly in this test).
    ctx->vars.slac_unmatched = true;

    fsm.feed(Event{EventKind::ResumeSession});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::BcbToggling);
    fx.mocks.slac.clear();
    fx.timer.clear();

    // Drive the six BCB edges; the last must route into SlacMatching (not
    // straight to V2GNegotiating) because the link was torn down.
    for (int i = 0; i < 6; ++i) {
        fsm.feed(Event{EventKind::StateDeadline});
    }
    REQUIRE(fsm.get_current_state_id() == api::FsmState::SlacMatching);
    // SlacMatching::enter re-triggers matching, mirroring EvManager's resume.
    REQUIRE(index_of_substr(fx.mocks.slac.records, "trigger_matching") >= 0);

    // On the SECC re-matching SLAC, the EV proceeds to V2GNegotiating. The
    // resume_awaiting_pwm contract from the BcbToggling resume must survive the
    // SlacMatching detour, so start_charging is still deferred until PWM-running.
    fx.mocks.iso.clear();
    fsm.feed(Event{SlacStatePayload{"MATCHED"}});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::V2GNegotiating);
    CHECK_FALSE(contains_substr(fx.mocks.iso.records, "start_charging"));
    // The torn-down flag is cleared once SLAC re-matches so a later resume
    // without a teardown takes the direct path.
    CHECK_FALSE(ctx->vars.slac_unmatched);
}

// A DC EV-initiated resume must NOT re-match SLAC. The DC pause is a clean
// D-LINK_PAUSE that leaves both sides MATCHED (slac_unmatched stays false). If
// the EV re-triggered matching it would go MATCHING while the SECC stays MATCHED,
// and the SlacSimulator co-match -- which needs BOTH sides MATCHING -- would
// deadlock until SlacTimeout. The SECC honors the resume on the surviving link
// (Car Paused -> PrepareCharging on the BCB wake-up), so on a DC ISO resume with
// an intact link the EV goes straight to V2GNegotiating.
TEST_CASE("EvSimulator DC resume with intact SLAC goes straight to V2GNegotiating", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ensure_session(*ctx).payment = api::PaymentOption::ExternalPayment;
    ctx->vars.departure_time_s = 7200;
    ctx->vars.e_amount_wh = 25000;

    // Mid-session: root at Charging, pause, then resume into BcbToggling.
    fsm::v2::FSM<StateBase> fsm{std::make_unique<Charging>(*ctx)};
    REQUIRE(fsm.get_current_state_id() == api::FsmState::Charging);
    fsm.feed(Event{EventKind::PauseSession});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::Paused);

    fsm.feed(Event{EventKind::ResumeSession});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::BcbToggling);
    // The DC pause leaves the EV-side SLAC link intact: no UNMATCHED arrives, so
    // slac_unmatched is false on the resume.
    REQUIRE_FALSE(ctx->vars.slac_unmatched);
    fx.mocks.slac.clear();
    fx.mocks.iso.clear();
    fx.timer.clear();

    // Drive the six BCB edges; the last routes directly to V2GNegotiating (no
    // SlacMatching detour) and never re-triggers SLAC.
    for (int i = 0; i < 6; ++i) {
        fsm.feed(Event{EventKind::StateDeadline});
    }
    REQUIRE(fsm.get_current_state_id() == api::FsmState::V2GNegotiating);
    CHECK_FALSE(contains_substr(fx.mocks.slac.records, "trigger_matching"));
    // The resume_awaiting_pwm contract holds: start_charging is deferred until a
    // PWM-running BspMeasurement.
    CHECK_FALSE(contains_substr(fx.mocks.iso.records, "start_charging"));
}

// Josev runs exactly one V2G comm session per start_charging and publishes
// v2g_session_finished (-> IsoV2GFinished) when each session's loop returns,
// including on a pause. If the EV begins its resume (BCB wake-up + re-SLAC)
// while the paused session is still tearing down, the previous session's
// lagging SessionStop reaches the SECC after the link is re-established and
// clobbers it: the SECC drops to D-LINK_PAUSE + PWM off and never re-applies
// the charging PWM, so V2GNegotiating's PWM gate never fires start_charging and
// the resume hangs. So a ResumeSession that arrives while the session is still
// live must DEFER until IsoV2GFinished.
TEST_CASE("EvSimulator resume defers BCB toggle until prior V2G session finished", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ensure_session(*ctx).payment = api::PaymentOption::ExternalPayment;
    // A live Josev session is in progress (start_charging was issued, no
    // IsoV2GFinished yet).
    ctx->vars.iso_session_active = true;

    Paused p{*ctx};

    // Resume requested while the paused session is still tearing down: stay in
    // Paused (deferred); do NOT start the BCB toggle yet.
    auto deferred = p.feed(Event{EventKind::ResumeSession});
    CHECK(deferred.new_state == nullptr);
    CHECK(ctx->vars.resume_pending);
    CHECK(ctx->vars.bcb_remaining == 0);

    // The paused session finishes: EvSimRuntime::apply_passthrough_vars clears
    // iso_session_active before the FSM feed (emulated here), then
    // IsoV2GFinished releases the deferred resume into BcbToggling.
    ctx->vars.iso_session_active = false;
    auto resumed = p.feed(Event{EventKind::IsoV2GFinished});
    REQUIRE(resumed.new_state);
    CHECK(resumed.new_state->get_id() == api::FsmState::BcbToggling);
    CHECK(ctx->vars.bcb_remaining == 6);
    CHECK_FALSE(ctx->vars.resume_pending);
}

// IsoV2GFinished without a pending resume keeps the EV in Paused, and applies
// the CP=B drop that enter() defers while a session is live. A later
// ResumeSession then takes the immediate path (iso_session_active is false).
TEST_CASE("EvSimulator Paused IsoV2GFinished without pending resume holds in Paused", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ensure_session(*ctx).payment = api::PaymentOption::ExternalPayment;
    ctx->vars.iso_session_active = false; // already finished

    Paused p{*ctx};
    auto finished = p.feed(Event{EventKind::IsoV2GFinished});
    CHECK(finished.new_state == nullptr);
    CHECK_FALSE(ctx->vars.resume_pending);
    // The deferred paused CP state is applied once the session is gone.
    CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));

    // The subsequent resume proceeds immediately (no session to wait on).
    auto resumed = p.feed(Event{EventKind::ResumeSession});
    REQUIRE(resumed.new_state);
    CHECK(resumed.new_state->get_id() == api::FsmState::BcbToggling);
    CHECK(ctx->vars.bcb_remaining == 6);
}

// An EV-initiated pause that begins while the HLC session is still live must NOT
// drop CP to B yet. Presenting CP=B mid-teardown makes the SECC abort the link
// with D-LINK_ERROR instead of a clean D-LINK_PAUSE; on D-LINK_ERROR the SECC
// never runs current_demand_finished, so its over-voltage monitor stays armed
// and the resume CableCheck (run at the EV's max voltage, which equals the OVM
// error limit) trips MREC5OverVoltage and the resume never reaches Charging. The
// EV therefore holds CP at C through the teardown and drops it only once the
// session is gone (IsoV2GFinished). Power-off and the graceful pause still fire
// immediately. This is the EvSimulator-side hardening of the EvManager pause.
TEST_CASE("EvSimulator Paused holds CP at C until the live V2G session finishes", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ensure_session(*ctx).payment = api::PaymentOption::ExternalPayment;
    ctx->vars.iso_session_active = true; // live Josev session in progress

    Paused p{*ctx};
    p.enter();

    // The graceful teardown and contactor open fire immediately...
    CHECK(contains_substr(fx.mocks.iso.records, "pause_charging()"));
    CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=false)"));
    // ...but CP is held at C: dropping to B now would provoke D-LINK_ERROR.
    CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));

    // The paused session finishes tearing down (apply_passthrough_vars clears
    // iso_session_active before the FSM feed; emulated here). Now CP drops to B.
    ctx->vars.iso_session_active = false;
    fx.mocks.bsp.clear();
    auto done = p.feed(Event{EventKind::IsoV2GFinished});
    CHECK(done.new_state == nullptr); // no resume requested -> stay Paused
    CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));
}

// With no live V2G session (AcIec, or a session that already finished before the
// pause), there is nothing to tear down gracefully, so Paused.enter drops CP to
// B immediately rather than waiting for an IsoV2GFinished that will never arrive.
TEST_CASE("EvSimulator Paused drops CP immediately when no V2G session is live", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.iso_session_active = false;

    Paused p{*ctx};
    p.enter();
    CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));
    CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=false)"));
}

// Companion to the resume test: if the SECC never re-applies PWM after the BCB
// wake-up, the bounding deadline (armed on the deferred V2GNegotiating::enter)
// must surface a V2GTimeout Faulted state rather than hang the resume forever.
TEST_CASE("EvSimulator resume faults with V2GTimeout if PWM never returns", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::AcIso2);
    ensure_session(*ctx).payment = api::PaymentOption::ExternalPayment;
    ctx->vars.three_phases = true;

    fsm::v2::FSM<StateBase> fsm{std::make_unique<Charging>(*ctx)};
    fsm.feed(Event{EventKind::PauseSession});
    // An AC pause tears the SLAC link down, so the resume re-matches SLAC.
    ctx->vars.slac_unmatched = true;
    fsm.feed(Event{EventKind::ResumeSession});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::BcbToggling);
    fx.mocks.iso.clear();
    for (int i = 0; i < 6; ++i) {
        fsm.feed(Event{EventKind::StateDeadline});
    }
    // The AC pause tore the link down, so the resume re-matches SLAC first;
    // advance through the detour.
    REQUIRE(fsm.get_current_state_id() == api::FsmState::SlacMatching);
    fsm.feed(Event{SlacStatePayload{"MATCHED"}});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::V2GNegotiating);

    // No PWM-running BspMeasurement arrives; the bounding deadline fires.
    CHECK_FALSE(contains_substr(fx.mocks.iso.records, "start_charging"));
    fsm.feed(Event{EventKind::StateDeadline});
    REQUIRE(fsm.get_current_state_id() == api::FsmState::Faulted);
    REQUIRE(ctx->vars.last_fault.has_value());
    CHECK(ctx->vars.last_fault->type == api::FaultType::V2GTimeout);
}
