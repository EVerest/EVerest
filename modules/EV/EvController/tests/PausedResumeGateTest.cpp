// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/FsmContext.hpp"
#include "../main/StateBase.hpp"
#include "../main/states/BcbToggling.hpp"
#include "../main/states/Charging.hpp"
#include "../main/states/Paused.hpp"
#include "../main/states/V2GNegotiating.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <everest/util/fsm/fsm.hpp>
#include <everest_api_types/ev_controller/codec.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_controller;

// An explicit BcbToggle (the smoke-test resume path) must honor the same
// iso_session_active gate as ResumeSession. Waking the SECC with BCB edges while
// the paused V2G session is still tearing down lets the previous session's
// lagging SessionStop clobber the freshly re-established link, so an explicit
// BcbToggle that arrives while the session is live must DEFER until
// IsoV2GFinished, just like an implicit ResumeSession.
TEST_CASE("EvController BcbToggle defers while V2G session live", "[evctl][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.iso_session_active = true;

    Paused p{*ctx};
    Event ev{EventKind::BcbToggle};
    ev.payload = api::BcbToggleParams{3};
    auto result = p.feed(ev);

    // Deferred: stay in Paused, mark the resume pending, do not seed bcb_remaining
    // yet (it is released into bcb_remaining only when the session finishes).
    CHECK(result.new_state == nullptr);
    CHECK(ctx->vars.bcb_remaining == 0);
}

// A deferred explicit BcbToggle remembers its round-trip count (bcb_pending =
// 2 * count) and releases it into bcb_remaining when IsoV2GFinished arrives,
// so the user-requested count survives the defer.
TEST_CASE("EvController deferred BcbToggle releases its count on IsoV2GFinished", "[evctl][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.iso_session_active = true;

    Paused p{*ctx};
    Event ev{EventKind::BcbToggle};
    ev.payload = api::BcbToggleParams{4};
    auto deferred = p.feed(ev);
    REQUIRE(deferred.new_state == nullptr);

    // The paused session finishes (apply_passthrough_vars clears the flag
    // pre-feed; emulated here), then IsoV2GFinished releases the deferred toggle.
    ctx->vars.iso_session_active = false;
    auto resumed = p.feed(Event{EventKind::IsoV2GFinished});
    REQUIRE(resumed.new_state);
    CHECK(resumed.new_state->get_id() == api::FsmState::BcbToggling);
    CHECK(ctx->vars.bcb_remaining == 8); // 2 * 4
}

// A deferred implicit ResumeSession releases the default 6 BCB edges into
// bcb_remaining when IsoV2GFinished arrives.
TEST_CASE("EvController deferred ResumeSession releases 6 edges on IsoV2GFinished", "[evctl][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.iso_session_active = true;

    Paused p{*ctx};
    auto deferred = p.feed(Event{EventKind::ResumeSession});
    REQUIRE(deferred.new_state == nullptr);

    ctx->vars.iso_session_active = false;
    auto resumed = p.feed(Event{EventKind::IsoV2GFinished});
    REQUIRE(resumed.new_state);
    CHECK(resumed.new_state->get_id() == api::FsmState::BcbToggling);
    CHECK(ctx->vars.bcb_remaining == 6);
}

// If the ISO15118_ev module never publishes v2g_session_finished (abnormal teardown), a deferred
// resume cannot wait forever for IsoV2GFinished: the short fallback timer armed
// on defer fires a StateDeadline, and with the resume still pending the EV does a
// best-effort resume into BcbToggling rather than giving up into Stopping.
TEST_CASE("EvController deferred resume falls back to BcbToggling on StateDeadline", "[evctl][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.iso_session_active = true;

    Paused p{*ctx};
    auto deferred = p.feed(Event{EventKind::ResumeSession});
    REQUIRE(deferred.new_state == nullptr);

    // No IsoV2GFinished arrives; the fallback deadline fires while the resume is
    // still pending. Best-effort: resume into BcbToggling, not Stopping.
    auto fallback = p.feed(Event{EventKind::StateDeadline});
    REQUIRE(fallback.new_state);
    CHECK(fallback.new_state->get_id() == api::FsmState::BcbToggling);
    CHECK(ctx->vars.bcb_remaining == 6);
}

