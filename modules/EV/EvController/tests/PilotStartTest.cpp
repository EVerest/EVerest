// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/states/BcbToggling.hpp"
#include "../main/states/ChargerFault.hpp"
#include "../main/states/Charging.hpp"
#include "../main/states/ChargingPwmPaused.hpp"
#include "../main/states/Disabled.hpp"
#include "../main/states/Faulted.hpp"
#include "../main/states/Paused.hpp"
#include "../main/states/Plugged.hpp"
#include "../main/states/SlacMatching.hpp"
#include "../main/states/Stopping.hpp"
#include "../main/states/Unplugged.hpp"
#include "../main/states/V2GNegotiating.hpp"
#include "TestFixture.hpp"
#include <array>
#include <everest/util/fsm/fsm.hpp>
#include <everest_api_types/ev_controller/codec.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_controller;
using Pilot = ::types::board_support_common::Event;

namespace {
Event pilot_event(Pilot pilot) {
    ::types::board_support_common::BspEvent bsp{};
    bsp.event = pilot;
    return Event{BspEventPayload{bsp}};
}

void drain(TestFixture& fx, fsm::v2::FSM<StateBase>& machine) {
    while (!fx.timer.enqueued_events.empty()) {
        auto ev = std::move(fx.timer.enqueued_events.front());
        fx.timer.enqueued_events.erase(fx.timer.enqueued_events.begin());
        machine.feed(std::move(ev));
    }
}

template <class State> StateBase::ContainerType make_state(FsmContext& ctx) {
    return std::make_unique<State>(ctx);
}

constexpr std::array session_states{make_state<Plugged>,     make_state<SlacMatching>, make_state<V2GNegotiating>,
                                    make_state<BcbToggling>, make_state<Charging>,     make_state<ChargingPwmPaused>,
                                    make_state<Paused>,      make_state<Stopping>};
} // namespace

TEST_CASE("EvController pilot A B or C presence edge starts a plugged session", "[evctl][pilot-start]") {
    const auto pilot = GENERATE(Pilot::A, Pilot::B, Pilot::C);
    TestFixture fx;
    auto ctx = fx.make_ctx();
    Unplugged state(*ctx);
    state.enter();
    auto result = state.feed(pilot_event(pilot));
    REQUIRE(result.new_state);
    CHECK(result.new_state->get_id() == api::FsmState::Plugged);
}

TEST_CASE("EvController pilot start resets SLAC before matching", "[evctl][pilot-start]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->configured_session = api::DcIso2SessionParams{};
    fsm::v2::FSM<StateBase> machine{std::make_unique<Unplugged>(*ctx)};
    REQUIRE(fx.mocks.slac.records == std::vector<std::string>{"reset()"});
    machine.feed(pilot_event(Pilot::A));
    REQUIRE(fx.timer.enqueued_events.size() == 1);
    machine.feed(fx.timer.enqueued_events.front());
    BspMeasurementPayload measurement{};
    measurement.cp_pwm_duty_cycle = 5;
    machine.feed(Event{measurement});
    CHECK(machine.get_current_state_id() == api::FsmState::SlacMatching);
    CHECK(fx.mocks.slac.records == std::vector<std::string>{"reset()", "trigger_matching()"});
}

TEST_CASE("EvController pilot faults abort each session in order", "[evctl][pilot-start]") {
    for (auto make : session_states) {
        for (auto pilot : {Pilot::E, Pilot::F}) {
            TestFixture fx;
            auto ctx = fx.make_ctx();
            set_mode(*ctx, api::ChargeMode::DcIso2);
            ctx->vars.bcb_remaining = 6;
            fsm::v2::FSM<StateBase> machine{make(*ctx)};
            INFO(static_cast<int>(machine.get_current_state_id()) << " pilot " << static_cast<int>(pilot));
            if (machine.get_current_state_id() == api::FsmState::Charging) {
                REQUIRE(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=C)"));
            }
            fx.mocks.iso.clear();
            fx.mocks.bsp.clear();
            fx.mocks.slac.clear();
            std::vector<std::string> calls;
            ctx->peer_actions.iso.abort_charging = [&] {
                fx.mocks.iso.call_abort_charging();
                calls.push_back(fx.mocks.iso.records.back());
            };
            ctx->peer_actions.bsp.allow_power_on = [&](bool on) {
                fx.mocks.bsp.call_allow_power_on(on);
                calls.push_back(fx.mocks.bsp.records.back());
            };
            ctx->peer_actions.bsp.set_cp = [&](::types::ev_board_support::EvCpState cp) {
                fx.mocks.bsp.call_set_cp_state(cp);
                calls.push_back(fx.mocks.bsp.records.back());
            };
            ctx->peer_actions.slac.reset = [&] {
                fx.mocks.slac.call_reset();
                calls.push_back(fx.mocks.slac.records.back());
            };
            machine.feed(pilot_event(pilot));
            CHECK(machine.get_current_state_id() == api::FsmState::ChargerFault);
            CHECK(calls == std::vector<std::string>{"abort_charging()", "allow_power_on(value=false)",
                                                    "set_cp_state(cp_state=B)", "reset()"});
        }
    }
}

