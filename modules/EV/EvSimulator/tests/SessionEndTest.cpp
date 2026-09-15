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
namespace api = everest::lib::API::V1_0::types::ev_simulator;

namespace {

thread_local std::optional<std::chrono::steady_clock::time_point> controlled_now;

struct ControlledClock {
    ControlledClock() {
        set(std::chrono::milliseconds(0));
    }
    ~ControlledClock() {
        controlled_now.reset();
    }
    void set(std::chrono::milliseconds elapsed) {
        controlled_now = std::chrono::steady_clock::time_point{elapsed};
    }
};

} // namespace

// The test target wraps libstdc++'s steady_clock::now; other tests and threads use real time.
extern "C" std::chrono::steady_clock::time_point __real__ZNSt6chrono3_V212steady_clock3nowEv() noexcept;
extern "C" std::chrono::steady_clock::time_point __wrap__ZNSt6chrono3_V212steady_clock3nowEv() noexcept {
    if (controlled_now) {
        return *controlled_now;
    }
    return __real__ZNSt6chrono3_V212steady_clock3nowEv();
}

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

// Runs a transition the way the FSM does: leave the old state, enter the new one.
StateBase::ContainerType take(StateBase& from, StateBase::Result result) {
    REQUIRE(result.new_state);
    from.leave();
    result.new_state->enter();
    return std::move(result.new_state);
}

const std::string cp_a = "set_cp_state(cp_state=A)";
const std::string cp_b = "set_cp_state(cp_state=B)";

using Machine = fsm::v2::FSM<StateBase>;

// Feeds what was queued, oldest first, as EvSimRuntime::on_wake drains its queue, with a
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

TEST_CASE("an EV stays plugged when its session ends", "[evsim][session-end]") {
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
        CHECK(ctx->persisted_state().plugged_in == true);
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

    SECTION("a scenario is refused while held, naming the hold") {
        auto ctx = fx.make_ctx();
        ctx->vars.hold_session = true;
        Plugged plugged{*ctx};

        plugged.feed(Event{api::RunScenarioParams{api::ScenarioName::AcIecBasic, std::nullopt}});

        const auto ack = payload_for(fx.sink, fx.topics.everest_to_extern("command_ack"));
        CHECK(ack.find("plugged with no session") != std::string::npos);
        CHECK(ack.find("no session active") == std::string::npos);
    }

    SECTION("a fresh plug still begins a session") {
        auto ctx = fx.make_ctx();
        Plugged plugged{*ctx};

        plugged.enter();

        CHECK(begin_session_count(fx.timer) == 1);
    }
}

