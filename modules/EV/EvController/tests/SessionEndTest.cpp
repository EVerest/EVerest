// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/Events.hpp"
#include "../main/FsmContext.hpp"
#include "../main/states/Charging.hpp"
#include "../main/states/ChargingPwmPaused.hpp"
#include "../main/states/Paused.hpp"
#include "../main/states/Plugged.hpp"
#include "../main/states/Stopping.hpp"
#include "../main/states/Unplugged.hpp"
#include "../main/states/V2GNegotiating.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <everest/util/fsm/fsm.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_controller;

namespace {

int count_kind(const TimerSink& timer, EventKind kind) {
    int count = 0;
    for (const auto& ev : timer.enqueued_events) {
        count += kind_of(ev) == kind ? 1 : 0;
    }
    return count;
}

int begin_session_count(const TimerSink& timer) {
    return count_kind(timer, EventKind::BeginSession);
}

const std::string cp_a = "set_cp_state(cp_state=A)";
const std::string cp_b = "set_cp_state(cp_state=B)";

using Machine = fsm::v2::FSM<StateBase>;

// Feeds what was queued, oldest first, as EvCtlRuntime::on_wake drains its queue, with a
// configure_session going to the pre-FSM interceptor as it does there.
void drain(TimerSink& timer, FsmContext& ctx, Machine& machine) {
    while (!timer.enqueued_events.empty()) {
        Event ev = std::move(timer.enqueued_events.front());
        timer.enqueued_events.erase(timer.enqueued_events.begin());
        if (const auto* spec = std::get_if<api::SessionConfigParams>(&ev.payload)) {
            ctx.configure_session(*spec);
        } else {
            machine.feed(ev);
        }
    }
}

Event pwm_measurement() {
    return Event{BspMeasurementPayload{50.0f, std::nullopt, ::types::board_support_common::ProximityPilot{}}};
}

api::FsmState finish_stop(StateBase::Result into_stopping) {
    REQUIRE(into_stopping.new_state);
    REQUIRE(into_stopping.new_state->get_id() == api::FsmState::Stopping);
    auto result = into_stopping.new_state->feed(Event{EventKind::IsoV2GFinished});
    REQUIRE(result.new_state);
    return result.new_state->get_id();
}

} // namespace

TEST_CASE("an EV stays plugged when its session ends", "[evctl][session-end]") {
    TestFixture fx;

    SECTION("a stop from the charger ends in Plugged with the session cleared") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        Charging charging{*ctx};

        CHECK(finish_stop(charging.feed(Event{EventKind::IsoStopFromCharger})) == api::FsmState::Plugged);
        CHECK_FALSE(ctx->vars.session.has_value());
    }

    SECTION("the held Plugged state does not begin a new session") {
        auto ctx = fx.make_ctx();
        Stopping stopping{*ctx};
        auto result = stopping.feed(Event{EventKind::IsoV2GFinished});
        REQUIRE(result.new_state);

        result.new_state->enter();

        CHECK(begin_session_count(fx.timer) == 0);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));
    }

    SECTION("an AC IEC stop clears the session and holds Plugged") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        Charging charging{*ctx};

        auto result = charging.feed(Event{EventKind::StopSession});

        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Plugged);
        CHECK_FALSE(ctx->vars.session.has_value());
        CHECK(ctx->vars.hold_session);
    }

    SECTION("an AC IEC stop during a PWM pause clears the session and holds Plugged") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        ChargingPwmPaused paused{*ctx};

        auto result = paused.feed(Event{EventKind::StopSession});

        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Plugged);
        CHECK_FALSE(ctx->vars.session.has_value());
        CHECK(ctx->vars.hold_session);
    }

    SECTION("a fresh plug still begins a session") {
        auto ctx = fx.make_ctx();
        Plugged plugged{*ctx};

        plugged.enter();

        CHECK(begin_session_count(fx.timer) == 1);
    }
}