// Regression guard: an idle pause (no resume requested) that hits its deadline
// still ends the session by transitioning to Stopping. The fallback only
// re-routes a deadline when a resume is actually pending.
TEST_CASE("EvController idle Paused StateDeadline still stops the session", "[evctl][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);

    Paused p{*ctx};
    auto result = p.feed(Event{EventKind::StateDeadline});
    REQUIRE(result.new_state);
    CHECK(result.new_state->get_id() == api::FsmState::Stopping);
}

// An ISO 15118-20 charger pause keeps the link matched. Once the paused session
// is over, the EV settles for 2 s, waits for running PWM and starts a new
// session, with no BCB toggle and no SLAC, as EvManager's
// arm_resume_after_charger_pause (car_simulation.cpp:694-708).
TEST_CASE("EvController resumes a charger pause on the PWM wake-up", "[evctl][group2][charger_pause]") {
    TestFixture fx;
    const auto has = [](const std::vector<std::string>& records, const std::string& r) {
        return std::find(records.begin(), records.end(), r) != records.end();
    };
    const auto pause_from_charger = [&](api::ChargeMode mode) {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, mode);
        ctx->vars.iso_session_active = true;
        Charging c{*ctx};
        auto paused = c.feed(Event{EventKind::IsoPauseFromCharger}).new_state;
        REQUIRE(paused);
        REQUIRE(paused->get_id() == api::FsmState::Paused);
        paused->enter();
        return std::make_pair(std::move(ctx), std::move(paused));
    };
    // The runtime records every duty in vars before the state sees it.
    const auto measure = [](FsmContext& ctx, StateBase& s, float duty) {
        ctx.vars.pwm_duty_cycle = duty;
        BspMeasurementPayload m;
        m.cp_pwm_duty_cycle = duty;
        return s.feed(Event{m}).new_state;
    };
    const auto session_ends = [&](FsmContext& ctx, StateBase& s) {
        ctx.vars.iso_session_active = false;
        fx.timer.state_timer_arms.clear();
        CHECK(s.feed(Event{EventKind::IsoV2GFinished}).new_state == nullptr);
        CHECK(fx.timer.state_timer_arms == std::vector<std::chrono::milliseconds>{std::chrono::seconds{2}});
    };

    SECTION("PWM still running when the session ends resumes after 2 s, with no gap") {
        auto [ctx, paused] = pause_from_charger(api::ChargeMode::DcIsoD20);
        CHECK_FALSE(has(fx.mocks.iso.records, "pause_charging()"));
        CHECK(has(fx.mocks.bsp.records, "allow_power_on(value=false)"));
        // Running PWM before the session has ended starts nothing.
        CHECK(measure(*ctx, *paused, 5) == nullptr);
        session_ends(*ctx, *paused);
        CHECK(measure(*ctx, *paused, 5) == nullptr);
        fx.mocks.iso.records.clear();
        fx.mocks.slac.records.clear();
        auto next = paused->feed(Event{EventKind::StateDeadline}).new_state;
        REQUIRE(next);
        CHECK(next->get_id() == api::FsmState::V2GNegotiating);
        next->enter();
        REQUIRE_FALSE(fx.mocks.iso.records.empty());
        CHECK(fx.mocks.iso.records.back().rfind("start_charging(", 0) == 0);
        CHECK(fx.mocks.slac.records.empty());
    }

    SECTION("PWM off again before the session ends waits for it to run") {
        auto [ctx, paused] = pause_from_charger(api::ChargeMode::DcIsoD20);
        CHECK(measure(*ctx, *paused, 100) == nullptr);
        CHECK(measure(*ctx, *paused, 5) == nullptr);
        CHECK(measure(*ctx, *paused, 100) == nullptr);
        session_ends(*ctx, *paused);
        fx.timer.state_timer_arms.clear();
        CHECK(paused->feed(Event{EventKind::StateDeadline}).new_state == nullptr);
        // The pause watchdog runs again while the EV waits for PWM.
        CHECK(fx.timer.state_timer_arms == std::vector<std::chrono::milliseconds>{std::chrono::hours{1}});
        CHECK(measure(*ctx, *paused, 0) == nullptr);
        auto next = measure(*ctx, *paused, 5);
        REQUIRE(next);
        CHECK(next->get_id() == api::FsmState::V2GNegotiating);
    }

    SECTION("A pause entered after the session ended settles at once") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIsoD20);
        Paused paused{*ctx, true};
        paused.enter();
        CHECK(fx.timer.state_timer_arms.back() == std::chrono::seconds{2});
        ctx->vars.pwm_duty_cycle = 5;
        auto next = paused.feed(Event{EventKind::StateDeadline}).new_state;
        REQUIRE(next);
        CHECK(next->get_id() == api::FsmState::V2GNegotiating);
    }

    SECTION("A second session end does not restart the settle; the watchdog still stops") {
        auto [ctx, paused] = pause_from_charger(api::ChargeMode::DcIsoD20);
        CHECK(measure(*ctx, *paused, 100) == nullptr);
        session_ends(*ctx, *paused);
        CHECK(paused->feed(Event{EventKind::StateDeadline}).new_state == nullptr);
        fx.timer.state_timer_arms.clear();
        CHECK(paused->feed(Event{EventKind::IsoV2GFinished}).new_state == nullptr);
        CHECK(fx.timer.state_timer_arms.empty());
        auto next = paused->feed(Event{EventKind::StateDeadline}).new_state;
        REQUIRE(next);
        CHECK(next->get_id() == api::FsmState::Stopping);
    }

    SECTION("Any running PWM wakes, nominal for DC and 5 % for AC") {
        for (const auto [mode, duty] :
             {std::pair{api::ChargeMode::DcIsoD20, 50.0f}, std::pair{api::ChargeMode::AcIsoD20, 5.0f},
              std::pair{api::ChargeMode::AcIsoD20, 50.0f}}) {
            CAPTURE(static_cast<int>(mode), duty);
            auto [ctx, paused] = pause_from_charger(mode);
            CHECK(measure(*ctx, *paused, 100) == nullptr);
            session_ends(*ctx, *paused);
            CHECK(paused->feed(Event{EventKind::StateDeadline}).new_state == nullptr);
            auto next = measure(*ctx, *paused, duty);
            REQUIRE(next);
            CHECK(next->get_id() == api::FsmState::V2GNegotiating);
        }
    }

    SECTION("A stop while waiting for PWM stops the peer and holds in Plugged") {
        auto [ctx, paused] = pause_from_charger(api::ChargeMode::DcIsoD20);
        session_ends(*ctx, *paused);
        CHECK(paused->feed(Event{EventKind::StateDeadline}).new_state == nullptr);
        fx.mocks.iso.records.clear();
        auto next = paused->feed(Event{EventKind::StopSession}).new_state;
        REQUIRE(next);
        CHECK(next->get_id() == api::FsmState::Plugged);
        CHECK(fx.mocks.iso.records == std::vector<std::string>{"stop_charging()"});
        CHECK(ctx->vars.hold_session);
    }

    SECTION("An EV pause still asks the peer to pause and does not wake on PWM") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIsoD20);
        ctx->vars.iso_session_active = true;
        Charging c{*ctx};
        auto paused = c.feed(Event{EventKind::PauseSession}).new_state;
        REQUIRE(paused);
        paused->enter();
        CHECK(has(fx.mocks.iso.records, "pause_charging()"));
        ctx->vars.iso_session_active = false;
        CHECK(paused->feed(Event{EventKind::IsoV2GFinished}).new_state == nullptr);
        CHECK(measure(*ctx, *paused, 100) == nullptr);
        CHECK(measure(*ctx, *paused, 5) == nullptr);
    }
}