TEST_CASE("EvController pilot faults stop power and report a charger fault", "[evctl][pilot-start]") {
    const auto make = GENERATE(make_state<Charging>, make_state<Plugged>, make_state<SlacMatching>);
    const auto pilot = GENERATE(Pilot::E, Pilot::F);
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    fsm::v2::FSM<StateBase> machine{make(*ctx)};
    fx.mocks.bsp.clear();
    fx.sink.clear();

    machine.feed(pilot_event(pilot));

    REQUIRE(machine.get_current_state_id() == api::FsmState::ChargerFault);
    CHECK(fx.mocks.bsp.records.front() == "allow_power_on(value=false)");
    CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=true)"));
    const auto fault_topic = fx.topics.everest_to_extern("fault");
    REQUIRE(topic_recorded(fx.sink, fault_topic));
    const auto report = api::deserialize<api::FaultReport>(payload_for(fx.sink, fault_topic));
    CHECK(report.type == api::FaultType::ChargerFault);
    REQUIRE(report.message.has_value());
    CHECK(*report.message == (pilot == Pilot::E ? "pilot E" : "pilot F"));
}

TEST_CASE("EvController pilot fault before a pending start enters ChargerFault", "[evctl][pilot-start]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->pilot_present = true;
    fsm::v2::FSM<StateBase> machine{std::make_unique<Unplugged>(*ctx)};
    fx.sink.clear();

    machine.feed(pilot_event(Pilot::F));
    drain(fx, machine);

    REQUIRE(machine.get_current_state_id() == api::FsmState::ChargerFault);
    CHECK(ctx->pilot_present);
    CHECK(topic_recorded(fx.sink, fx.topics.everest_to_extern("fault")));
}

TEST_CASE("EvController charger fault recovers on A with a fresh session", "[evctl][pilot-start]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->configured_session = api::AcIecSessionParams{12.0f, false, std::nullopt};
    fsm::v2::FSM<StateBase> machine{std::make_unique<Plugged>(*ctx)};
    const auto old_begin = fx.timer.enqueued_events.back();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.hold_session = true;
    ctx->vars.abort_on_stop = true;
    ctx->vars.resume_awaiting_pwm = true;
    machine.feed(pilot_event(Pilot::E));
    REQUIRE(machine.get_current_state_id() == api::FsmState::ChargerFault);
    machine.feed(pilot_event(Pilot::A));
    REQUIRE(machine.get_current_state_id() == api::FsmState::Plugged);
    CHECK_FALSE(ctx->vars.session);
    CHECK_FALSE(ctx->vars.hold_session);
    CHECK_FALSE(ctx->vars.abort_on_stop);
    CHECK_FALSE(ctx->vars.resume_awaiting_pwm);
    REQUIRE(fx.timer.enqueued_events.size() == 2);
    machine.feed(old_begin);
    CHECK_FALSE(ctx->vars.session);
    machine.feed(fx.timer.enqueued_events.back());
    CHECK(ctx->vars.charge_mode() == api::ChargeMode::AcIec);
    CHECK(ctx->vars.charging_current_a == 12.0f);
    CHECK_FALSE(ctx->vars.three_phases);
}

