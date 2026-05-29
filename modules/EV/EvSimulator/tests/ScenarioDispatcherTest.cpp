// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/ScenarioDispatcher.hpp"
#include "../main/FsmContext.hpp"
#include "../main/states/Plugged.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <everest_api_types/ev_simulator/codec.hpp>

#include <algorithm>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

namespace {

int count_kind(const std::vector<Event>& evs, EventKind k) {
    return static_cast<int>(std::count_if(evs.begin(), evs.end(), [k](const Event& e) { return e.kind == k; }));
}

int index_of_kind(const std::vector<Event>& evs, EventKind k) {
    for (size_t i = 0; i < evs.size(); ++i) {
        if (evs[i].kind == k) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

TEST_CASE("ScenarioDispatcher presets", "[evsim][scenario]") {
    TestFixture fx;
    const auto ack_topic = fx.topics.everest_to_extern("command_ack");

    SECTION("AcIecBasic enqueues 3 steps in order") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::AcIecBasic, *ctx);

        // Two immediate enqueues at offset 0: Plug then StartSession.
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);

        // Scenario timer armed for the StopSession step at +30s.
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(fx.timer.scenario_timer_arms[0] == std::chrono::seconds(30));
        CHECK(ctx->scenario.active());

        // Firing the scenario timer enqueues the StopSession step.
        ctx->scenario.on_timer_fire(*ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 3);
        CHECK(fx.timer.enqueued_events[2].kind == EventKind::StopSession);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("AcIsoBasic enqueues 3 steps with 60s terminator") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::AcIsoBasic, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(fx.timer.scenario_timer_arms[0] == std::chrono::seconds(60));

        ctx->scenario.on_timer_fire(*ctx);
        REQUIRE(fx.timer.enqueued_events.size() == 3);
        CHECK(fx.timer.enqueued_events[2].kind == EventKind::StopSession);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("DcIsoBasic single-step at offset 0 does not arm timer") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::DcIsoBasic, *ctx);

