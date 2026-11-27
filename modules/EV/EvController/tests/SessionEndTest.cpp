// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/EventDispatch.hpp"
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
#include <everest_api_types/ev_controller/codec.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
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

TEST_CASE("a pilot disconnect aborts the V2G session without a SessionStop", "[evctl][session-end]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    StateBase::ContainerType state;
    SECTION("from Charging") {
        state = std::make_unique<Charging>(*ctx);
    }
    SECTION("from V2GNegotiating") {
        state = std::make_unique<V2GNegotiating>(*ctx);
    }
    Machine machine{std::move(state)};
    fx.mocks.iso.clear();
    machine.feed(pilot_input(::types::board_support_common::Event::Disconnected));
    REQUIRE(machine.get_current_state_id() == api::FsmState::Unplugged);
    CHECK(contains_substr(fx.mocks.iso.records, "abort_charging()"));
    CHECK_FALSE(contains_substr(fx.mocks.iso.records, "stop_charging()"));
}

TEST_CASE("a V2G session that ends during negotiation ends the stop", "[evctl][session-end]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->pilot_present = true;
    set_mode(*ctx, api::ChargeMode::DcIso2);
    Machine machine{std::make_unique<V2GNegotiating>(*ctx)};
    SECTION("before power ready") {
    }
    SECTION("after a DC power ready closed the contactor") {
        machine.feed(Event{IsoPowerReadyEvt{}});
        REQUIRE(machine.get_current_state_id() == api::FsmState::V2GNegotiating);
        REQUIRE(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=C)"));
    }
    fx.mocks.bsp.clear();
    machine.feed(Event{IsoV2GFinishedEvt{}});
    CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
    CHECK(ctx->vars.hold_session);
    CHECK_FALSE(ctx->vars.session);
    REQUIRE_FALSE(fx.mocks.bsp.records.empty());
    CHECK(index_of_substr(fx.mocks.bsp.records, "allow_power_on(value=false)") >= 0);
    const auto last_cp = std::find_if(fx.mocks.bsp.records.rbegin(), fx.mocks.bsp.records.rend(),
                                      [](const std::string& r) { return r.rfind("set_cp_state", 0) == 0; });
    REQUIRE(last_cp != fx.mocks.bsp.records.rend());
    CHECK(*last_cp == cp_b);
    CHECK(begin_session_count(fx.timer) == 0);
    const auto fault_topic = fx.topics.everest_to_extern("fault");
    REQUIRE(topic_recorded(fx.sink, fault_topic));
    const auto fault = api::deserialize<api::FaultReport>(payload_for(fx.sink, fault_topic));
    // The negotiation failed, as the removed V2G deadline reported it; no Faulted follows.
    CHECK(fault.type == api::FaultType::V2GTimeout);
    CHECK(fault.message == std::optional<std::string>{"V2G session ended during negotiation"});
}

TEST_CASE("a V2G session end before the resumed start is sent belongs to the paused session", "[evctl][session-end]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.resume_awaiting_pwm = true;
    Machine machine{std::make_unique<V2GNegotiating>(*ctx)};
    machine.feed(Event{IsoV2GFinishedEvt{}});
    CHECK(machine.get_current_state_id() == api::FsmState::V2GNegotiating);
    CHECK(ctx->vars.session);
}

TEST_CASE("a stop holds CP C while the V2G session winds down", "[evctl][session-end][stop-hold]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->vars.iso_session_active = true;
    const auto released = [&] {
        return contains_substr(fx.mocks.bsp.records, cp_b) &&
               contains_substr(fx.mocks.bsp.records, "allow_power_on(value=false)");
    };

    SECTION("until PowerDelivery(Stop) is accepted") {
        set_mode(*ctx, api::ChargeMode::DcIso2);
        Machine machine{std::make_unique<Stopping>(*ctx)};
        CHECK(contains_substr(fx.mocks.iso.records, "stop_charging()"));
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, cp_b));
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=false)"));

        machine.feed(Event{IsoPowerDeliveryStoppedEvt{}});
        CHECK(machine.get_current_state_id() == api::FsmState::Stopping);
        CHECK(index_of_substr(fx.mocks.bsp.records, "allow_power_on(value=false)") <
              index_of_substr(fx.mocks.bsp.records, cp_b));
        CHECK(released());

        machine.feed(Event{IsoV2GFinishedEvt{}});
        CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
    }

    SECTION("until the session ends when it ends without PowerDelivery(Stop)") {
        set_mode(*ctx, api::ChargeMode::AcIsoD20);
        Machine machine{std::make_unique<Stopping>(*ctx)};
        CHECK_FALSE(released());
        ctx->vars.iso_session_active = false;
        machine.feed(Event{IsoV2GFinishedEvt{}});
        CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
        CHECK(released());
    }

    SECTION("for MCS until the session ends") {
        set_mode(*ctx, api::ChargeMode::DcIsoD20);
        ensure_session(*ctx).mcs_enabled = true;
        Machine machine{std::make_unique<Stopping>(*ctx)};
        machine.feed(Event{IsoPowerDeliveryStoppedEvt{}});
        CHECK_FALSE(released());
        ctx->vars.iso_session_active = false;
        machine.feed(Event{IsoV2GFinishedEvt{}});
        CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
        CHECK(released());
    }

    SECTION("until the deadline") {
        set_mode(*ctx, api::ChargeMode::DcIsoD20);
        ensure_session(*ctx).mcs_enabled = true;
        Machine machine{std::make_unique<Stopping>(*ctx)};
        machine.feed(Event{StateDeadlineEvt{}});
        CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
        CHECK(released());
    }

    SECTION("not when the stop aborts the session") {
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.abort_on_stop = true;
        Machine machine{std::make_unique<Stopping>(*ctx)};
        CHECK(contains_substr(fx.mocks.iso.records, "abort_charging()"));
        CHECK(released());
    }

    SECTION("not without a live session") {
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.iso_session_active = false;
        Machine machine{std::make_unique<Stopping>(*ctx)};
        CHECK(released());
    }

    SECTION("not into a software fault") {
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->set_cp(::types::ev_board_support::EvCpState::C);
        Machine machine{std::make_unique<Stopping>(*ctx)};
        machine.feed(Event{InternalFault{api::FaultType::Internal, std::string{"boom"}}});
        CHECK(machine.get_current_state_id() == api::FsmState::Faulted);
        CHECK(released());
    }
}