TEST_CASE("a BeginSession queued by an earlier plug begins nothing", "[evsim][session-end][begin-session]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    const auto queue_replug = [&] {
        ctx->enqueue(Event{PlugCmd{}});
        ctx->enqueue(Event{UnplugCmd{}});
        ctx->enqueue(Event{PlugCmd{}});
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

TEST_CASE("a plug while held is a real replug", "[evsim][session-end][replug]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->vars.hold_session = true;
    Plugged held{*ctx};
    held.enter();
    REQUIRE(begin_session_count(fx.timer) == 0);
    fx.mocks.bsp.records.clear();
    fx.timer.clear();

    auto unplugged = take(held, held.feed(Event{EventKind::Plug}));
    REQUIRE(unplugged->get_id() == api::FsmState::Unplugged);

    SECTION("CP A for the dwell, then B and a new session on its deadline") {
        CHECK(contains_substr(fx.mocks.bsp.records, cp_a));
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, cp_b));
        REQUIRE(fx.timer.state_timer_arms.size() == 1);
        CHECK(fx.timer.state_timer_arms[0] == Unplugged::replug_dwell);
        CHECK(begin_session_count(fx.timer) == 0);
        CHECK(count_kind(fx.timer, EventKind::Plug) == 0);

        auto plugged = take(*unplugged, unplugged->feed(Event{EventKind::StateDeadline}));

        CHECK(plugged->get_id() == api::FsmState::Plugged);
        CHECK(index_of_substr(fx.mocks.bsp.records, cp_a) < index_of_substr(fx.mocks.bsp.records, cp_b));
        CHECK(begin_session_count(fx.timer) == 1);
        CHECK_FALSE(ctx->vars.hold_session);
    }

    SECTION("an unplug during the dwell cancels the replug") {
        const auto cancels = fx.timer.state_timer_cancels;

        auto result = unplugged->feed(Event{EventKind::Unplug});

        CHECK(result.new_state == nullptr);
        CHECK_FALSE(result.unhandled);
        CHECK(fx.timer.state_timer_cancels == cancels + 1);
        CHECK(ctx->persisted_state().plugged_in == false);
        auto late = unplugged->feed(Event{EventKind::StateDeadline});
        CHECK(late.new_state == nullptr);
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, cp_b));
    }

    SECTION("a disable during the dwell leaves Unplugged, which cancels the replug") {
        const auto cancels = fx.timer.state_timer_cancels;

        auto disabled = take(*unplugged, unplugged->feed(Event{EventKind::Disable}));

        CHECK(disabled->get_id() == api::FsmState::Disabled);
        CHECK(fx.timer.state_timer_cancels == cancels + 1);
        CHECK(ctx->persisted_state().plugged_in == false);
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, cp_b));
    }

    SECTION("a plug during the dwell waits for it") {
        auto result = unplugged->feed(Event{EventKind::Plug});

        CHECK(result.new_state == nullptr);
        CHECK_FALSE(result.unhandled);
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, cp_b));
    }
}