namespace {

constexpr auto kX1Wait = Paused::kChargerX1Wait;
constexpr auto kFallback = Paused::kDeferredResumeFallback;
constexpr auto kWatchdog = std::chrono::hours{1};
const std::string kPendingReason{"resume already pending"};

// An EV pause of a live ISO session at 5 % PWM, driven through the real FSM so
// every enter() and leave() runs. The runtime records every duty in vars
// before the state sees it, and clears iso_session_active before it feeds
// IsoV2GFinished; both are emulated here.
struct EvPauseRig {
    TestFixture fx;
    std::unique_ptr<FsmContext> ctx;
    std::unique_ptr<fsm::v2::FSM<StateBase>> fsm;

    explicit EvPauseRig(api::ChargeMode mode) : ctx(fx.make_ctx()) {
        set_mode(*ctx, mode);
        ensure_session(*ctx).payment = api::PaymentOption::ExternalPayment;
        pause();
    }
    // A new EV pause of a live session on the same context.
    void pause() {
        ctx->vars.iso_session_active = true;
        ctx->vars.pwm_duty_cycle = 5;
        fsm = std::make_unique<fsm::v2::FSM<StateBase>>(std::make_unique<Charging>(*ctx));
        fsm->feed(Event{EventKind::PauseSession});
        REQUIRE(state() == api::FsmState::Paused);
        fx.timer.clear();
        fx.mocks.iso.clear();
        fx.sink.clear();
    }
    api::FsmState state() const {
        return fsm->get_current_state_id();
    }
    void feed(Event ev) {
        fsm->feed(std::move(ev));
    }
    void measure(float duty) {
        ctx->vars.pwm_duty_cycle = duty;
        feed(Event{BspMeasurementPayload{duty, std::nullopt, ::types::board_support_common::ProximityPilot{}}});
    }
    void session_ends() {
        ctx->vars.iso_session_active = false;
        feed(Event{EventKind::IsoV2GFinished});
    }
    void toggle(int32_t round_trips) {
        Event ev{EventKind::BcbToggle};
        ev.payload = api::BcbToggleParams{round_trips};
        feed(ev);
    }
    std::vector<std::chrono::milliseconds> arms() const {
        return fx.timer.state_timer_arms;
    }
    std::optional<api::CommandAck> last_ack() const {
        const auto topic = fx.topics.everest_to_extern("command_ack");
        if (!topic_recorded(fx.sink, topic)) {
            return std::nullopt;
        }
        return api::deserialize<api::CommandAck>(last_payload_for(fx.sink, topic));
    }
};

using Arms = std::vector<std::chrono::milliseconds>;

} // namespace

