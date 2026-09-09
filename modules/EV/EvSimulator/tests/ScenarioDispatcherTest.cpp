// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/ScenarioDispatcher.hpp"
#include "../main/FsmContext.hpp"
#include "../main/states/Charging.hpp"
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
    return static_cast<int>(std::count_if(evs.begin(), evs.end(), [k](const Event& e) { return kind_of(e) == k; }));
}

int index_of_kind(const std::vector<Event>& evs, EventKind k) {
    for (size_t i = 0; i < evs.size(); ++i) {
        if (kind_of(evs[i]) == k) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// arm_next computes delay = at - elapsed where `elapsed` is the time between
// start_at_ and now. Allow a small symmetric slack so assertions stay stable
// on busy CI runners regardless of which direction jitter falls.
bool arm_close_to(std::chrono::milliseconds actual, std::chrono::milliseconds expected) {
    constexpr auto slack = std::chrono::milliseconds(50);
    auto diff = actual - expected;
    if (diff < std::chrono::milliseconds(0)) {
        diff = -diff;
    }
    return diff <= slack;
}

} // namespace

TEST_CASE("ScenarioDispatcher presets", "[evsim][scenario]") {
    TestFixture fx;
    const auto ack_topic = fx.topics.everest_to_extern("command_ack");

    SECTION("AcIecBasic enqueues 3 steps in order") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::AcIecBasic, std::nullopt, *ctx);

        // Two immediate enqueues at offset 0: ConfigureSession then Plug.
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);

        // Scenario timer armed for the StopSession step at +30s.
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(arm_close_to(fx.timer.scenario_timer_arms[0], std::chrono::milliseconds(30000)));
        CHECK(ctx->scenario.active());

        // Firing the scenario timer enqueues the StopSession step.
        ctx->scenario.on_timer_fire(*ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 3);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::StopSession);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("AcIsoBasic enqueues 3 steps with 60s terminator") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::AcIsoBasic, std::nullopt, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(arm_close_to(fx.timer.scenario_timer_arms[0], std::chrono::milliseconds(60000)));

        ctx->scenario.on_timer_fire(*ctx);
        REQUIRE(fx.timer.enqueued_events.size() == 3);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::StopSession);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("DcIsoBasic single-step at offset 0 does not arm timer") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::DcIsoBasic, std::nullopt, *ctx);

        // Only ConfigureSession + Plug at offset 0; no follow-up step.
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        CHECK(fx.timer.scenario_timer_arms.empty());
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("reset() clears pending step-list") {
        auto ctx = fx.make_ctx();
        ctx->scenario.start(api::ScenarioName::AcIecBasic, std::nullopt, *ctx);
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

    SECTION("DcIsoBpt threads bpt params through configure_session") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::DcIsoBpt, std::nullopt, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(arm_close_to(fx.timer.scenario_timer_arms[0], std::chrono::milliseconds(180000)));

        const auto& start_ev = fx.timer.enqueued_events[0];
        REQUIRE(std::holds_alternative<api::SessionConfigParams>(start_ev.payload));
        const auto& sp = std::get<api::SessionConfigParams>(start_ev.payload);
        REQUIRE(std::holds_alternative<api::DcIsoD20SessionParams>(sp));
        const auto& params = std::get<api::DcIsoD20SessionParams>(sp);
        REQUIRE(params.bpt.has_value());
        CHECK(params.bpt->discharge_max_current_limit == 50.0f);
        CHECK(params.bpt->discharge_max_power_limit == 11000.0f);
        CHECK(params.bpt->discharge_target_current == 30.0f);
        CHECK(params.bpt->discharge_minimal_soc == 20.0f);
        CHECK_FALSE(params.mcs_enabled);

        ctx->scenario.on_timer_fire(*ctx); // StopSession at +180s
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +185s

        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::StopSession);
        CHECK(kind_of(fx.timer.enqueued_events[3]) == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());

        auto ack_payload = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                                        [&](const auto& kv) { return kv.first == ack_topic; });
        CHECK(ack_payload == fx.sink.records.end());
    }

    SECTION("DcIsoMcs sets mcs presence flag on configure_session") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::DcIsoMcs, std::nullopt, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(arm_close_to(fx.timer.scenario_timer_arms[0], std::chrono::milliseconds(180000)));

        const auto& start_ev = fx.timer.enqueued_events[0];
        REQUIRE(std::holds_alternative<api::SessionConfigParams>(start_ev.payload));
        const auto& sp = std::get<api::SessionConfigParams>(start_ev.payload);
        REQUIRE(std::holds_alternative<api::DcIsoD20SessionParams>(sp));
        const auto& params = std::get<api::DcIsoD20SessionParams>(sp);
        CHECK(params.mcs_enabled);
        CHECK_FALSE(params.bpt.has_value());

        ctx->scenario.on_timer_fire(*ctx); // StopSession at +180s
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +185s

        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::StopSession);
        CHECK(kind_of(fx.timer.enqueued_events[3]) == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());

        auto ack_payload = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                                        [&](const auto& kv) { return kv.first == ack_topic; });
        CHECK(ack_payload == fx.sink.records.end());
    }

    SECTION("AcIecPauseResume emits 6-step script") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::AcIecPauseResume, std::nullopt, *ctx);

        // Offset 0: ConfigureSession + Plug fire immediately.
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(arm_close_to(fx.timer.scenario_timer_arms[0], std::chrono::milliseconds(30000)));

        // Flush remaining steps via scenario timer fires.
        ctx->scenario.on_timer_fire(*ctx); // PauseSession at +30s
        ctx->scenario.on_timer_fire(*ctx); // ResumeSession at +60s
        ctx->scenario.on_timer_fire(*ctx); // StopSession at +120s
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +125s

        REQUIRE(fx.timer.enqueued_events.size() == 6);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::PauseSession);
        CHECK(kind_of(fx.timer.enqueued_events[3]) == EventKind::ResumeSession);
        CHECK(kind_of(fx.timer.enqueued_events[4]) == EventKind::StopSession);
        CHECK(kind_of(fx.timer.enqueued_events[5]) == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("DcIsoPauseResume reaches charging via Paused-Resume sequence") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::DcIsoPauseResume, std::nullopt, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(arm_close_to(fx.timer.scenario_timer_arms[0], std::chrono::milliseconds(45000)));

        ctx->scenario.on_timer_fire(*ctx); // PauseSession at +45s
        ctx->scenario.on_timer_fire(*ctx); // ResumeSession at +75s
        ctx->scenario.on_timer_fire(*ctx); // StopSession at +180s
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +185s

        REQUIRE(fx.timer.enqueued_events.size() == 6);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::PauseSession);
        CHECK(kind_of(fx.timer.enqueued_events[3]) == EventKind::ResumeSession);
        CHECK(kind_of(fx.timer.enqueued_events[4]) == EventKind::StopSession);
        CHECK(kind_of(fx.timer.enqueued_events[5]) == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("DiodeFailSmoke schedules fault then clear") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::DiodeFailSmoke, std::nullopt, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(arm_close_to(fx.timer.scenario_timer_arms[0], std::chrono::milliseconds(15000)));

        ctx->scenario.on_timer_fire(*ctx); // InjectFault at +15s
        ctx->scenario.on_timer_fire(*ctx); // ClearFault at +20s

        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::InjectFault);
        CHECK(kind_of(fx.timer.enqueued_events[3]) == EventKind::ClearFault);
        CHECK(index_of_kind(fx.timer.enqueued_events, EventKind::InjectFault) >
              index_of_kind(fx.timer.enqueued_events, EventKind::ConfigureSession));

        // InjectFault payload carries the DiodeFail type.
        const auto& inject_ev = fx.timer.enqueued_events[2];
        REQUIRE(std::holds_alternative<api::InjectFaultParams>(inject_ev.payload));
        CHECK(std::get<api::InjectFaultParams>(inject_ev.payload).type == api::FaultType::DiodeFail);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("AcIsoD20Basic dispatches without command_ack rejection") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::AcIsoD20Basic, std::nullopt, *ctx);

        // Offset 0: ConfigureSession + Plug fire immediately.
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(arm_close_to(fx.timer.scenario_timer_arms[0], std::chrono::milliseconds(120000)));

        // ConfigureSession payload carries AcIsoD20 with three_phases=true.
        const auto& start_ev = fx.timer.enqueued_events[0];
        REQUIRE(std::holds_alternative<api::SessionConfigParams>(start_ev.payload));
        const auto& sp = std::get<api::SessionConfigParams>(start_ev.payload);
        REQUIRE(std::holds_alternative<api::AcIsoD20SessionParams>(sp));
        const auto& params = std::get<api::AcIsoD20SessionParams>(sp);
        REQUIRE(params.three_phases.has_value());
        CHECK(*params.three_phases == true);

        ctx->scenario.on_timer_fire(*ctx); // StopSession at +120s
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +125s

        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::StopSession);
        CHECK(kind_of(fx.timer.enqueued_events[3]) == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());

        // No "scenario not implemented in v1" rejection was published.
        auto ack_payload = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                                        [&](const auto& kv) { return kv.first == ack_topic; });
        CHECK(ack_payload == fx.sink.records.end());
    }

    SECTION("DcIsoD20Basic dispatches with mode=DcIsoD20") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::DcIsoD20Basic, std::nullopt, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(arm_close_to(fx.timer.scenario_timer_arms[0], std::chrono::milliseconds(180000)));

        const auto& start_ev = fx.timer.enqueued_events[0];
        REQUIRE(std::holds_alternative<api::SessionConfigParams>(start_ev.payload));
        CHECK(api::mode_of(std::get<api::SessionConfigParams>(start_ev.payload)) == api::ChargeMode::DcIsoD20);

        ctx->scenario.on_timer_fire(*ctx); // StopSession at +180s
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +185s

        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::StopSession);
        CHECK(kind_of(fx.timer.enqueued_events[3]) == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());

        auto ack_payload = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                                        [&](const auto& kv) { return kv.first == ack_topic; });
        CHECK(ack_payload == fx.sink.records.end());
    }

    SECTION("AcIecRampUp emits 3 curve points at expected offsets") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::AcIecRampUp, std::nullopt, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);

        const auto& start_ev = fx.timer.enqueued_events[0];
        REQUIRE(std::holds_alternative<api::SessionConfigParams>(start_ev.payload));
        const auto& sp = std::get<api::SessionConfigParams>(start_ev.payload);
        REQUIRE(std::holds_alternative<api::AcIecSessionParams>(sp));
        const auto& params = std::get<api::AcIecSessionParams>(sp);
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

        ctx->scenario.start(api::ScenarioName::DcIsoTaper, std::nullopt, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);

        const auto& start_ev = fx.timer.enqueued_events[0];
        REQUIRE(std::holds_alternative<api::SessionConfigParams>(start_ev.payload));
        const auto& sp = std::get<api::SessionConfigParams>(start_ev.payload);
        REQUIRE(std::holds_alternative<api::DcIso2SessionParams>(sp));
        const auto& params = std::get<api::DcIso2SessionParams>(sp);
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

        ctx->scenario.start(api::ScenarioName::AcIecBasic, std::nullopt, *ctx);
        REQUIRE(ctx->scenario.active());
        fx.timer.clear();

        ctx->scenario.start(api::ScenarioName::DcIsoBasic, std::nullopt, *ctx);

        // DcIsoBasic has no terminator: the prior AcIecBasic 30s arm should
        // not survive into the new scenario.
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        CHECK_FALSE(ctx->scenario.active());

        // Cross-check kinds: both events are session-level (no leftover Stop).
        CHECK(count_kind(fx.timer.enqueued_events, EventKind::StopSession) == 0);
        CHECK(index_of_kind(fx.timer.enqueued_events, EventKind::Plug) == 1);
    }

    SECTION("AcIecPauseResume with all four timing overrides applied") {
        auto ctx = fx.make_ctx();

        api::ScenarioTimingOverrides ov;
        ov.pause_at_ms = 5000;
        ov.resume_at_ms = 10000;
        ov.stop_after_ms = 15000;
        ov.unplug_after_ms = 18000;
        ctx->scenario.start(api::ScenarioName::AcIecPauseResume, ov, *ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(arm_close_to(fx.timer.scenario_timer_arms[0], std::chrono::milliseconds(5000)));

        ctx->scenario.on_timer_fire(*ctx); // PauseSession at +5s
        ctx->scenario.on_timer_fire(*ctx); // ResumeSession at +10s
        ctx->scenario.on_timer_fire(*ctx); // StopSession at +15s
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +18s

        REQUIRE(fx.timer.enqueued_events.size() == 6);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::ConfigureSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Plug);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::PauseSession);
        CHECK(kind_of(fx.timer.enqueued_events[3]) == EventKind::ResumeSession);
        CHECK(kind_of(fx.timer.enqueued_events[4]) == EventKind::StopSession);
        CHECK(kind_of(fx.timer.enqueued_events[5]) == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());

        auto ack = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                                [&](const auto& kv) { return kv.first == ack_topic; });
        CHECK(ack == fx.sink.records.end());
    }

    SECTION("nullopt timing preserves preset defaults") {
        auto ctx = fx.make_ctx();

        ctx->scenario.start(api::ScenarioName::AcIecPauseResume, std::nullopt, *ctx);

        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(arm_close_to(fx.timer.scenario_timer_arms[0], std::chrono::milliseconds(30000)));
        CHECK(ctx->scenario.active());
    }

    SECTION("partial override: only stop_after_ms set keeps pause/resume defaults") {
        auto ctx = fx.make_ctx();

        // Stop override stays between resume default (60s) and unplug
        // default (125s) so the script remains strictly monotonic.
        api::ScenarioTimingOverrides ov;
        ov.stop_after_ms = 90000;
        ctx->scenario.start(api::ScenarioName::AcIecPauseResume, ov, *ctx);

        // Pause default (30s) unchanged.
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(arm_close_to(fx.timer.scenario_timer_arms[0], std::chrono::milliseconds(30000)));

        ctx->scenario.on_timer_fire(*ctx); // PauseSession at +30s (default)
        ctx->scenario.on_timer_fire(*ctx); // ResumeSession at +60s (default)
        ctx->scenario.on_timer_fire(*ctx); // StopSession at +90s (override)
        ctx->scenario.on_timer_fire(*ctx); // Unplug at +125s (default)

        REQUIRE(fx.timer.enqueued_events.size() == 6);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::PauseSession);
        CHECK(kind_of(fx.timer.enqueued_events[3]) == EventKind::ResumeSession);
        CHECK(kind_of(fx.timer.enqueued_events[4]) == EventKind::StopSession);
        CHECK(kind_of(fx.timer.enqueued_events[5]) == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("reject: pause_at_ms not applicable to AcIecBasic") {
        auto ctx = fx.make_ctx();

        api::ScenarioTimingOverrides ov;
        ov.pause_at_ms = 5000;
        ctx->scenario.start(api::ScenarioName::AcIecBasic, ov, *ctx);

        CHECK(fx.timer.enqueued_events.empty());
        CHECK(fx.timer.scenario_timer_arms.empty());
        CHECK_FALSE(ctx->scenario.active());

        auto it = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                               [&](const auto& kv) { return kv.first == ack_topic; });
        REQUIRE(it != fx.sink.records.end());
        auto ack = api::deserialize<api::CommandAck>(it->second);
        REQUIRE(ack.reason.has_value());
        CHECK(ack.reason->find("not applicable") != std::string::npos);
    }

    SECTION("reject: stop_after_ms negative is non-positive") {
        auto ctx = fx.make_ctx();

        api::ScenarioTimingOverrides ov;
        ov.stop_after_ms = -1;
        ctx->scenario.start(api::ScenarioName::AcIecBasic, ov, *ctx);

        CHECK(fx.timer.enqueued_events.empty());
        CHECK_FALSE(ctx->scenario.active());

        auto it = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                               [&](const auto& kv) { return kv.first == ack_topic; });
        REQUIRE(it != fx.sink.records.end());
        auto ack = api::deserialize<api::CommandAck>(it->second);
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "scenario timing must be positive");
    }

    SECTION("reject: resume before pause is non-monotonic") {
        auto ctx = fx.make_ctx();

        api::ScenarioTimingOverrides ov;
        ov.pause_at_ms = 60000;
        ov.resume_at_ms = 30000; // earlier than pause
        ctx->scenario.start(api::ScenarioName::AcIecPauseResume, ov, *ctx);

        CHECK(fx.timer.enqueued_events.empty());
        CHECK_FALSE(ctx->scenario.active());

        auto it = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                               [&](const auto& kv) { return kv.first == ack_topic; });
        REQUIRE(it != fx.sink.records.end());
        auto ack = api::deserialize<api::CommandAck>(it->second);
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "scenario timing not monotonic");
    }

    SECTION("reject: any timing field on DcIsoBasic not applicable") {
        auto ctx = fx.make_ctx();

        api::ScenarioTimingOverrides ov;
        ov.stop_after_ms = 5000;
        ctx->scenario.start(api::ScenarioName::DcIsoBasic, ov, *ctx);

        CHECK(fx.timer.enqueued_events.empty());
        CHECK_FALSE(ctx->scenario.active());

        auto it = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                               [&](const auto& kv) { return kv.first == ack_topic; });
        REQUIRE(it != fx.sink.records.end());
        auto ack = api::deserialize<api::CommandAck>(it->second);
        REQUIRE(ack.reason.has_value());
        CHECK(ack.reason->find("not applicable") != std::string::npos);
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

    SECTION("latched curve enqueues N SetChargingCurrent events at right offsets") {
        auto ctx = fx.make_ctx();
        Plugged plugged{*ctx};

        api::AcIecSessionParams params{};
        params.charging_current_a = 16.0f;
        params.three_phases = true;
        params.curve = make_curve({1000, 5000, 10000}, false);
        ctx->configured_session = api::SessionConfigParams{params};
        auto result = plugged.feed(Event{BeginSessionEvt{}});
        CHECK(result.new_state == nullptr);
        // Plugged stashes the curve; the splice happens on Charging::enter.
        REQUIRE(ctx->vars.pending_curve().has_value());
        CHECK_FALSE(ctx->scenario.active());

        Charging charging{*ctx};
        charging.enter();

        // Three SetChargingCurrent steps were appended to the dispatcher.
        REQUIRE(ctx->scenario.active());
        // Fire timer 3 times to flush the curve steps.
        ctx->scenario.on_timer_fire(*ctx); // +1000ms
        ctx->scenario.on_timer_fire(*ctx); // +5000ms
        ctx->scenario.on_timer_fire(*ctx); // +10000ms

        REQUIRE(fx.timer.enqueued_events.size() == 3);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::SetChargingCurrent);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::SetChargingCurrent);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::SetChargingCurrent);

        // First arm is for first curve point at +1000ms (ms precision).
        REQUIRE(fx.timer.scenario_timer_arms.size() >= 1);
        CHECK(arm_close_to(fx.timer.scenario_timer_arms[0], std::chrono::milliseconds(1000)));

        // SetChargingCurrent payload carries current_a from curve.
        const auto& p0 = std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[0].payload);
        CHECK(p0.current_a == 11.0f); // 10 + 1000/1000
        CHECK(p0.three_phases == true);
        const auto& p2 = std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[2].payload);
        CHECK(p2.current_a == 20.0f); // 10 + 10000/1000

        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("configure_session with non-monotonic curve rejected via command_ack") {
        auto ctx = fx.make_ctx();

        api::AcIecSessionParams params{};
        params.curve = make_curve({1000, 5000, 5000}, false); // not strictly monotonic
        ctx->configure_session(api::SessionConfigParams{params});

        CHECK_FALSE(ctx->configured_session.has_value());
        auto it = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                               [&](const auto& kv) { return kv.first == ack_topic; });
        REQUIRE(it != fx.sink.records.end());
        auto ack = api::deserialize<api::CommandAck>(it->second);
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "curve points not monotonic");
    }

    SECTION("ChargingCurve.loop=true rewinds curve steps after last point") {
        auto ctx = fx.make_ctx();
        Plugged plugged{*ctx};

        api::AcIecSessionParams params{};
        params.curve = make_curve({1000, 3000}, true); // 2-point looping curve
        ctx->configured_session = api::SessionConfigParams{params};
        auto result = plugged.feed(Event{BeginSessionEvt{}});
        CHECK(result.new_state == nullptr);
        // Plugged stashes the curve; the splice happens on Charging::enter.
        REQUIRE(ctx->vars.pending_curve().has_value());

        Charging charging{*ctx};
        charging.enter();
        REQUIRE(ctx->scenario.active());

        // Fire 1: cycle 1, point 0 enqueues current=11.
        ctx->scenario.on_timer_fire(*ctx);
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        CHECK(std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[0].payload).current_a == 11.0f);

        // Fire 2: cycle 1 point 1 enqueues current=13, then the loop rewinds
        // and arm_next fires the rewound first step (current=11) inline since
        // its zero-delay slot would otherwise disarm the timer.
        ctx->scenario.on_timer_fire(*ctx);
        REQUIRE(fx.timer.enqueued_events.size() == 3);
        CHECK(std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[1].payload).current_a == 13.0f);
        CHECK(std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[2].payload).current_a == 11.0f);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::SetChargingCurrent);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::SetChargingCurrent);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::SetChargingCurrent);

        // Dispatcher stays active across the wrap and the post-rewind arm is
        // strictly positive (otherwise the underlying timerfd would disarm).
        CHECK(ctx->scenario.active());
        CHECK(fx.timer.scenario_timer_arms.back() > std::chrono::milliseconds(0));
    }
}