TEST_CASE("the forced end of a stop stops the V2G session again", "[evsim][session-end]") {
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

TEST_CASE("a scenario outlives a stop that leaves the EV plugged", "[evsim][session-end][scenario]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->scenario.start(api::ScenarioName::DcIsoD20Basic, std::nullopt, *ctx);
    ctx->scenario.on_timer_fire(*ctx);
    REQUIRE(count_kind(fx.timer, EventKind::StopSession) == 1);
    set_mode(*ctx, api::ChargeMode::DcIsoD20);
    Charging charging{*ctx};

    auto stopping = take(charging, charging.feed(Event{EventKind::StopSession}));
    auto plugged = take(*stopping, stopping->feed(Event{EventKind::IsoV2GFinished}));
    REQUIRE(plugged->get_id() == api::FsmState::Plugged);

    CHECK(ctx->scenario.active());
    ctx->scenario.on_timer_fire(*ctx);
    REQUIRE(count_kind(fx.timer, EventKind::Unplug) == 1);

    auto unplugged = take(*plugged, plugged->feed(Event{EventKind::Unplug}));
    CHECK(unplugged->get_id() == api::FsmState::Unplugged);
    CHECK_FALSE(ctx->scenario.active());
}

TEST_CASE("a scenario does not outlive a replug", "[evsim][session-end][scenario][replug]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->scenario.start(api::ScenarioName::AcIecPauseResume, std::nullopt, *ctx);
    ctx->vars.hold_session = true;
    Plugged held{*ctx};
    held.enter();
    REQUIRE(ctx->scenario.active());

    auto unplugged = take(held, held.feed(Event{EventKind::Plug}));

    REQUIRE(unplugged->get_id() == api::FsmState::Unplugged);
    CHECK_FALSE(ctx->scenario.active());
}

TEST_CASE("a session's charging curve ends with the session", "[evsim][session-end][scenario][curve]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    const auto ack_topic = fx.topics.everest_to_extern("command_ack");
    const auto rejected_acks = [&] {
        return std::count_if(fx.sink.records.begin(), fx.sink.records.end(), [&](const auto& kv) {
            return kv.first == ack_topic && kv.second.find("Rejected") != std::string::npos;
        });
    };
    // Drives the scenario timer the way the runtime does, feeding what it enqueues to `state`;
    // returns how many SetChargingCurrent steps it fed.
    const auto fire_into = [&](StateBase& state, int fires) {
        int set_current = 0;
        for (int i = 0; i < fires; ++i) {
            fx.timer.enqueued_events.clear();
            ctx->scenario.on_timer_fire(*ctx);
            set_current += count_kind(fx.timer, EventKind::SetChargingCurrent);
            for (const auto& ev : fx.timer.enqueued_events) {
                REQUIRE(state.feed(ev).new_state == nullptr);
            }
        }
        return set_current;
    };

    SECTION("a looping curve stops at a stop that leaves the EV plugged") {
        api::AcIecSessionParams params{};
        params.curve = api::ChargingCurve{{{0, 10.0f, true, std::nullopt}, {100, 12.0f, true, std::nullopt}}, true};
        ctx->configured_session = api::SessionConfigParams{params};
        Plugged plugged{*ctx};
        REQUIRE(plugged.feed(Event{BeginSessionEvt{}}).new_state == nullptr);
        Charging charging{*ctx};
        charging.enter();
        ctx->scenario.on_timer_fire(*ctx);
        REQUIRE(count_kind(fx.timer, EventKind::SetChargingCurrent) >= 2);

        auto held = take(charging, charging.feed(Event{EventKind::StopSession}));
        REQUIRE(held->get_id() == api::FsmState::Plugged);
        fx.sink.records.clear();

        CHECK(fire_into(*held, 20) == 0);
        CHECK(rejected_acks() == 0);
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("a looping curve inside a preset does not replay after the preset's later steps") {
        ctx->scenario.start(api::ScenarioName::AcIecPauseResume, std::nullopt, *ctx);
        api::AcIecSessionParams params{};
        params.curve = api::ChargingCurve{{{0, 10.0f, true, std::nullopt}, {100, 12.0f, true, std::nullopt}}, true};
        ctx->configured_session = api::SessionConfigParams{params};
        Plugged plugged{*ctx};
        REQUIRE(plugged.feed(Event{BeginSessionEvt{}}).new_state == nullptr);
        Charging charging{*ctx};
        charging.enter();

        auto held = take(charging, charging.feed(Event{EventKind::StopSession}));
        REQUIRE(held->get_id() == api::FsmState::Plugged);
        fx.timer.enqueued_events.clear();
        for (int i = 0; i < 20; ++i) {
            ctx->scenario.on_timer_fire(*ctx);
        }

        std::vector<EventKind> kinds;
        for (const auto& ev : fx.timer.enqueued_events) {
            kinds.push_back(kind_of(ev));
        }
        CHECK(kinds == std::vector<EventKind>{EventKind::PauseSession, EventKind::ResumeSession, EventKind::StopSession,
                                              EventKind::Unplug});
    }

    SECTION("the preset's own stop and unplug still fire, on the preset's clock") {
        ctx->scenario.start(api::ScenarioName::AcIecRampUp, std::nullopt, *ctx);
        REQUIRE(kind_of(fx.timer.enqueued_events.at(0)) == EventKind::ConfigureSession);
        ctx->configure_session(std::get<api::SessionConfigParams>(fx.timer.enqueued_events[0].payload));
        Plugged plugged{*ctx};
        REQUIRE(plugged.feed(Event{BeginSessionEvt{}}).new_state == nullptr);
        Charging charging{*ctx};
        charging.enter();
        fx.timer.enqueued_events.clear();
        ctx->scenario.on_timer_fire(*ctx);
        REQUIRE(count_kind(fx.timer, EventKind::SetChargingCurrent) == 1);

        auto held = take(charging, charging.feed(Event{EventKind::StopSession}));
        REQUIRE(held->get_id() == api::FsmState::Plugged);
        // Re-armed for the preset's stop at 60 s, not the dropped curve point at 8 s.
        CHECK(fx.timer.scenario_timer_arms.back() > std::chrono::milliseconds(50000));
        fx.timer.enqueued_events.clear();
        ctx->scenario.on_timer_fire(*ctx);
        ctx->scenario.on_timer_fire(*ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::StopSession);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::Unplug);
        CHECK_FALSE(ctx->scenario.active());
        auto unplugged = take(*held, held->feed(Event{EventKind::Unplug}));
        CHECK(unplugged->get_id() == api::FsmState::Unplugged);
    }
}

TEST_CASE("a looping curve's rewinds leave a preset's steps on the preset's clock",
          "[evsim][session-end][scenario][curve]") {
    using std::chrono::milliseconds;
    TestFixture fx;
    auto ctx = fx.make_ctx();
    Machine machine{std::make_unique<Unplugged>(*ctx)};
    api::ScenarioTimingOverrides timing;
    timing.stop_after_ms = 1000;
    timing.unplug_after_ms = 1500;
    api::AcIecSessionParams params{};
    params.curve = api::ChargingCurve{{{0, 10.0f, true, std::nullopt}, {100, 12.0f, true, std::nullopt}}, true};
    const auto since_start = [start = std::chrono::steady_clock::now()] {
        return std::chrono::duration_cast<milliseconds>(std::chrono::steady_clock::now() - start);
    };

    machine.feed(Event{api::RunScenarioParams{api::ScenarioName::AcIecRampUp, timing}});
    // Replaces the preset's own curve before the plug's session begins.
    ctx->enqueue(Event{api::SessionConfigParams{params}});
    drain(fx.timer, *ctx, machine);
    machine.feed(pwm_measurement());
    REQUIRE(machine.get_current_state_id() == api::FsmState::Charging);
    int set_current = count_kind(fx.timer, EventKind::SetChargingCurrent);
    drain(fx.timer, *ctx, machine);
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(milliseconds(100));
        ctx->scenario.on_timer_fire(*ctx);
        set_current += count_kind(fx.timer, EventKind::SetChargingCurrent);
        drain(fx.timer, *ctx, machine);
    }
    // More points than the curve has: it rewound.
    REQUIRE(set_current > 2);

    machine.feed(Event{StopSessionCmd{}});
    REQUIRE(machine.get_current_state_id() == api::FsmState::Plugged);
    const auto stop_elapsed = since_start();
    const auto stop_arm = fx.timer.scenario_timer_arms.back();
    CHECK(stop_arm >= milliseconds(1000) - stop_elapsed);
    CHECK(stop_arm <= milliseconds(1000) - stop_elapsed + milliseconds(30));

    ctx->scenario.on_timer_fire(*ctx);
    REQUIRE(count_kind(fx.timer, EventKind::StopSession) == 1);
    const auto unplug_elapsed = since_start();
    const auto unplug_arm = fx.timer.scenario_timer_arms.back();
    CHECK(unplug_arm >= milliseconds(1500) - unplug_elapsed);
    CHECK(unplug_arm <= milliseconds(1500) - unplug_elapsed + milliseconds(30));
}