// After SessionStop(Pause) the SECC keeps its 5 % oscillator for the retain
// time ([V2G-DC-968]) and then goes to X1. A resume must wake the charger in
// X1 and start only on the PWM it re-applies: the retain PWM is the paused
// session's, and a session started on it lands on the charger's X1.
TEST_CASE("EvController EV pause resume starts only on PWM after the charger's X1", "[evctl][group2]") {
    EvPauseRig rig{api::ChargeMode::DcIso2};
    rig.feed(Event{EventKind::ResumeSession});
    rig.session_ends();

    SECTION("The retain PWM never starts the session") {
        for (int i = 0; i < 6 && rig.state() != api::FsmState::V2GNegotiating; ++i) {
            rig.measure(5);
            if (rig.state() == api::FsmState::BcbToggling) {
                rig.feed(Event{EventKind::StateDeadline});
            }
        }
        rig.measure(5);
        CHECK_FALSE(contains_substr(rig.fx.mocks.iso.records, "start_charging"));
        CHECK(rig.state() == api::FsmState::Paused);
    }

    SECTION("X1 releases the wake-up and the re-applied PWM starts the session") {
        REQUIRE(rig.state() == api::FsmState::Paused);
        rig.measure(100);
        REQUIRE(rig.state() == api::FsmState::BcbToggling);
        for (int i = 0; i < 6; ++i) {
            rig.feed(Event{EventKind::StateDeadline});
        }
        REQUIRE(rig.state() == api::FsmState::V2GNegotiating);
        CHECK_FALSE(contains_substr(rig.fx.mocks.iso.records, "start_charging"));
        rig.measure(5);
        CHECK(contains_substr(rig.fx.mocks.iso.records, "start_charging"));
    }

    SECTION("A charger that keeps its PWM is woken best-effort when the bound passes") {
        REQUIRE(rig.state() == api::FsmState::Paused);
        rig.feed(Event{EventKind::StateDeadline});
        REQUIRE(rig.state() == api::FsmState::BcbToggling);
        CHECK(rig.ctx->vars.bcb_remaining == 6);
    }
}