TEST_CASE("EvController charger fault recovers only within the running PWM window", "[evctl][pilot-start]") {
    const auto duty = GENERATE(0.0f, 4.0f, 4.1f, 50.0f, 96.9f, 97.0f, 100.0f);
    TestFixture fx;
    auto ctx = fx.make_ctx();
    fsm::v2::FSM<StateBase> machine{std::make_unique<Plugged>(*ctx)};
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.hold_session = true;
    machine.feed(pilot_event(Pilot::F));
    REQUIRE(machine.get_current_state_id() == api::FsmState::ChargerFault);
    BspMeasurementPayload measurement{};
    measurement.cp_pwm_duty_cycle = duty;
    machine.feed(Event{measurement});
    if (duty > 4.0f && duty < 97.0f) {
        REQUIRE(machine.get_current_state_id() == api::FsmState::Plugged);
        CHECK_FALSE(ctx->vars.session);
        CHECK_FALSE(ctx->vars.hold_session);
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        machine.feed(fx.timer.enqueued_events.back());
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::AcIec);
    } else {
        CHECK(machine.get_current_state_id() == api::FsmState::ChargerFault);
        CHECK(fx.timer.enqueued_events.size() == 1);
    }
}

TEST_CASE("EvController pilot disconnect unplugs every connected state", "[evctl][pilot-start]") {
    std::vector states(session_states.begin(), session_states.end());
    states.push_back(make_state<ChargerFault>);
    states.push_back(make_state<Faulted>);
    for (auto make : states) {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.bcb_remaining = 6;
        fsm::v2::FSM<StateBase> machine{make(*ctx)};
        INFO(static_cast<int>(machine.get_current_state_id()));
        machine.feed(pilot_event(Pilot::Disconnected));
        CHECK(machine.get_current_state_id() == api::FsmState::Unplugged);
        CHECK_FALSE(ctx->vars.session);
    }
}

TEST_CASE("EvController charger fault honors lifecycle commands", "[evctl][pilot-start]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    fsm::v2::FSM<StateBase> machine{std::make_unique<ChargerFault>(*ctx)};
    SECTION("query state") {
        fx.sink.clear();
        machine.feed(Event{QueryStateCmd{}});
        CHECK(payload_for(fx.sink, fx.topics.everest_to_extern("state")) == "\"ChargerFault\"");
    }
    SECTION("disable") {
        machine.feed(Event{DisableCmd{}});
        CHECK(machine.get_current_state_id() == api::FsmState::Disabled);
    }
    SECTION("session commands are rejected") {
        fx.sink.clear();
        machine.feed(Event{StopSessionCmd{}});
        CHECK(machine.get_current_state_id() == api::FsmState::ChargerFault);
        const auto ack = payload_for(fx.sink, fx.topics.everest_to_extern("command_ack"));
        CHECK(ack.find("\"stop_session\"") != std::string::npos);
        CHECK(ack.find("\"Rejected\"") != std::string::npos);
        CHECK(ack.find("\"charger fault\"") != std::string::npos);
    }
}

TEST_CASE("EvController repeated pilot faults have no further effects", "[evctl][pilot-start]") {
    const auto first_fault = GENERATE(Pilot::E, Pilot::F);
    TestFixture fx;
    auto ctx = fx.make_ctx();
    fsm::v2::FSM<StateBase> machine{std::make_unique<Plugged>(*ctx)};
    machine.feed(pilot_event(first_fault));
    REQUIRE(machine.get_current_state_id() == api::FsmState::ChargerFault);
    fx.mocks.iso.clear();
    fx.mocks.bsp.clear();
    fx.mocks.slac.clear();
    for (auto pilot : {Pilot::E, Pilot::F, Pilot::E, Pilot::F}) {
        machine.feed(pilot_event(pilot));
        CHECK(machine.get_current_state_id() == api::FsmState::ChargerFault);
        CHECK(fx.mocks.iso.records.empty());
        CHECK(fx.mocks.bsp.records.empty());
        CHECK(fx.mocks.slac.records.empty());
    }
}