        // Only Plug + StartSession at offset 0; no follow-up step.
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);
        CHECK(fx.timer.scenario_timer_arms.empty());
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("reset() clears pending step-list") {
        auto ctx = fx.make_ctx();
        ctx->scenario.start(api::ScenarioName::AcIecBasic, *ctx);
        REQUIRE(ctx->scenario.active());

        ctx->scenario.reset();

        CHECK_FALSE(ctx->scenario.active());
        // Subsequent timer fire is a no-op (no crash, no enqueue).
        auto before = fx.timer.enqueued_events.size();
        ctx->scenario.on_timer_fire(*ctx);
        CHECK(fx.timer.enqueued_events.size() == before);
    }

    SECTION("on_timer_fire with empty step-list is a no-op") {
        auto ctx = fx.make_ctx();
        // No start() — dispatcher is idle.
        CHECK_FALSE(ctx->scenario.active());
        ctx->scenario.on_timer_fire(*ctx);
        CHECK(fx.timer.enqueued_events.empty());
        CHECK(fx.timer.scenario_timer_arms.empty());
    }

    SECTION("DcIsoBpt threads bpt params through start_session") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::DcIsoBpt, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(fx.timer.scenario_timer_arms[0] == std::chrono::seconds(180));

        const auto& start_ev = fx.timer.enqueued_events[1];
        REQUIRE(std::holds_alternative<api::StartSessionParams>(start_ev.payload));
        const auto& params = std::get<api::StartSessionParams>(start_ev.payload);
        CHECK(params.mode == api::ChargeMode::DcIsoD20);
        REQUIRE(params.bpt.has_value());
        CHECK(params.bpt->discharge_max_current_limit == 50.0f);
        CHECK(params.bpt->discharge_max_power_limit == 11000.0f);
        CHECK(params.bpt->discharge_target_current == 30.0f);
        CHECK(params.bpt->discharge_minimal_soc == 20.0f);
        CHECK_FALSE(params.mcs.has_value());

        ctx->scenario.on_timer_fire(*ctx); // StopSession at +180s
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +185s

        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(fx.timer.enqueued_events[2].kind == EventKind::StopSession);
        CHECK(fx.timer.enqueued_events[3].kind == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());

        auto ack_payload = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                                        [&](const auto& kv) { return kv.first == ack_topic; });
        CHECK(ack_payload == fx.sink.records.end());
    }

    SECTION("DcIsoMcs sets mcs presence flag on start_session") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::DcIsoMcs, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(fx.timer.scenario_timer_arms[0] == std::chrono::seconds(180));

        const auto& start_ev = fx.timer.enqueued_events[1];
        REQUIRE(std::holds_alternative<api::StartSessionParams>(start_ev.payload));
        const auto& params = std::get<api::StartSessionParams>(start_ev.payload);
        CHECK(params.mode == api::ChargeMode::DcIsoD20);
        REQUIRE(params.mcs.has_value());
        CHECK_FALSE(params.bpt.has_value());

        ctx->scenario.on_timer_fire(*ctx); // StopSession at +180s
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +185s

        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(fx.timer.enqueued_events[2].kind == EventKind::StopSession);
        CHECK(fx.timer.enqueued_events[3].kind == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());

        auto ack_payload = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                                        [&](const auto& kv) { return kv.first == ack_topic; });
        CHECK(ack_payload == fx.sink.records.end());
    }

    SECTION("AcIecPauseResume emits 6-step script") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::AcIecPauseResume, *ctx);

        // Offset 0: Plug + StartSession fire immediately.
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(fx.timer.scenario_timer_arms[0] == std::chrono::seconds(30));

        // Drain remaining steps via scenario timer fires.
        ctx->scenario.on_timer_fire(*ctx); // PauseSession at +30s
        ctx->scenario.on_timer_fire(*ctx); // ResumeSession at +60s
        ctx->scenario.on_timer_fire(*ctx); // StopSession at +120s
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +125s

        REQUIRE(fx.timer.enqueued_events.size() == 6);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);
        CHECK(fx.timer.enqueued_events[2].kind == EventKind::PauseSession);
        CHECK(fx.timer.enqueued_events[3].kind == EventKind::ResumeSession);
        CHECK(fx.timer.enqueued_events[4].kind == EventKind::StopSession);
        CHECK(fx.timer.enqueued_events[5].kind == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("DcIsoPauseResume reaches charging via Paused-Resume sequence") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::DcIsoPauseResume, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(fx.timer.scenario_timer_arms[0] == std::chrono::seconds(45));

        ctx->scenario.on_timer_fire(*ctx); // PauseSession at +45s
        ctx->scenario.on_timer_fire(*ctx); // ResumeSession at +75s
        ctx->scenario.on_timer_fire(*ctx); // StopSession at +180s
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +185s

        REQUIRE(fx.timer.enqueued_events.size() == 6);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);
        CHECK(fx.timer.enqueued_events[2].kind == EventKind::PauseSession);
        CHECK(fx.timer.enqueued_events[3].kind == EventKind::ResumeSession);
        CHECK(fx.timer.enqueued_events[4].kind == EventKind::StopSession);
        CHECK(fx.timer.enqueued_events[5].kind == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("DiodeFailSmoke schedules fault then clear") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::DiodeFailSmoke, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(fx.timer.scenario_timer_arms[0] == std::chrono::seconds(15));

        ctx->scenario.on_timer_fire(*ctx); // InjectFault at +15s
        ctx->scenario.on_timer_fire(*ctx); // ClearFault at +20s

        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(fx.timer.enqueued_events[2].kind == EventKind::InjectFault);
        CHECK(fx.timer.enqueued_events[3].kind == EventKind::ClearFault);
        CHECK(index_of_kind(fx.timer.enqueued_events, EventKind::InjectFault) >
              index_of_kind(fx.timer.enqueued_events, EventKind::StartSession));

        // InjectFault payload carries the DiodeFail type.
        const auto& inject_ev = fx.timer.enqueued_events[2];
        REQUIRE(std::holds_alternative<api::InjectFaultParams>(inject_ev.payload));
        CHECK(std::get<api::InjectFaultParams>(inject_ev.payload).type == api::FaultType::DiodeFail);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("AcIsoD20Basic dispatches without command_ack rejection") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::AcIsoD20Basic, *ctx);

        // Offset 0: Plug + StartSession fire immediately.
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(fx.timer.scenario_timer_arms[0] == std::chrono::seconds(120));

        // StartSession payload carries AcIsoD20 with three_phases=true.
        const auto& start_ev = fx.timer.enqueued_events[1];
        REQUIRE(std::holds_alternative<api::StartSessionParams>(start_ev.payload));
        const auto& params = std::get<api::StartSessionParams>(start_ev.payload);
        CHECK(params.mode == api::ChargeMode::AcIsoD20);
        REQUIRE(params.three_phases.has_value());
        CHECK(*params.three_phases == true);

        ctx->scenario.on_timer_fire(*ctx); // StopSession at +120s
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +125s

        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(fx.timer.enqueued_events[2].kind == EventKind::StopSession);
        CHECK(fx.timer.enqueued_events[3].kind == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());

        // No "scenario not implemented in v1" rejection was published.
        auto ack_payload = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                                        [&](const auto& kv) { return kv.first == ack_topic; });
        CHECK(ack_payload == fx.sink.records.end());
    }

    SECTION("DcIsoD20Basic dispatches with mode=DcIsoD20") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::DcIsoD20Basic, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(fx.timer.scenario_timer_arms[0] == std::chrono::seconds(180));

        const auto& start_ev = fx.timer.enqueued_events[1];
        REQUIRE(std::holds_alternative<api::StartSessionParams>(start_ev.payload));
        CHECK(std::get<api::StartSessionParams>(start_ev.payload).mode == api::ChargeMode::DcIsoD20);

        ctx->scenario.on_timer_fire(*ctx); // StopSession at +180s
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +185s

        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);
        CHECK(fx.timer.enqueued_events[2].kind == EventKind::StopSession);
        CHECK(fx.timer.enqueued_events[3].kind == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());

        auto ack_payload = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                                        [&](const auto& kv) { return kv.first == ack_topic; });
        CHECK(ack_payload == fx.sink.records.end());
    }

    SECTION("AcIecRampUp emits 3 curve points at expected offsets") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::AcIecRampUp, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);

        const auto& start_ev = fx.timer.enqueued_events[1];
        REQUIRE(std::holds_alternative<api::StartSessionParams>(start_ev.payload));
        const auto& params = std::get<api::StartSessionParams>(start_ev.payload);
        CHECK(params.mode == api::ChargeMode::AcIec);
        REQUIRE(params.curve.has_value());
        REQUIRE(params.curve->points.size() == 3);
        CHECK_FALSE(params.curve->loop);

        const auto& p0 = params.curve->points[0];
        CHECK(p0.t_offset_ms == 2000);
        CHECK(p0.current_a == 8.0f);
        CHECK(p0.three_phases == true);
        REQUIRE(p0.ramp_ms.has_value());
        CHECK(*p0.ramp_ms == 2000);

        const auto& p1 = params.curve->points[1];
        CHECK(p1.t_offset_ms == 8000);
        CHECK(p1.current_a == 16.0f);
        CHECK(p1.three_phases == true);
        REQUIRE(p1.ramp_ms.has_value());
        CHECK(*p1.ramp_ms == 2000);

        const auto& p2 = params.curve->points[2];
        CHECK(p2.t_offset_ms == 20000);
        CHECK(p2.current_a == 32.0f);
        CHECK(p2.three_phases == true);
        REQUIRE(p2.ramp_ms.has_value());
        CHECK(*p2.ramp_ms == 4000);
    }

    SECTION("DcIsoTaper emits 4 curve points starting at 100A") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::DcIsoTaper, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);

        const auto& start_ev = fx.timer.enqueued_events[1];
        REQUIRE(std::holds_alternative<api::StartSessionParams>(start_ev.payload));
        const auto& params = std::get<api::StartSessionParams>(start_ev.payload);
        CHECK(params.mode == api::ChargeMode::DcIso2);
        REQUIRE(params.curve.has_value());
        REQUIRE(params.curve->points.size() == 4);
        CHECK_FALSE(params.curve->loop);

        const auto& p0 = params.curve->points[0];
        CHECK(p0.t_offset_ms == 0);
        CHECK(p0.current_a == 100.0f);
        CHECK(p0.three_phases == false);
        CHECK_FALSE(p0.ramp_ms.has_value());

        const auto& p1 = params.curve->points[1];
        CHECK(p1.t_offset_ms == 30000);
        CHECK(p1.current_a == 50.0f);
        REQUIRE(p1.ramp_ms.has_value());
        CHECK(*p1.ramp_ms == 10000);

        const auto& p2 = params.curve->points[2];
        CHECK(p2.t_offset_ms == 60000);
        CHECK(p2.current_a == 20.0f);
        REQUIRE(p2.ramp_ms.has_value());
        CHECK(*p2.ramp_ms == 10000);

        const auto& p3 = params.curve->points[3];
        CHECK(p3.t_offset_ms == 90000);
        CHECK(p3.current_a == 5.0f);
        REQUIRE(p3.ramp_ms.has_value());
        CHECK(*p3.ramp_ms == 5000);
    }

    SECTION("start() while another scenario active clears prior steps") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::AcIecBasic, *ctx);
        REQUIRE(ctx->scenario.active());
        fx.timer.clear();

        ctx->scenario.start(api::ScenarioName::DcIsoBasic, *ctx);

        // DcIsoBasic has no terminator: the prior AcIecBasic 30s arm should
        // not survive into the new scenario.
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::Plug);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::StartSession);
        CHECK_FALSE(ctx->scenario.active());

        // Cross-check kinds: both events are session-level (no leftover Stop).
        CHECK(count_kind(fx.timer.enqueued_events, EventKind::StopSession) == 0);
        CHECK(index_of_kind(fx.timer.enqueued_events, EventKind::Plug) == 0);
    }
}