TEST_CASE("a looping curve inside a preset runs until the preset's own stop", "[evsim][session-end][scenario][curve]") {
    using std::chrono::milliseconds;
    TestFixture fx;
    auto ctx = fx.make_ctx();
    Machine machine{std::make_unique<Unplugged>(*ctx)};
    api::ScenarioTimingOverrides timing;
    timing.stop_after_ms = 650;
    timing.unplug_after_ms = 950;
    api::AcIecSessionParams params{};
    params.curve = api::ChargingCurve{{{0, 10.0f, true, std::nullopt}, {100, 12.0f, true, std::nullopt}}, true};
    const auto since_start = [start = std::chrono::steady_clock::now()] {
        return std::chrono::duration_cast<milliseconds>(std::chrono::steady_clock::now() - start);
    };

    machine.feed(Event{api::RunScenarioParams{api::ScenarioName::AcIecRampUp, timing}});
    ctx->enqueue(Event{api::SessionConfigParams{params}});
    drain(fx.timer, *ctx, machine);
    machine.feed(pwm_measurement());
    REQUIRE(machine.get_current_state_id() == api::FsmState::Charging);
    drain(fx.timer, *ctx, machine);

    bool stop_drained = false;
    bool unplug_delivered = false;
    // A late callback can queue curve points and teardown together. Only callbacks after
    // draining that batch must stop producing curve points.
    while (ctx->scenario.active() && since_start() < milliseconds(5000)) {
        REQUIRE_FALSE(fx.timer.scenario_timer_arms.empty());
        const auto arm = fx.timer.scenario_timer_arms.back();
        REQUIRE(arm > milliseconds(0));
        std::this_thread::sleep_for(arm);
        ctx->scenario.on_timer_fire(*ctx);
        if (stop_drained) {
            CHECK(count_kind(fx.timer, EventKind::SetChargingCurrent) == 0);
        }
        while (!fx.timer.enqueued_events.empty()) {
            auto ev = std::move(fx.timer.enqueued_events.front());
            fx.timer.enqueued_events.erase(fx.timer.enqueued_events.begin());
            const auto kind = kind_of(ev);
            machine.feed(ev);
            if (kind == EventKind::StopSession) {
                CHECK_FALSE(stop_drained);
                CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
                CHECK(ctx->vars.hold_session);
                CHECK_FALSE(ctx->vars.session.has_value());
                stop_drained = true;
            } else if (kind == EventKind::Unplug) {
                CHECK(stop_drained);
                CHECK_FALSE(unplug_delivered);
                CHECK(machine.get_current_state_id() == api::FsmState::Unplugged);
                unplug_delivered = true;
            }
        }
    }

    CHECK(stop_drained);
    CHECK(unplug_delivered);
    CHECK_FALSE(ctx->scenario.active());
    CHECK(machine.get_current_state_id() == api::FsmState::Unplugged);
    ctx->scenario.on_timer_fire(*ctx);
    CHECK(fx.timer.enqueued_events.empty());
}