TEST_CASE("EvController pilot faults work without optional peers", "[evctl][pilot-start]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->peer_actions.iso = {};
    ctx->peer_actions.slac = {};
    fsm::v2::FSM<StateBase> machine{std::make_unique<Unplugged>(*ctx)};
    machine.feed(pilot_event(Pilot::A));
    REQUIRE(machine.get_current_state_id() == api::FsmState::Plugged);
    fx.mocks.bsp.clear();
    machine.feed(pilot_event(Pilot::F));
    CHECK(machine.get_current_state_id() == api::FsmState::ChargerFault);
    CHECK(fx.mocks.bsp.records == std::vector<std::string>{"allow_power_on(value=false)", "set_cp_state(cp_state=B)"});
    CHECK(fx.mocks.iso.records.empty());
    CHECK(fx.mocks.slac.records.empty());
}

TEST_CASE("EvController enable starts only when the pilot is present", "[evctl][pilot-start]") {
    const auto present = GENERATE(false, true);
    TestFixture fx;
    auto ctx = fx.make_ctx();
    fsm::v2::FSM<StateBase> machine{std::make_unique<Disabled>(*ctx)};
    if (present) {
        machine.feed(pilot_event(Pilot::A));
        REQUIRE(machine.get_current_state_id() == api::FsmState::Disabled);
    }
    machine.feed(Event{EnableCmd{}});
    drain(fx, machine);
    CHECK(machine.get_current_state_id() == (present ? api::FsmState::Plugged : api::FsmState::Unplugged));
    CHECK(ctx->vars.session.has_value() == present);
}

TEST_CASE("EvController ignores its own A echo while the pilot is present", "[evctl][pilot-start]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->pilot_present = true;
    Unplugged state(*ctx);
    state.enter();
    REQUIRE(fx.timer.enqueued_events.size() == 1);
    auto result = state.feed(pilot_event(Pilot::A));
    CHECK_FALSE(result.new_state);
    CHECK(fx.timer.enqueued_events.size() == 1);
}

TEST_CASE("EvController pilot mode restores configuration but waits for presence", "[evctl][pilot-start]") {
    const auto present = GENERATE(false, true);
    TestFixture fx;
    fx.cfg.keep_cross_boot_plugin_state = true;
    fx.mocks.next_kvs_load_value =
        R"({"plugged_in":true,"hold_session":true,"configured_session":{"mode":"AcIec","params":{"charging_current_a":12,"three_phases":false}}})";
    auto ctx = fx.make_ctx();
    ctx->kvs_load();
    REQUIRE(ctx->configured_session);
    fsm::v2::FSM<StateBase> machine{std::make_unique<Disabled>(*ctx)};
    if (present) {
        machine.feed(pilot_event(Pilot::A));
    }
    machine.feed(Event{EnableCmd{}});
    CHECK(fx.timer.state_timer_arms.empty());
    machine.feed(Event{StateDeadlineEvt{}});
    drain(fx, machine);
    CHECK(machine.get_current_state_id() == (present ? api::FsmState::Plugged : api::FsmState::Unplugged));
    CHECK(ctx->vars.session.has_value() == present);
    if (present) {
        CHECK(ctx->vars.charging_current_a == 12.0f);
        CHECK_FALSE(ctx->vars.hold_session);
    }
}

TEST_CASE("EvController tracks pilot presence in every state including Disabled", "[evctl][pilot-start]") {
    const auto pilot = GENERATE(Pilot::A, Pilot::B, Pilot::C, Pilot::D, Pilot::E, Pilot::F, Pilot::Disconnected,
                                Pilot::PowerOn, Pilot::PowerOff);
    const auto was_present = GENERATE(false, true);
    std::vector states(session_states.begin(), session_states.end());
    states.insert(states.end(),
                  {make_state<Disabled>, make_state<Unplugged>, make_state<Faulted>, make_state<ChargerFault>});
    for (auto make : states) {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        ctx->pilot_present = was_present;
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.bcb_remaining = 6;
        ctx->vars.last_fault = api::FaultReport{api::FaultType::Internal, "test", std::nullopt};
        fsm::v2::FSM<StateBase> machine{make(*ctx)};
        INFO(static_cast<int>(machine.get_current_state_id()) << " pilot " << static_cast<int>(pilot));
        machine.feed(pilot_event(pilot));
        const bool expected =
            (pilot == Pilot::E || pilot == Pilot::F || pilot == Pilot::PowerOn || pilot == Pilot::PowerOff)
                ? was_present
                : pilot != Pilot::Disconnected;
        CHECK(ctx->pilot_present == expected);
    }
}