TEST_CASE("ScenarioDispatcher ChargingCurve handling", "[evsim][scenario][curve]") {
    TestFixture fx;
    const auto ack_topic = fx.topics.everest_to_extern("command_ack");

    auto make_curve = [](std::vector<int32_t> offsets, bool loop) {
        api::ChargingCurve c;
        c.loop = loop;
        for (auto o : offsets) {
            api::CurvePoint p;
            p.t_offset_ms = o;
            p.current_a = 10.0f + static_cast<float>(o) / 1000.0f;
            p.three_phases = true;
            c.points.push_back(p);
        }
        return c;
    };

    SECTION("StartSession with curve enqueues N SetChargingCurrent events at right offsets") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};

        Event ev{EventKind::StartSession};
        api::StartSessionParams params{};
        params.mode = api::ChargeMode::AcIec;
        params.charging_current_a = 16.0f;
        params.three_phases = true;
        params.curve = make_curve({1000, 5000, 10000}, false);
        ev.payload = params;
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);

        // Three SetChargingCurrent steps were appended to the dispatcher.
        REQUIRE(ctx->scenario.active());
        // Fire timer 3 times to drain the curve steps.
        ctx->scenario.on_timer_fire(*ctx); // +1000ms
        ctx->scenario.on_timer_fire(*ctx); // +5000ms
        ctx->scenario.on_timer_fire(*ctx); // +10000ms

        REQUIRE(fx.timer.enqueued_events.size() == 3);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::SetChargingCurrent);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::SetChargingCurrent);
        CHECK(fx.timer.enqueued_events[2].kind == EventKind::SetChargingCurrent);

        // First arm is for first curve point at +1000ms (rounds up to 1s).
        REQUIRE(fx.timer.scenario_timer_arms.size() >= 1);
        CHECK(fx.timer.scenario_timer_arms[0] == std::chrono::seconds(1));

        // SetChargingCurrent payload carries current_a from curve.
        const auto& p0 = std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[0].payload);
        CHECK(p0.current_a == 11.0f); // 10 + 1000/1000
        CHECK(p0.three_phases == true);
        const auto& p2 = std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[2].payload);
        CHECK(p2.current_a == 20.0f); // 10 + 10000/1000

        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("StartSession with empty curve points rejected via command_ack") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};

        Event ev{EventKind::StartSession};
        api::StartSessionParams params{};
        params.mode = api::ChargeMode::AcIec;
        params.curve = api::ChargingCurve{}; // empty points
        ev.payload = params;
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK_FALSE(ctx->vars.charge_mode.has_value());

        // Locate ack record manually since this test file does not include
        // the StateTransitions helper namespace.
        auto it = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                               [&](const auto& kv) { return kv.first == ack_topic; });
        REQUIRE(it != fx.sink.records.end());
        auto ack = api::deserialize<api::CommandAck>(it->second);
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "curve has empty points");
    }

    SECTION("StartSession with non-monotonic curve rejected via command_ack") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};

        Event ev{EventKind::StartSession};
        api::StartSessionParams params{};
        params.mode = api::ChargeMode::AcIec;
        params.curve = make_curve({1000, 5000, 5000}, false); // not strictly monotonic
        ev.payload = params;
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK_FALSE(ctx->vars.charge_mode.has_value());

        auto it = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                               [&](const auto& kv) { return kv.first == ack_topic; });
        REQUIRE(it != fx.sink.records.end());
        auto ack = api::deserialize<api::CommandAck>(it->second);
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "curve points not monotonic");
    }

    SECTION("ChargingCurve.loop=true rewinds curve steps after last point") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};

        Event ev{EventKind::StartSession};
        api::StartSessionParams params{};
        params.mode = api::ChargeMode::AcIec;
        params.curve = make_curve({1000, 3000}, true); // 2-point looping curve
        ev.payload = params;
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        REQUIRE(ctx->scenario.active());

        // Drain two cycles' worth of curve steps (2 points x 2 cycles = 4 fires).
        ctx->scenario.on_timer_fire(*ctx); // cycle 1, point 0 (current=11)
        ctx->scenario.on_timer_fire(*ctx); // cycle 1, point 1 (current=13)
        ctx->scenario.on_timer_fire(*ctx); // cycle 2, point 0 (current=11) -- rewound
        ctx->scenario.on_timer_fire(*ctx); // cycle 2, point 1 (current=13)

        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(fx.timer.enqueued_events[0].kind == EventKind::SetChargingCurrent);
        CHECK(fx.timer.enqueued_events[1].kind == EventKind::SetChargingCurrent);
        CHECK(fx.timer.enqueued_events[2].kind == EventKind::SetChargingCurrent);
        CHECK(fx.timer.enqueued_events[3].kind == EventKind::SetChargingCurrent);

        const auto& p2 = std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[2].payload);
        const auto& p3 = std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[3].payload);
        // After wrap, the first curve point fires again (current=11), then the second (current=13).
        CHECK(p2.current_a == 11.0f);
        CHECK(p3.current_a == 13.0f);

        // Dispatcher stays active across the wrap.
        CHECK(ctx->scenario.active());
    }
}