TEST_CASE("ScenarioDispatcher rearm precision and loop disarm", "[evsim][scenario][rearm]") {
    TestFixture fx;

    // Build a 3-step list using append_steps so the dispatcher seeds start_at_
    // to "now" and treats offsets as relative to it. mark_loop(1, 3) marks
    // [Pause, Resume] as the loop body — the offset-0 step fires inline and
    // is outside the loop.
    auto make_three_step_list = []() {
        std::vector<ScenarioStep> steps;
        Event plug;
        plug = Event{EventKind::Plug};
        Event pause;
        pause = Event{EventKind::PauseSession};
        Event resume;
        resume = Event{EventKind::ResumeSession};
        steps.push_back({std::chrono::milliseconds(0), std::move(plug)});
        steps.push_back({std::chrono::milliseconds(100), std::move(pause)});
        steps.push_back({std::chrono::milliseconds(200), std::move(resume)});
        return steps;
    };

    SECTION("Sub-second positive delay arms with millisecond precision") {
        auto ctx = fx.make_ctx();

        std::vector<ScenarioStep> steps;
        Event pause;
        pause = Event{EventKind::PauseSession};
        steps.push_back({std::chrono::milliseconds(250), std::move(pause)});
        ctx->scenario.append_steps(std::move(steps), *ctx);

        // No offset-0 step, so nothing has been enqueued yet.
        CHECK(fx.timer.enqueued_events.empty());
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        // Should arm a ms-precision delay close to 250ms, not rounded up to 1s.
        CHECK(fx.timer.scenario_timer_arms[0] <= std::chrono::milliseconds(250));
        CHECK(fx.timer.scenario_timer_arms[0] > std::chrono::milliseconds(0));
    }

    SECTION("Loop rewind arms strictly positive delay (does not disarm)") {
        auto ctx = fx.make_ctx();

        ctx->scenario.append_steps(make_three_step_list(), *ctx);
        ctx->scenario.mark_loop(1, 3);

        // Offset-0 Plug fired inline; first arm is for PauseSession at +100ms.
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::Plug);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);

        fx.timer.scenario_timer_arms.clear();

        // Fire 1: PauseSession enqueued, arm for ResumeSession at +200ms.
        ctx->scenario.on_timer_fire(*ctx);
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::PauseSession);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);

        fx.timer.scenario_timer_arms.clear();

        // Fire 2: ResumeSession enqueued, then loop rewinds to idx=1 and
        // rebases start_at_. arm_next fires the rewound PauseSession inline
        // (its delay is zero post-rebase), then arms a strictly positive
        // delay for the next future step. The arm must be > 0 so the
        // underlying timerfd does not disarm.
        ctx->scenario.on_timer_fire(*ctx);
        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::ResumeSession);
        CHECK(kind_of(fx.timer.enqueued_events[3]) == EventKind::PauseSession);
        CHECK(ctx->scenario.active());
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(fx.timer.scenario_timer_arms[0] > std::chrono::milliseconds(0));
    }

    SECTION("Loop emits its body again on the second pass") {
        auto ctx = fx.make_ctx();

        ctx->scenario.append_steps(make_three_step_list(), *ctx);
        ctx->scenario.mark_loop(1, 3);

        // Fire 1: cycle 1 Pause.
        ctx->scenario.on_timer_fire(*ctx);
        // Fire 2: cycle 1 Resume, loop rewinds and arm_next fires the rewound
        // cycle 2 Pause inline (its post-rewind delay is zero).
        ctx->scenario.on_timer_fire(*ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::Plug);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::PauseSession);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::ResumeSession);
        CHECK(kind_of(fx.timer.enqueued_events[3]) == EventKind::PauseSession);
        CHECK(ctx->scenario.active());
    }

    SECTION("Past-due step fires inline without arming a timer") {
        auto ctx = fx.make_ctx();

        std::vector<ScenarioStep> steps;
        Event plug;
        plug = Event{EventKind::Plug};
        Event pause;
        pause = Event{EventKind::PauseSession};
        // Both steps are at offset 0 (past-due immediately).
        steps.push_back({std::chrono::milliseconds(0), std::move(plug)});
        steps.push_back({std::chrono::milliseconds(0), std::move(pause)});
        ctx->scenario.append_steps(std::move(steps), *ctx);

        // Both steps fired inline; no timer arm recorded.
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::Plug);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::PauseSession);
        CHECK(fx.timer.scenario_timer_arms.empty());
        CHECK_FALSE(ctx->scenario.active());
    }
}