TEST_CASE("a fault releases CP C", "[evctl][session-end][fault]") {
    TestFixture fx;
    const auto last_cp = [&] {
        const auto it = std::find_if(fx.mocks.bsp.records.rbegin(), fx.mocks.bsp.records.rend(),
                                     [](const std::string& r) { return r.rfind("set_cp_state", 0) == 0; });
        return it == fx.mocks.bsp.records.rend() ? std::string{} : *it;
    };

    SECTION("after a DC power ready") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        Machine machine{std::make_unique<V2GNegotiating>(*ctx)};
        machine.feed(Event{IsoPowerReadyEvt{}});
        REQUIRE(last_cp() == "set_cp_state(cp_state=C)");
        machine.feed(Event{InternalFault{api::FaultType::Internal, std::string{"boom"}}});
        CHECK(machine.get_current_state_id() == api::FsmState::Faulted);
        CHECK(last_cp() == cp_b);
    }

    SECTION("from a pause that holds C") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.iso_session_active = true;
        ctx->set_cp(::types::ev_board_support::EvCpState::C);
        Machine machine{std::make_unique<Paused>(*ctx)};
        REQUIRE(last_cp() == "set_cp_state(cp_state=C)");
        machine.feed(Event{InternalFault{api::FaultType::Internal, std::string{"boom"}}});
        CHECK(machine.get_current_state_id() == api::FsmState::Faulted);
        CHECK(last_cp() == cp_b);
    }

    SECTION("when a handler throws while C is held") {
        PeerActions actions = make_peer_actions(fx.mocks);
        bool thrown = false;
        actions.iso.stop_charging = [&] {
            fx.mocks.iso.call_stop_charging();
            if (!std::exchange(thrown, true)) {
                throw std::runtime_error("simulated peer action failure");
            }
        };
        FsmContext ctx{std::move(actions),
                       [&fx](const std::string& topic, const std::string& payload) { fx.sink(topic, payload); },
                       [&fx](std::chrono::milliseconds ms) { fx.timer.arm(ms); },
                       [&fx]() { fx.timer.cancel(); },
                       [&fx](int ms) { fx.timer.arm_tick(ms); },
                       [&fx]() { fx.timer.disarm_tick(); },
                       [&fx](Event ev) { fx.timer.record_enqueue(std::move(ev)); },
                       [&fx](std::chrono::milliseconds ms) { fx.timer.arm_curve(ms); },
                       fx.cfg,
                       fx.topics};
        set_mode(ctx, api::ChargeMode::DcIso2);
        ctx.vars.iso_session_active = true;
        auto fsm = std::make_unique<Machine>(std::make_unique<Charging>(ctx));
        REQUIRE(last_cp() == "set_cp_state(cp_state=C)");
        feed_with_fault_isolation(fsm, ctx, Event{EventKind::StopSession});
        REQUIRE(thrown);
        CHECK(fsm->get_current_state_id() == api::FsmState::Faulted);
        CHECK(last_cp() == cp_b);
    }
}

TEST_CASE("a pause releases CP C when PowerDelivery(Stop) is accepted", "[evctl][session-end][stop-hold]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.iso_session_active = true;
    Machine machine{std::make_unique<Paused>(*ctx)};
    CHECK_FALSE(contains_substr(fx.mocks.bsp.records, cp_b));
    machine.feed(Event{IsoPowerDeliveryStoppedEvt{}});
    CHECK(machine.get_current_state_id() == api::FsmState::Paused);
    CHECK(contains_substr(fx.mocks.bsp.records, cp_b));
}

TEST_CASE("an ISO 15118-20 AC pause releases CP C at once", "[evctl][session-end][stop-hold]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::AcIsoD20);
    ctx->vars.iso_session_active = true;
    Machine machine{std::make_unique<Paused>(*ctx)};
    CHECK(contains_substr(fx.mocks.bsp.records, cp_b));
}
