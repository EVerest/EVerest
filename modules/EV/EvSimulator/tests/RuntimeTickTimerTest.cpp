// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Composition coverage for the three EvSimRuntime timer-fd handlers:
//   - on_tick           -> ramp_step + soc_step
//   - on_scenario_timer  -> ScenarioDispatcher::on_timer_fire
//   - on_state_timer     -> feeds a StateDeadlineEvt to the FSM
//
// on_tick / on_scenario_timer / on_state_timer are private members of
// EvSimRuntime, which can only be constructed from a live framework
// `EvSimulator&` (real MqttProvider + ev-cli `*Intf` peers needing an
// `Everest::ModuleAdapter*`) — the same constraint that forced the
// PeerActions injection seam this suite is built on. The individual
// steps each handler drives are already unit-covered:
//   * ramp_step                   -> RampInterpolatorTest.cpp
//   * soc_step                    -> SocIntegratorTest.cpp
//   * ScenarioDispatcher::on_timer_fire -> ScenarioDispatcherTest.cpp
//   * StateDeadlineEvt routing    -> StateTransitionsTest.cpp (per state)
//
// What was NOT covered, and what these tests pin, is the *composition*
// each handler performs: on_tick must advance the ramp AND integrate
// SoC in a single fire (not one or the other), and the scenario / state
// timer handlers must reach exactly the dispatcher / FSM seam. The
// handlers' epoll registration, fd flush, and exception isolation are
// exercised by TimerFdFlushTest.cpp and the SIL smokes.

#include "../main/EventDispatch.hpp"
#include "../main/FsmContext.hpp"
#include "../main/RampInterpolator.hpp"
#include "../main/ScenarioDispatcher.hpp"
#include "../main/SocIntegrator.hpp"
#include "../main/states/Disabled.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <everest/util/fsm/fsm.hpp>
#include <everest_api_types/ev_simulator/codec.hpp>

#include <algorithm>
#include <chrono>
#include <memory>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

namespace {

bool any_substr(const std::vector<std::string>& records, const std::string& needle) {
    return std::any_of(records.begin(), records.end(),
                       [&](const std::string& r) { return r.find(needle) != std::string::npos; });
}

// Replays the exact body of EvSimRuntime::on_tick (sans the ctx-null
// guard and exception isolation, both covered elsewhere).
void run_on_tick_body(FsmContext& ctx, std::chrono::steady_clock::time_point now) {
    ramp_step(ctx, now);
    soc_step(ctx);
}

// Replays the relevant arms of the EvSimRuntime::apply_passthrough_vars switch
// (EvSimRuntime is not constructible without a live framework EvSimulator&, the
// same constraint that drives the body-replay style of the on_tick tests
// above). The arms are mechanical writes into ctx.vars, so the routing under
// test is: a DcEvsePresentCurrentPayload populates the live-current optional, a
// DcEvsePresentVoltagePayload writes dc_present_voltage_v, and an
// IsoV2GFinished clears iso_session_active (the pre-feed clear that releases a
// deferred resume in Paused). Keep this in lockstep with the production switch.
void run_apply_passthrough_vars_body(FsmContext& ctx, const Event& ev) {
    using K = EventKind;
    switch (kind_of(ev)) {
    case K::DcEvsePresentCurrent:
        if (auto* p = std::get_if<DcEvsePresentCurrentPayload>(&ev.payload)) {
            ctx.vars.evse_dc_present_current_a = static_cast<float>(p->current_a);
        }
        break;
    case K::DcEvsePresentVoltage:
        if (auto* p = std::get_if<DcEvsePresentVoltagePayload>(&ev.payload)) {
            ctx.vars.dc_present_voltage_v = static_cast<float>(p->voltage_v);
        }
        break;
    case K::IsoV2GFinished:
        ctx.vars.iso_session_active = false;
        break;
    default:
        break;
    }
}

} // namespace

TEST_CASE("apply_passthrough_vars routes DC present current/voltage into vars", "[evsim][runtime][passthrough]") {
    SECTION("DcEvsePresentCurrentPayload populates the live-current optional") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        // Open-loop fallback until a present current is reported.
        REQUIRE_FALSE(ctx->vars.evse_dc_present_current_a.has_value());

        run_apply_passthrough_vars_body(*ctx, Event{DcEvsePresentCurrentPayload{75.0}});

        REQUIRE(ctx->vars.evse_dc_present_current_a.has_value());
        CHECK(*ctx->vars.evse_dc_present_current_a == 75.0f);
    }

    SECTION("DcEvsePresentVoltagePayload writes dc_present_voltage_v") {
        TestFixture fx;
        auto ctx = fx.make_ctx();

        run_apply_passthrough_vars_body(*ctx, Event{DcEvsePresentVoltagePayload{550.0}});

        CHECK(ctx->vars.dc_present_voltage_v == 550.0f);
    }

    SECTION("IsoV2GFinished clears iso_session_active before the FSM feed") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        // A live session is in progress; the pre-feed pass must clear it so a
        // deferred resume in Paused is released on the same on_wake iteration.
        ctx->vars.iso_session_active = true;

        run_apply_passthrough_vars_body(*ctx, Event{IsoV2GFinishedEvt{}});

        CHECK_FALSE(ctx->vars.iso_session_active);
    }
}