TEST_CASE("ScenarioDispatcher forward-progress guard", "[evsim][scenario][loop-guard]") {
    TestFixture fx;

    SECTION("Single zero-offset step looped on itself terminates with safety arm") {
        auto ctx = fx.make_ctx();

        // Use a small positive offset so the dispatcher arms a timer first
        // (rather than flushing inline). Once the timer fires and the loop
        // rewinds, every subsequent iteration sees `at=10ms - elapsed=0ms`
        // post-rewind. Without the guard this spins forever.
        std::vector<ScenarioStep> steps;
        Event pause;
        pause = Event{EventKind::PauseSession};
        steps.push_back({std::chrono::milliseconds(10), std::move(pause)});
        ctx->scenario.append_steps(std::move(steps), *ctx);
        ctx->scenario.mark_loop(0, 1);

        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        REQUIRE(fx.timer.enqueued_events.empty());

        fx.timer.scenario_timer_arms.clear();

        // Simulate the timer firing. on_timer_fire enqueues once, rewinds,
        // then arm_next would spin without the guard.
        ctx->scenario.on_timer_fire(*ctx);

        // The guard must bound inline iterations and then arm the 1 ms
        // safety delay rather than spinning forever.
        REQUIRE_FALSE(fx.timer.scenario_timer_arms.empty());
        CHECK(fx.timer.scenario_timer_arms.back() == std::chrono::milliseconds(1));
        // Many copies of the step were enqueued before the guard tripped,
        // bounded by steps_.size() + 128 + 1 ~= 130 here. Includes the
        // initial on_timer_fire enqueue and the inline-flushed copies.
        CHECK(fx.timer.enqueued_events.size() > 1);
        CHECK(fx.timer.enqueued_events.size() < 200);
        for (const auto& ev : fx.timer.enqueued_events) {
            CHECK(kind_of(ev) == EventKind::PauseSession);
        }
    }

    SECTION("Well-formed 5-step at-zero scenario completes without tripping the guard") {
        auto ctx = fx.make_ctx();

        std::vector<ScenarioStep> steps;
        for (int i = 0; i < 5; ++i) {
            Event ev;
            ev = Event{EventKind::Plug};
            steps.push_back({std::chrono::milliseconds(0), std::move(ev)});
        }
        ctx->scenario.append_steps(std::move(steps), *ctx);
        // No mark_loop — all five steps flush inline and the dispatcher
        // goes idle. The guard must not interfere.
        CHECK(fx.timer.enqueued_events.size() == 5);
        CHECK(fx.timer.scenario_timer_arms.empty());
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("mark_loop with begin >= end leaves loop unset") {
        auto ctx = fx.make_ctx();

        std::vector<ScenarioStep> steps;
        Event a;
        a = Event{EventKind::Plug};
        Event b;
        b = Event{EventKind::PauseSession};
        steps.push_back({std::chrono::milliseconds(0), std::move(a)});
        steps.push_back({std::chrono::milliseconds(0), std::move(b)});
        ctx->scenario.append_steps(std::move(steps), *ctx);

        // begin == end (degenerate).
        ctx->scenario.mark_loop(1, 1);
        // begin > end.
        ctx->scenario.mark_loop(2, 1);

        // Both calls must be ignored; the dispatcher already flushed both
        // steps inline and is idle. If the loop were set, arm_next on the
        // next round (e.g. via on_timer_fire) would attempt a rewind — but
        // since we are idle there is nothing more to fire. Verify via
        // a follow-up timer-fire that nothing additional happens.
        ctx->scenario.on_timer_fire(*ctx);
        CHECK(fx.timer.enqueued_events.size() == 2);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("Loop body preserves non-trivial payload across rewinds") {
        // Guards against the latent move-from-inside-loop bug: dispatching a
        // looped step via std::move would hollow out steps_[idx].ev so the
        // next iteration would see an empty payload. The fix copies inside
        // the loop body. We exercise it with SessionConfigParams carrying a
        // ChargingCurve (variant alternative with a nested vector) — not a
        // realistic scenario, but the only easy way to surface the UB with
        // the current EventKind set.
        auto ctx = fx.make_ctx();

        std::vector<ScenarioStep> steps;
        api::AcIecSessionParams params{};
        params.charging_current_a = 16.0f;
        params.three_phases = true;
        api::ChargingCurve curve;
        curve.loop = false;
        for (int i = 0; i < 3; ++i) {
            api::CurvePoint p;
            p.t_offset_ms = (i + 1) * 1000;
            p.current_a = 8.0f + static_cast<float>(i);
            p.three_phases = true;
            curve.points.push_back(p);
        }
        params.curve = std::move(curve);

        Event ev;
        ev = Event{EventKind::ConfigureSession};
        ev.payload = api::SessionConfigParams{std::move(params)};
        steps.push_back({std::chrono::milliseconds(50), std::move(ev)});

        ctx->scenario.append_steps(std::move(steps), *ctx);
        ctx->scenario.mark_loop(0, 1);

        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        REQUIRE(fx.timer.enqueued_events.empty());

        // Drive two rewinds. Each on_timer_fire enqueues the (looped) step
        // once and then arm_next inline-flushes the rewound past-due copy
        // until the forward-progress guard arms a 1 ms safety delay. The
        // assertion is that EVERY enqueued copy still carries the full
        // curve.points payload — the bug would manifest as an empty vector
        // (or a hollowed-out variant) on the second and later passes.
        ctx->scenario.on_timer_fire(*ctx);
        ctx->scenario.on_timer_fire(*ctx);

        REQUIRE(fx.timer.enqueued_events.size() >= 2);
        for (const auto& enqueued : fx.timer.enqueued_events) {
            REQUIRE(kind_of(enqueued) == EventKind::ConfigureSession);
            REQUIRE(std::holds_alternative<api::SessionConfigParams>(enqueued.payload));
            const auto& sp = std::get<api::SessionConfigParams>(enqueued.payload);
            REQUIRE(std::holds_alternative<api::AcIecSessionParams>(sp));
            const auto& p = std::get<api::AcIecSessionParams>(sp);
            REQUIRE(p.curve.has_value());
            CHECK(p.curve->points.size() == 3);
            CHECK(p.curve->points[0].current_a == 8.0f);
            CHECK(p.curve->points[2].current_a == 10.0f);
        }
    }

    SECTION("mark_loop with end > step_count leaves loop unset") {
        auto ctx = fx.make_ctx();

        std::vector<ScenarioStep> steps;
        Event a;
        a = Event{EventKind::Plug};
        steps.push_back({std::chrono::milliseconds(100), std::move(a)});
        ctx->scenario.append_steps(std::move(steps), *ctx);
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);

        // end > step_count (out of bounds).
        ctx->scenario.mark_loop(0, 5);
        // begin >= step_count.
        ctx->scenario.mark_loop(2, 3);

        fx.timer.scenario_timer_arms.clear();
        ctx->scenario.on_timer_fire(*ctx);
        // Loop unset, so the single step fires once and the dispatcher
        // goes idle without rearming.
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        CHECK(fx.timer.scenario_timer_arms.empty());
        CHECK_FALSE(ctx->scenario.active());
    }
}