// The wait for X1 is bounded once, from the end of the paused session, never
// from each resume: the whole wait never exceeds 15 s after the session end,
// and a resume that misses the session end altogether waits no longer either.
TEST_CASE("EvController EV pause X1 bound runs from the session end", "[evctl][group2]") {
    EvPauseRig rig{api::ChargeMode::DcIso2};

    SECTION("The session end arms the bound and a later resume arms nothing") {
        rig.session_ends();
        CHECK(rig.arms() == Arms{kX1Wait});
        rig.measure(5);
        rig.feed(Event{EventKind::ResumeSession});
        CHECK(rig.state() == api::FsmState::Paused);
        CHECK(rig.arms() == Arms{kX1Wait});
        rig.feed(Event{EventKind::StateDeadline});
        CHECK(rig.state() == api::FsmState::BcbToggling);
    }

    SECTION("A resume after the bound passed wakes at once") {
        rig.session_ends();
        rig.feed(Event{EventKind::StateDeadline});
        REQUIRE(rig.state() == api::FsmState::Paused);
        CHECK(rig.arms() == Arms{kX1Wait, kWatchdog});
        rig.measure(5);
        rig.feed(Event{EventKind::ResumeSession});
        CHECK(rig.state() == api::FsmState::BcbToggling);
    }

    SECTION("X1 seen with no resume ends the bound and keeps the watchdog") {
        rig.session_ends();
        rig.measure(100);
        CHECK(rig.arms() == Arms{kX1Wait, kWatchdog});
        rig.feed(Event{EventKind::ResumeSession});
        CHECK(rig.state() == api::FsmState::BcbToggling);
    }

    SECTION("A resume whose session never reports its end wakes at the fallback") {
        rig.feed(Event{EventKind::ResumeSession});
        CHECK(rig.arms() == Arms{kFallback});
        rig.feed(Event{EventKind::StateDeadline});
        CHECK(rig.state() == api::FsmState::BcbToggling);
        CHECK(rig.arms() == Arms{kFallback, std::chrono::milliseconds{250}});
    }
}

// One rule for a second resume in both waits: a Rejected ack, with no re-arm
// and no change to the edge count of the resume already pending.
TEST_CASE("EvController a second resume is rejected while one is pending", "[evctl][group2]") {
    EvPauseRig rig{api::ChargeMode::DcIso2};
    rig.feed(Event{EventKind::ResumeSession});
    const auto expect_rejected = [&](const std::string& command) {
        const auto ack = rig.last_ack();
        REQUIRE(ack.has_value());
        CHECK(ack->command == command);
        CHECK(ack->status == api::CommandAckStatus::Rejected);
        CHECK(ack->reason == std::optional<std::string>{kPendingReason});
    };

    SECTION("While the paused session is live") {
        rig.toggle(2);
        expect_rejected("bcb_toggle");
        rig.feed(Event{EventKind::ResumeSession});
        expect_rejected("resume_session");
        CHECK(rig.arms() == Arms{kFallback});
        rig.measure(100);
        rig.session_ends();
        REQUIRE(rig.state() == api::FsmState::BcbToggling);
        CHECK(rig.ctx->vars.bcb_remaining == 6);
    }

    SECTION("While waiting for X1") {
        rig.session_ends();
        REQUIRE(rig.state() == api::FsmState::Paused);
        rig.fx.timer.clear();
        rig.feed(Event{EventKind::ResumeSession});
        expect_rejected("resume_session");
        rig.toggle(4);
        expect_rejected("bcb_toggle");
        CHECK(rig.arms().empty());
        rig.measure(100);
        REQUIRE(rig.state() == api::FsmState::BcbToggling);
        CHECK(rig.ctx->vars.bcb_remaining == 6);
    }
}

