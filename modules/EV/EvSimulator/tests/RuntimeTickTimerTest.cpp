// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Composition coverage for the three EvSimRuntime timer-fd handlers. Each step they
// drive is unit-covered elsewhere; what is pinned here is the composition, above all
// that on_tick advances the ramp AND integrates SoC in a single fire rather than one
// or the other. These call EvSimRuntime::tick_step and ::apply_passthrough_vars, the
// same definitions the runtime's handlers call, so breaking the production composition
// reds these tests. EvSimRuntime itself is not unit-constructible without a live
// framework EvSimulator&, so epoll registration, fd flush and exception isolation stay
// in the SIL smokes.

#include "../main/EvSimRuntime.hpp"
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

} // namespace

TEST_CASE("apply_passthrough_vars routes DC present current/voltage into vars", "[evsim][runtime][passthrough]") {
    SECTION("DcEvsePresentCurrentPayload populates the live-current optional") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        // Open-loop fallback until a present current is reported.
        REQUIRE_FALSE(ctx->vars.evse_dc_present_current_a.has_value());

        EvSimRuntime::apply_passthrough_vars(*ctx, Event{DcEvsePresentCurrentPayload{75.0}});

        REQUIRE(ctx->vars.evse_dc_present_current_a.has_value());
        CHECK(*ctx->vars.evse_dc_present_current_a == 75.0f);
    }

    SECTION("DcEvsePresentVoltagePayload writes dc_present_voltage_v") {
        TestFixture fx;
        auto ctx = fx.make_ctx();

        EvSimRuntime::apply_passthrough_vars(*ctx, Event{DcEvsePresentVoltagePayload{550.0}});

        CHECK(ctx->vars.dc_present_voltage_v == 550.0f);
    }

    SECTION("IsoV2GFinished clears iso_session_active before the FSM feed") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        // A live session is in progress; the pre-feed pass must clear it so a
        // deferred resume in Paused is released on the same on_wake iteration.
        ctx->vars.iso_session_active = true;

        EvSimRuntime::apply_passthrough_vars(*ctx, Event{IsoV2GFinishedEvt{}});

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

        EvSimRuntime::tick_step(*ctx, t0 + std::chrono::milliseconds{500});

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

        EvSimRuntime::tick_step(*ctx, std::chrono::steady_clock::now());

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

        EvSimRuntime::tick_step(*ctx, t0 + std::chrono::milliseconds{2000}); // past end_at

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

// The ISO lifecycle publish is the one behavioural fix this module carries, and the runtime's
// publish path used to sit in no test target at all: deleting the publish outright left every
// unit test green. These pin it against the same definition the runtime calls.
TEST_CASE("publish_passthrough_external publishes an iso_session_event per lifecycle edge",
          "[evsim][runtime][iso-session-event]") {
    using ISEK = api::IsoSessionEventKind;

    // One publish per lifecycle edge, each carrying its own kind. The five listed here are exactly
    // the kinds iso_session_event_kind maps, so a mapping that lost one reds this section.
    const auto publishes_kind = [](Event ev, ISEK expected) {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        const auto topic = fx.topics.everest_to_extern("iso_session_event");

        EvSimRuntime::publish_passthrough_external(*ctx, ev, false);

        REQUIRE(topic_recorded(fx.sink, topic));
        return api::deserialize<api::IsoSessionEvent>(payload_for(fx.sink, topic)).kind == expected;
    };

    SECTION("every mapped lifecycle edge reaches the e2m topic with its own kind") {
        CHECK(publishes_kind(Event{IsoPowerReadyEvt{}}, ISEK::PowerReady));
        CHECK(publishes_kind(Event{IsoDcPowerOnEvt{}}, ISEK::DcPowerOn));
        CHECK(publishes_kind(Event{IsoStopFromChargerEvt{}}, ISEK::StopFromCharger));
        CHECK(publishes_kind(Event{IsoPauseFromChargerEvt{}}, ISEK::PauseFromCharger));
        CHECK(publishes_kind(Event{IsoV2GFinishedEvt{}}, ISEK::V2GFinished));
    }

    SECTION("an unmapped event publishes no iso_session_event") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        const auto topic = fx.topics.everest_to_extern("iso_session_event");

        EvSimRuntime::publish_passthrough_external(*ctx, Event{DcEvsePresentVoltagePayload{400.0}}, false);

        CHECK_FALSE(topic_recorded(fx.sink, topic));
    }

    SECTION("the DC present values ride along when they are known") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        ctx->vars.dc_present_voltage_v = 410.0f;
        ctx->vars.evse_dc_present_current_a = 125.0f;
        const auto topic = fx.topics.everest_to_extern("iso_session_event");

        EvSimRuntime::publish_passthrough_external(*ctx, Event{IsoDcPowerOnEvt{}}, false);

        const auto decoded = api::deserialize<api::IsoSessionEvent>(payload_for(fx.sink, topic));
        REQUIRE(decoded.dc_voltage_v.has_value());
        CHECK(*decoded.dc_voltage_v == 410.0f);
        REQUIRE(decoded.dc_current_a.has_value());
        CHECK(*decoded.dc_current_a == 125.0f);
    }
}