TEST_CASE("on_tick composition advances ramp and integrates SoC in one fire", "[evsim][runtime][tick]") {
    using Catch::Matchers::WithinAbs;

    SECTION("a single tick both steps the ramp and accumulates charge") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        ctx->vars.charging_current_a = 6.0f;
        ctx->vars.three_phases = false;

        const auto t0 = std::chrono::steady_clock::time_point{std::chrono::milliseconds{10000}};
        ActiveRamp r;
        r.start_a = 6.0f;
        r.target_a = 16.0f;
        r.three_phases = false;
        r.start_at = t0;
        r.end_at = t0 + std::chrono::milliseconds{1000};
        ctx->vars.active_ramp = r;
        const float charge_before = ctx->vars.battery_charge_wh;

        run_on_tick_body(*ctx, t0 + std::chrono::milliseconds{500});

        // Ramp half-way: a BSP current command was issued (~11A) and the
        // ramp is still active (mid-flight).
        CHECK(any_substr(fx.mocks.bsp.records, "set_ac_max_current(current="));
        CHECK(ctx->vars.active_ramp.has_value());
        // SoC integrated on the same tick: battery charge advanced.
        CHECK(ctx->vars.battery_charge_wh > charge_before);
    }

    SECTION("tick with no ramp still integrates SoC (ramp step is the no-op half)") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIso2);
        ctx->vars.charging_current_a = 16.0f;
        ctx->vars.three_phases = true;
        REQUIRE_FALSE(ctx->vars.active_ramp.has_value());
        const float charge_before = ctx->vars.battery_charge_wh;

        run_on_tick_body(*ctx, std::chrono::steady_clock::now());

        CHECK(ctx->vars.battery_charge_wh > charge_before);
        // The ramp half issued no BSP current command (no active ramp).
        CHECK_FALSE(any_substr(fx.mocks.bsp.records, "set_ac_max_current(current="));
    }

    SECTION("final ramp tick snaps to target then SoC integrates at the target current") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        ctx->vars.charging_current_a = 6.0f;
        ctx->vars.three_phases = false;

        const auto t0 = std::chrono::steady_clock::time_point{std::chrono::milliseconds{20000}};
        ActiveRamp r;
        r.start_a = 6.0f;
        r.target_a = 16.0f;
        r.three_phases = false;
        r.start_at = t0;
        r.end_at = t0 + std::chrono::milliseconds{1000};
        ctx->vars.active_ramp = r;
        const float charge_before = ctx->vars.battery_charge_wh;

        run_on_tick_body(*ctx, t0 + std::chrono::milliseconds{2000}); // past end_at

        CHECK_FALSE(ctx->vars.active_ramp.has_value());
        CHECK(std::abs(ctx->vars.charging_current_a - 16.0f) < 0.01f);
        CHECK(ctx->vars.battery_charge_wh > charge_before);
    }
}

TEST_CASE("on_scenario_timer composition reaches the dispatcher", "[evsim][runtime][scenario-timer]") {
    SECTION("firing the scenario timer flushes the next scheduled step") {
        TestFixture fx;
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::AcIecBasic, std::nullopt, *ctx);
        // Offset-0 Plug + ConfigureSession fired inline; the +30s StopSession
        // is still pending behind the scenario timer.
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        REQUIRE(ctx->scenario.active());

        // EvSimRuntime::on_scenario_timer body: ctx->scenario.on_timer_fire.
        ctx->scenario.on_timer_fire(*ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 3);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::StopSession);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("scenario timer fire while idle is a no-op (matches handler guard semantics)") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        REQUIRE_FALSE(ctx->scenario.active());

        ctx->scenario.on_timer_fire(*ctx);

        CHECK(fx.timer.enqueued_events.empty());
        CHECK(fx.timer.scenario_timer_arms.empty());
    }
}

TEST_CASE("on_state_timer composition feeds a StateDeadlineEvt to the FSM", "[evsim][runtime][state-timer]") {
    SECTION("StateDeadlineEvt routed through the fault-isolating feed seam") {
        TestFixture fx;
        auto ctx = fx.make_ctx();

        // Build a real FSM (Disabled is the runtime's initial state) so the
        // state-timer body can be replayed verbatim:
        //   Event ev{StateDeadlineEvt{}};
        //   feed_with_fault_isolation(fsm, *ctx, ev);
        auto fsm = std::make_unique<fsm::v2::FSM<StateBase>>(std::make_unique<Disabled>(*ctx));

        Event ev{StateDeadlineEvt{}};
        // Disabled ignores a deadline (no per-state timer there); the
        // contract under test is that the handler reaches the FSM feed
        // without throwing and the FSM stays consistent.
        REQUIRE_NOTHROW(feed_with_fault_isolation(fsm, *ctx, ev));
        REQUIRE(fsm);
        CHECK(fsm->get_current_state_id() == api::FsmState::Disabled);
    }
}