TEST_CASE("a looping curve preserves preset deadlines with a controlled clock",
          "[evsim][session-end][scenario][curve][controlled-clock]") {
    using std::chrono::milliseconds;
    ControlledClock clock;
    TestFixture fx;
    auto ctx = fx.make_ctx();
    Machine machine{std::make_unique<Unplugged>(*ctx)};
    api::ScenarioTimingOverrides timing;
    timing.stop_after_ms = 650;
    timing.unplug_after_ms = 950;
    api::AcIecSessionParams params{};
    params.curve = api::ChargingCurve{{{0, 10.0f, true, std::nullopt}, {100, 12.0f, true, std::nullopt}}, true};

    machine.feed(Event{api::RunScenarioParams{api::ScenarioName::AcIecRampUp, timing}});
    ctx->enqueue(Event{api::SessionConfigParams{params}});
    drain(fx.timer, *ctx, machine);
    machine.feed(pwm_measurement());
    REQUIRE(machine.get_current_state_id() == api::FsmState::Charging);
    drain(fx.timer, *ctx, machine);

    const auto fire_at = [&](int elapsed) {
        clock.set(milliseconds(elapsed));
        fx.timer.scenario_timer_arms.clear();
        ctx->scenario.on_timer_fire(*ctx);
    };
    const auto check_arm = [&](int delay) {
        REQUIRE(fx.timer.scenario_timer_arms.size() == 1);
        CHECK(fx.timer.scenario_timer_arms.back() == milliseconds(delay));
    };
    // The preset initially arms its stop; entering Charging replaces it with the curve timer.
    REQUIRE(fx.timer.scenario_timer_arms == std::vector<milliseconds>{milliseconds(650), milliseconds(100)});
    for (int elapsed : {100, 200, 300, 400, 500}) {
        fire_at(elapsed);
        CHECK(count_kind(fx.timer, EventKind::SetChargingCurrent) == 2);
        CHECK(count_kind(fx.timer, EventKind::StopSession) == 0);
        check_arm(100);
        drain(fx.timer, *ctx, machine);
    }

    SECTION("on-time callbacks arm the stop at 650 ms") {
        fire_at(600);
        CHECK(count_kind(fx.timer, EventKind::SetChargingCurrent) == 2);
        CHECK(count_kind(fx.timer, EventKind::StopSession) == 0);
        check_arm(50);
        drain(fx.timer, *ctx, machine);
        fire_at(650);
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        CHECK(count_kind(fx.timer, EventKind::StopSession) == 1);
        drain(fx.timer, *ctx, machine);
        // Draining StopSession drops the curve and re-arms for the original unplug deadline.
        CHECK(fx.timer.scenario_timer_arms == std::vector<milliseconds>{milliseconds(50), milliseconds(300)});
    }
    SECTION("the callback due at 600 ms arrives at 700 ms and flushes the stop in its batch") {
        fire_at(700);
        REQUIRE(fx.timer.enqueued_events.size() == 3);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::SetChargingCurrent);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::StopSession);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::SetChargingCurrent);
        check_arm(100);
        drain(fx.timer, *ctx, machine);
        CHECK(fx.timer.scenario_timer_arms == std::vector<milliseconds>{milliseconds(100), milliseconds(250)});
    }

    CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
    CHECK_FALSE(ctx->vars.session.has_value());
    fire_at(950);
    REQUIRE(fx.timer.enqueued_events.size() == 1);
    CHECK(count_kind(fx.timer, EventKind::Unplug) == 1);
    CHECK(fx.timer.scenario_timer_arms.empty());
    drain(fx.timer, *ctx, machine);
    CHECK(machine.get_current_state_id() == api::FsmState::Unplugged);
    CHECK_FALSE(ctx->scenario.active());
    fire_at(1200);
    CHECK(fx.timer.enqueued_events.empty());
}