TEST_CASE("a BeginSession queued by an earlier plug begins nothing", "[evctl][session-end][begin-session]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    const auto queue_replug = [&] {
        ctx->enqueue(pilot_input(::types::board_support_common::Event::A));
        ctx->enqueue(pilot_input(::types::board_support_common::Event::Disconnected));
        ctx->enqueue(pilot_input(::types::board_support_common::Event::A));
    };

    SECTION("two plugs' BeginSessions begin one session") {
        Machine machine{std::make_unique<Unplugged>(*ctx)};
        fx.mocks.bsp.clear();
        queue_replug();

        drain(fx.timer, *ctx, machine);

        CHECK(std::count_if(fx.mocks.bsp.records.begin(), fx.mocks.bsp.records.end(), [](const std::string& r) {
                  return r.find("set_ac_max_current") != std::string::npos;
              }) == 1);
        CHECK(ctx->vars.session.has_value());
    }

    SECTION("a stop sent while the second plug is entered holds the EV") {
        // The charger's PWM and an operator's stop, arriving while the second plug sets CP B.
        int cp_b_count = 0;
        ctx->peer_actions.bsp.set_cp = [&](::types::ev_board_support::EvCpState state) {
            fx.mocks.bsp.call_set_cp_state(state);
            if (state == ::types::ev_board_support::EvCpState::B && ++cp_b_count == 2) {
                ctx->enqueue(pwm_measurement());
                ctx->enqueue(Event{StopSessionCmd{}});
            }
        };
        Machine machine{std::make_unique<Unplugged>(*ctx)};
        queue_replug();

        drain(fx.timer, *ctx, machine);
        machine.feed(pwm_measurement());
        machine.feed(pwm_measurement());

        CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
        CHECK(ctx->vars.hold_session);
        CHECK_FALSE(ctx->vars.session.has_value());
        const auto last_cp = std::find_if(fx.mocks.bsp.records.rbegin(), fx.mocks.bsp.records.rend(),
                                          [](const std::string& r) { return r.rfind("set_cp_state", 0) == 0; });
        REQUIRE(last_cp != fx.mocks.bsp.records.rend());
        CHECK(*last_cp == cp_b);
    }
}

TEST_CASE("a held session needs a deliberate pilot cycle", "[evctl][session-end][replug]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->pilot_present = true;
    ctx->vars.hold_session = true;
    Machine machine{std::make_unique<Plugged>(*ctx)};
    REQUIRE(begin_session_count(fx.timer) == 0);
    machine.feed(pilot_input(::types::board_support_common::Event::A));
    machine.feed(pwm_measurement());
    CHECK(ctx->vars.hold_session);
    CHECK_FALSE(ctx->vars.session);
    CHECK(begin_session_count(fx.timer) == 0);
    fx.mocks.bsp.clear();
    machine.feed(pilot_input(::types::board_support_common::Event::Disconnected));
    CHECK(machine.get_current_state_id() == api::FsmState::Unplugged);
    CHECK(fx.timer.state_timer_arms.empty());
    CHECK_FALSE(ctx->vars.hold_session);
    machine.feed(Event{StateDeadlineEvt{}});
    CHECK(machine.get_current_state_id() == api::FsmState::Unplugged);
    machine.feed(pilot_input(::types::board_support_common::Event::A));
    drain(fx.timer, *ctx, machine);
    CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
    CHECK(ctx->vars.session);
    CHECK(index_of_substr(fx.mocks.bsp.records, cp_a) < index_of_substr(fx.mocks.bsp.records, cp_b));
}

TEST_CASE("the forced end of a stop stops the V2G session again", "[evctl][session-end]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    Stopping stopping{*ctx};
    stopping.enter();
    const auto count_stops = [&] {
        return std::count(fx.mocks.iso.records.begin(), fx.mocks.iso.records.end(), std::string{"stop_charging()"});
    };
    const auto stops_on_enter = count_stops();

    SECTION("on the deadline") {
        auto result = stopping.feed(Event{EventKind::StateDeadline});

        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Plugged);
        CHECK(count_stops() == stops_on_enter + 1);
    }

    SECTION("not when the session reported finished") {
        auto result = stopping.feed(Event{EventKind::IsoV2GFinished});

        REQUIRE(result.new_state);
        CHECK(count_stops() == stops_on_enter);
    }
}

TEST_CASE("a pilot disconnect during an ISO session ends unplugged", "[evctl][session-end]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    StateBase::ContainerType state;
    SECTION("from Charging") {
        state = std::make_unique<Charging>(*ctx);
    }
    SECTION("from Paused") {
        state = std::make_unique<Paused>(*ctx);
    }
    SECTION("from V2GNegotiating") {
        state = std::make_unique<V2GNegotiating>(*ctx);
    }
    SECTION("from Stopping") {
        state = std::make_unique<Stopping>(*ctx);
    }
    Machine machine{std::move(state)};
    machine.feed(pilot_input(::types::board_support_common::Event::Disconnected));
    CHECK(machine.get_current_state_id() == api::FsmState::Unplugged);
    CHECK_FALSE(ctx->vars.session);
    machine.feed(Event{IsoV2GFinishedEvt{}});
    machine.feed(Event{StateDeadlineEvt{}});
    CHECK(machine.get_current_state_id() == api::FsmState::Unplugged);
}