TEST_CASE("EvController disconnect then pilot A starts a fresh session", "[evctl][pilot-start]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    fsm::v2::FSM<StateBase> machine{std::make_unique<Unplugged>(*ctx)};
    machine.feed(pilot_event(Pilot::A));
    drain(fx, machine);
    const auto generation = ctx->vars.plug_generation;
    REQUIRE(machine.get_current_state_id() == api::FsmState::Plugged);
    machine.feed(pilot_event(Pilot::Disconnected));
    drain(fx, machine);
    REQUIRE(machine.get_current_state_id() == api::FsmState::Unplugged);
    REQUIRE_FALSE(ctx->pilot_present);
    machine.feed(pilot_event(Pilot::A));
    drain(fx, machine);
    CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
    CHECK(ctx->vars.plug_generation == generation + 1);
    CHECK(ctx->vars.session.has_value());
}

TEST_CASE("EvController disconnect cancels a queued pilot start", "[evctl][pilot-start]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    fsm::v2::FSM<StateBase> machine{std::make_unique<Disabled>(*ctx)};
    machine.feed(pilot_event(Pilot::A));
    machine.feed(Event{EnableCmd{}});
    REQUIRE(fx.timer.enqueued_events.size() == 1);
    fx.mocks.bsp.clear();
    machine.feed(pilot_event(Pilot::Disconnected));
    drain(fx, machine);
    CHECK(machine.get_current_state_id() == api::FsmState::Unplugged);
    CHECK_FALSE(ctx->vars.session);
    CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=A)"));
    CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));
}

TEST_CASE("EvController stale BeginSession cannot substitute for a pilot presence edge", "[evctl][pilot-start]") {
    const auto pilot = GENERATE(Pilot::D, Pilot::E, Pilot::F);
    const auto enabling = GENERATE(false, true);
    TestFixture fx;
    auto ctx = fx.make_ctx();
    fsm::v2::FSM<StateBase> machine{std::make_unique<Disabled>(*ctx)};
    if (enabling) {
        machine.feed(pilot_event(Pilot::A));
        machine.feed(Event{EnableCmd{}});
    } else {
        machine.feed(Event{EnableCmd{}});
        machine.feed(pilot_event(Pilot::A));
    }
    REQUIRE(fx.timer.enqueued_events.size() == 1);
    machine.feed(pilot_event(Pilot::Disconnected));
    machine.feed(pilot_event(pilot));
    drain(fx, machine);
    CHECK(machine.get_current_state_id() == api::FsmState::Unplugged);
    CHECK_FALSE(ctx->vars.session);
}

TEST_CASE("EvController initial pilot fault waits for a usable pilot before starting", "[evctl][pilot-start]") {
    const auto fault = GENERATE(Pilot::E, Pilot::F);
    const auto recovery = GENERATE(Pilot::B, Pilot::C);
    const auto enable_after_fault = GENERATE(false, true);
    TestFixture fx;
    auto ctx = fx.make_ctx();
    fsm::v2::FSM<StateBase> machine{std::make_unique<Disabled>(*ctx)};
    if (!enable_after_fault) {
        machine.feed(Event{EnableCmd{}});
    }
    machine.feed(pilot_event(fault));
    CHECK_FALSE(ctx->pilot_present);
    if (enable_after_fault) {
        machine.feed(Event{EnableCmd{}});
    }
    drain(fx, machine);
    CHECK(machine.get_current_state_id() == api::FsmState::Unplugged);
    CHECK_FALSE(ctx->vars.session);
    machine.feed(pilot_event(recovery));
    drain(fx, machine);
    CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
    CHECK(ctx->vars.session.has_value());
}

TEST_CASE("EvController enable with pilot presence avoids a CP A pulse", "[evctl][pilot-start]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    fsm::v2::FSM<StateBase> machine{make_state<Disabled>(*ctx)};
    machine.feed(pilot_event(Pilot::A));
    fx.mocks.bsp.clear();
    machine.feed(Event{EnableCmd{}});
    drain(fx, machine);
    CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=A)"));
    CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));
    CHECK(ctx->vars.session);
}

TEST_CASE("EvController disabling does not clear world faults", "[evctl][pilot-only]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    transition_to_disabled(*ctx);
    CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "diode_fail"));
    CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_rcd_error"));
}