TEST_CASE("an unplug during an ISO session still ends unplugged", "[evsim][session-end]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);

    SECTION("from Charging") {
        Charging charging{*ctx};
        CHECK(finish_stop(charging.feed(Event{EventKind::Unplug})) == api::FsmState::Unplugged);
    }

    SECTION("from Paused") {
        Paused paused{*ctx};
        CHECK(finish_stop(paused.feed(Event{EventKind::Unplug})) == api::FsmState::Unplugged);
    }

    SECTION("from V2GNegotiating") {
        V2GNegotiating negotiating{*ctx};
        CHECK(finish_stop(negotiating.feed(Event{EventKind::Unplug})) == api::FsmState::Unplugged);
    }

    SECTION("an unplug that arrives while Stopping") {
        Stopping stopping{*ctx};

        auto during = stopping.feed(Event{EventKind::Unplug});
        CHECK(during.new_state == nullptr);
        CHECK_FALSE(during.unhandled);

        auto result = stopping.feed(Event{EventKind::StateDeadline});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("the flag does not outlive an unplug that ends the stop another way") {
        Charging charging{*ctx};
        auto first_stop = take(charging, charging.feed(Event{EventKind::Unplug}));
        REQUIRE(first_stop->get_id() == api::FsmState::Stopping);
        const ::types::board_support_common::BspEvent disconnected{::types::board_support_common::Event::Disconnected};
        auto unplugged = take(*first_stop, first_stop->feed(Event{BspEventPayload{disconnected}}));
        REQUIRE(unplugged->get_id() == api::FsmState::Unplugged);

        Stopping stopping{*ctx};
        auto result = stopping.feed(Event{EventKind::IsoV2GFinished});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Plugged);
    }
}