// Leaving Paused during the X1 wait drops the resume with the state: the next
// EV pause starts with no resume pending and no edge count.
TEST_CASE("EvController leaving Paused during the X1 wait drops the resume", "[evctl][group2]") {
    EvPauseRig rig{api::ChargeMode::DcIso2};
    rig.feed(Event{EventKind::ResumeSession});
    rig.session_ends();
    REQUIRE(rig.state() == api::FsmState::Paused);

    SECTION("stop_session") {
        rig.feed(Event{EventKind::StopSession});
        CHECK(rig.state() == api::FsmState::Plugged);
    }
    SECTION("disable") {
        rig.feed(Event{EventKind::Disable});
        CHECK(rig.state() == api::FsmState::Disabled);
    }
    SECTION("unplug") {
        rig.feed(pilot_input(::types::board_support_common::Event::Disconnected));
        CHECK(rig.state() == api::FsmState::Unplugged);
    }
    rig.ctx->vars.bcb_remaining = 0;
    rig.pause();
    rig.session_ends();
    rig.measure(100);
    CHECK(rig.state() == api::FsmState::Paused);
    CHECK(rig.ctx->vars.bcb_remaining == 0);
    rig.toggle(1);
    CHECK_FALSE(rig.last_ack().has_value());
    REQUIRE(rig.state() == api::FsmState::BcbToggling);
    CHECK(rig.ctx->vars.bcb_remaining == 2);
}

// AC ISO pauses wait for X1 the same way: AcIso2 re-matches SLAC after the
// wake-up, and ISO 15118-20 AC drops CP to B on entry while the session is live.
TEST_CASE("EvController AC EV pause resumes wait for X1", "[evctl][group2]") {
    SECTION("AcIso2 re-matches SLAC and starts on the re-applied PWM") {
        EvPauseRig rig{api::ChargeMode::AcIso2};
        rig.ctx->vars.slac_unmatched = true;
        rig.feed(Event{EventKind::ResumeSession});
        rig.session_ends();
        rig.measure(5);
        REQUIRE(rig.state() == api::FsmState::Paused);
        rig.measure(100);
        REQUIRE(rig.state() == api::FsmState::BcbToggling);
        for (int i = 0; i < 6; ++i) {
            rig.feed(Event{EventKind::StateDeadline});
        }
        REQUIRE(rig.state() == api::FsmState::SlacMatching);
        rig.feed(Event{SlacStatePayload{"MATCHED"}});
        REQUIRE(rig.state() == api::FsmState::V2GNegotiating);
        CHECK_FALSE(contains_substr(rig.fx.mocks.iso.records, "start_charging"));
        rig.measure(5);
        CHECK(contains_substr(rig.fx.mocks.iso.records, "start_charging"));
    }

    SECTION("AcIsoD20 waits although CP went to B at once") {
        EvPauseRig rig{api::ChargeMode::AcIsoD20};
        CHECK(contains_substr(rig.fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));
        rig.feed(Event{EventKind::ResumeSession});
        rig.session_ends();
        rig.measure(5);
        REQUIRE(rig.state() == api::FsmState::Paused);
        rig.measure(100);
        CHECK(rig.state() == api::FsmState::BcbToggling);
    }
}

// A BcbToggle after the paused session ended waits for X1 as well, and keeps its
// edge count. A pause without a live ISO session sent no SessionStop, so there is
// no retain PWM to wait out.
TEST_CASE("EvController BcbToggle after an EV pause waits for X1 with its count", "[evctl][group2]") {
    SECTION("Paused session") {
        EvPauseRig rig{api::ChargeMode::DcIsoD20};
        rig.session_ends();
        rig.toggle(2);
        REQUIRE(rig.state() == api::FsmState::Paused);
        rig.measure(100);
        REQUIRE(rig.state() == api::FsmState::BcbToggling);
        CHECK(rig.ctx->vars.bcb_remaining == 4);
    }

    SECTION("No session") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIsoD20);
        ctx->vars.pwm_duty_cycle = 5;
        Paused p{*ctx};
        p.enter();
        Event toggle{EventKind::BcbToggle};
        toggle.payload = api::BcbToggleParams{2};
        auto next = p.feed(toggle).new_state;
        REQUIRE(next);
        CHECK(next->get_id() == api::FsmState::BcbToggling);
        CHECK(ctx->vars.bcb_remaining == 4);
    }
}
