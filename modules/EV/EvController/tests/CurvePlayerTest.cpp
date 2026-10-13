// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/CurvePlayer.hpp"
#include "../main/FsmContext.hpp"
#include "../main/states/Charging.hpp"
#include "../main/states/Plugged.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <everest_api_types/ev_controller/codec.hpp>

#include <algorithm>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_controller;

namespace {

// arm_next computes delay = due_at(step) - elapsed where `elapsed` is the time between
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

TEST_CASE("CurvePlayer ChargingCurve handling", "[evctl][curve][curve]") {
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
        // Plugged stashes the curve; the append happens on Charging::enter.
        REQUIRE(ctx->vars.pending_curve().has_value());
        CHECK_FALSE(ctx->curve.active());

        Charging charging{*ctx};
        charging.enter();

        // Three SetChargingCurrent steps were appended to the dispatcher.
        REQUIRE(ctx->curve.active());
        // Fire timer 3 times to flush the curve steps.
        ctx->curve.on_timer_fire(*ctx); // +1000ms
        ctx->curve.on_timer_fire(*ctx); // +5000ms
        ctx->curve.on_timer_fire(*ctx); // +10000ms

        REQUIRE(fx.timer.enqueued_events.size() == 3);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::SetChargingCurrent);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::SetChargingCurrent);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::SetChargingCurrent);

        // First arm is for first curve point at +1000ms (ms precision).
        REQUIRE(fx.timer.curve_timer_arms.size() >= 1);
        CHECK(arm_close_to(fx.timer.curve_timer_arms[0], std::chrono::milliseconds(1000)));

        // SetChargingCurrent payload carries current_a from curve.
        const auto& p0 = std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[0].payload);
        CHECK(p0.current_a == 11.0f); // 10 + 1000/1000
        CHECK(p0.three_phases == true);
        const auto& p2 = std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[2].payload);
        CHECK(p2.current_a == 20.0f); // 10 + 10000/1000

        CHECK_FALSE(ctx->curve.active());
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
        // Plugged stashes the curve; the append happens on Charging::enter.
        REQUIRE(ctx->vars.pending_curve().has_value());

        Charging charging{*ctx};
        charging.enter();
        REQUIRE(ctx->curve.active());

        // Fire 1: cycle 1, point 0 enqueues current=11.
        ctx->curve.on_timer_fire(*ctx);
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        CHECK(std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[0].payload).current_a == 11.0f);

        // Fire 2: cycle 1 point 1 enqueues current=13, then the loop rewinds
        // and arm_next fires the rewound first step (current=11) inline since
        // its zero-delay slot would otherwise disarm the timer.
        ctx->curve.on_timer_fire(*ctx);
        REQUIRE(fx.timer.enqueued_events.size() == 3);
        CHECK(std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[1].payload).current_a == 13.0f);
        CHECK(std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[2].payload).current_a == 11.0f);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::SetChargingCurrent);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::SetChargingCurrent);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::SetChargingCurrent);

        // Dispatcher stays active across the wrap and the post-rewind arm is
        // strictly positive (otherwise the underlying timerfd would disarm).
        CHECK(ctx->curve.active());
        CHECK(fx.timer.curve_timer_arms.back() > std::chrono::milliseconds(0));
    }
}

TEST_CASE("CurvePlayer rearm precision and loop disarm", "[evctl][curve][rearm]") {
    TestFixture fx;

    // Build a 3-step list using append_steps so the dispatcher seeds start_at_
    // to "now" and treats offsets as relative to it. mark_loop(1, 3) marks
    // [Pause, Resume] as the loop body; the offset-0 step fires inline and
    // is outside the loop.
    auto make_three_step_list = []() {
        std::vector<CurveStep> steps;
        Event current;
        current = Event{EventKind::SetChargingCurrent};
        Event pause;
        pause = Event{EventKind::PauseSession};
        Event resume;
        resume = Event{EventKind::ResumeSession};
        steps.push_back({std::chrono::milliseconds(0), std::move(current)});
        steps.push_back({std::chrono::milliseconds(100), std::move(pause)});
        steps.push_back({std::chrono::milliseconds(200), std::move(resume)});
        return steps;
    };

    SECTION("Sub-second positive delay arms with millisecond precision") {
        auto ctx = fx.make_ctx();

        std::vector<CurveStep> steps;
        Event pause;
        pause = Event{EventKind::PauseSession};
        steps.push_back({std::chrono::milliseconds(250), std::move(pause)});
        ctx->curve.append_steps(std::move(steps), false, *ctx);

        // No offset-0 step, so nothing has been enqueued yet.
        CHECK(fx.timer.enqueued_events.empty());
        REQUIRE(fx.timer.curve_timer_arms.size() == 1);
        // Should arm a ms-precision delay close to 250ms, not rounded up to 1s.
        CHECK(fx.timer.curve_timer_arms[0] <= std::chrono::milliseconds(250));
        CHECK(fx.timer.curve_timer_arms[0] > std::chrono::milliseconds(0));
    }

    SECTION("Loop rewind arms strictly positive delay (does not disarm)") {
        auto ctx = fx.make_ctx();

        ctx->curve.append_steps(make_three_step_list(), false, *ctx);
        ctx->curve.mark_loop(1, 3);

        // Offset-0 current fired inline; first arm is for PauseSession at +100ms.
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::SetChargingCurrent);
        REQUIRE(fx.timer.curve_timer_arms.size() == 1);

        fx.timer.curve_timer_arms.clear();

        // Fire 1: PauseSession enqueued, arm for ResumeSession at +200ms.
        ctx->curve.on_timer_fire(*ctx);
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::PauseSession);
        REQUIRE(fx.timer.curve_timer_arms.size() == 1);

        fx.timer.curve_timer_arms.clear();

        // Fire 2: ResumeSession completes the loop body. Its shift makes PauseSession
        // due immediately without changing start_at_; arm_next flushes it inline and
        // arms a positive delay for ResumeSession so the timerfd does not disarm.
        ctx->curve.on_timer_fire(*ctx);
        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::ResumeSession);
        CHECK(kind_of(fx.timer.enqueued_events[3]) == EventKind::PauseSession);
        CHECK(ctx->curve.active());
        REQUIRE(fx.timer.curve_timer_arms.size() == 1);
        CHECK(fx.timer.curve_timer_arms[0] > std::chrono::milliseconds(0));
    }

    SECTION("Loop emits its body again on the second pass") {
        auto ctx = fx.make_ctx();

        ctx->curve.append_steps(make_three_step_list(), false, *ctx);
        ctx->curve.mark_loop(1, 3);

        // Fire 1: cycle 1 Pause.
        ctx->curve.on_timer_fire(*ctx);
        // Fire 2: cycle 1 Resume, loop rewinds and arm_next fires the rewound
        // cycle 2 Pause inline (its post-rewind delay is zero).
        ctx->curve.on_timer_fire(*ctx);

        REQUIRE(fx.timer.enqueued_events.size() == 4);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::SetChargingCurrent);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::PauseSession);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::ResumeSession);
        CHECK(kind_of(fx.timer.enqueued_events[3]) == EventKind::PauseSession);
        CHECK(ctx->curve.active());
    }

    SECTION("Past-due step fires inline without arming a timer") {
        auto ctx = fx.make_ctx();

        std::vector<CurveStep> steps;
        Event current;
        current = Event{EventKind::SetChargingCurrent};
        Event pause;
        pause = Event{EventKind::PauseSession};
        // Both steps are at offset 0 (past-due immediately).
        steps.push_back({std::chrono::milliseconds(0), std::move(current)});
        steps.push_back({std::chrono::milliseconds(0), std::move(pause)});
        ctx->curve.append_steps(std::move(steps), false, *ctx);

        // Both steps fired inline; no timer arm recorded.
        REQUIRE(fx.timer.enqueued_events.size() == 2);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::SetChargingCurrent);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::PauseSession);
        CHECK(fx.timer.curve_timer_arms.empty());
        CHECK_FALSE(ctx->curve.active());
    }
}

TEST_CASE("CurvePlayer forward-progress guard", "[evctl][curve][loop-guard]") {
    TestFixture fx;

    SECTION("A single-step loop has zero duration and disarms the loop") {
        auto ctx = fx.make_ctx();

        // Use a small positive offset so the dispatcher arms a timer first
        // (rather than flushing inline). Once the timer fires and the loop
        // rewinds, its only step is due immediately again. Without the guard
        // this spins forever.
        std::vector<CurveStep> steps;
        Event pause;
        pause = Event{EventKind::PauseSession};
        steps.push_back({std::chrono::milliseconds(10), std::move(pause)});
        ctx->curve.append_steps(std::move(steps), false, *ctx);
        ctx->curve.mark_loop(0, 1);

        REQUIRE(fx.timer.curve_timer_arms.size() == 1);
        REQUIRE(fx.timer.enqueued_events.empty());

        fx.timer.curve_timer_arms.clear();

        // Simulate the timer firing. on_timer_fire enqueues once, rewinds,
        // then arm_next would spin without the guard.
        ctx->curve.on_timer_fire(*ctx);

        // The guard bounds the inline iterations and then disarms the loop. Arming 1 ms and
        // returning would leave the dispatcher re-arming at 1 kHz forever, which is the guard
        // failing open: the run is already wrong and the only way to stop it is to stop looping.
        CHECK(fx.timer.curve_timer_arms.empty());
        CHECK_FALSE(ctx->curve.active());
        // Many copies of the step were enqueued before the guard tripped, bounded by
        // steps_.size() + 128 + 1 ~= 130 here. Includes the initial on_timer_fire enqueue and
        // the inline-flushed copies.
        CHECK(fx.timer.enqueued_events.size() > 1);
        CHECK(fx.timer.enqueued_events.size() < 200);
        for (const auto& ev : fx.timer.enqueued_events) {
            CHECK(kind_of(ev) == EventKind::PauseSession);
        }

        // And it stays stopped: a second fire has nothing left to dispatch.
        const auto enqueued_after_guard = fx.timer.enqueued_events.size();
        ctx->curve.on_timer_fire(*ctx);
        CHECK(fx.timer.enqueued_events.size() == enqueued_after_guard);
        CHECK(fx.timer.curve_timer_arms.empty());
    }
    SECTION("Well-formed 5-step at-zero scenario completes without tripping the guard") {
        auto ctx = fx.make_ctx();

        std::vector<CurveStep> steps;
        for (int i = 0; i < 5; ++i) {
            Event ev;
            ev = Event{EventKind::SetChargingCurrent};
            steps.push_back({std::chrono::milliseconds(0), std::move(ev)});
        }
        ctx->curve.append_steps(std::move(steps), false, *ctx);
        // No mark_loop: all five steps flush inline and the dispatcher
        // goes idle. The guard must not interfere.
        CHECK(fx.timer.enqueued_events.size() == 5);
        CHECK(fx.timer.curve_timer_arms.empty());
        CHECK_FALSE(ctx->curve.active());
    }

    SECTION("mark_loop with begin >= end leaves loop unset") {
        auto ctx = fx.make_ctx();

        std::vector<CurveStep> steps;
        Event a;
        a = Event{EventKind::SetChargingCurrent};
        Event b;
        b = Event{EventKind::PauseSession};
        steps.push_back({std::chrono::milliseconds(0), std::move(a)});
        steps.push_back({std::chrono::milliseconds(0), std::move(b)});
        ctx->curve.append_steps(std::move(steps), false, *ctx);

        // begin == end (degenerate).
        ctx->curve.mark_loop(1, 1);
        // begin > end.
        ctx->curve.mark_loop(2, 1);

        // Both calls must be ignored; the dispatcher already flushed both
        // steps inline and is idle. If the loop were set, arm_next on the
        // next round (e.g. via on_timer_fire) would attempt a rewind; but
        // since we are idle there is nothing more to fire. Verify via
        // a follow-up timer-fire that nothing additional happens.
        ctx->curve.on_timer_fire(*ctx);
        CHECK(fx.timer.enqueued_events.size() == 2);
        CHECK_FALSE(ctx->curve.active());
    }

    SECTION("Loop body preserves non-trivial payload across rewinds") {
        // Guards against the latent move-from-inside-loop bug: dispatching a
        // looped step via std::move would hollow out its stored event so the
        // next iteration would see an empty payload. The fix copies inside
        // the loop body. We exercise it with SessionConfigParams carrying a
        // ChargingCurve (variant alternative with a nested vector); not a
        // realistic scenario, but the only easy way to surface the UB with
        // the current EventKind set.
        auto ctx = fx.make_ctx();

        std::vector<CurveStep> steps;
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

        ctx->curve.append_steps(std::move(steps), false, *ctx);
        ctx->curve.mark_loop(0, 1);

        REQUIRE(fx.timer.curve_timer_arms.size() == 1);
        REQUIRE(fx.timer.enqueued_events.empty());

        // The first fire repeats the zero-duration body until the guard clears the loop;
        // the second fire is a no-op. Every queued copy must retain the full curve payload.
        ctx->curve.on_timer_fire(*ctx);
        ctx->curve.on_timer_fire(*ctx);

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

        std::vector<CurveStep> steps;
        Event a;
        a = Event{EventKind::SetChargingCurrent};
        steps.push_back({std::chrono::milliseconds(100), std::move(a)});
        ctx->curve.append_steps(std::move(steps), false, *ctx);
        REQUIRE(fx.timer.curve_timer_arms.size() == 1);

        // end > step_count (out of bounds).
        ctx->curve.mark_loop(0, 5);
        // begin >= step_count.
        ctx->curve.mark_loop(2, 3);

        fx.timer.curve_timer_arms.clear();
        ctx->curve.on_timer_fire(*ctx);
        // Loop unset, so the single step fires once and the dispatcher
        // goes idle without rearming.
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        CHECK(fx.timer.curve_timer_arms.empty());
        CHECK_FALSE(ctx->curve.active());
    }
}

TEST_CASE("CurvePlayer dispatches appended points before later points", "[evctl][curve][curve][timing]") {
    TestFixture fx;

    // A curve is appended while the preset's teardown is still pending far in
    // the future. The curve lands behind the teardown in the step-list, and its
    // points must still fire at their own due times rather than in one past-due
    // burst once the teardown has fired. These pin WHEN the points fire, not
    // only that four of them eventually do.
    auto seed_with_late_teardown = [](auto& ctx) {
        std::vector<CurveStep> steps;
        Event stop;
        stop = Event{EventKind::StopSession};
        steps.push_back({std::chrono::milliseconds(60000), std::move(stop)});
        ctx->curve.append_steps(std::move(steps), false, *ctx);
    };

    auto make_curve = []() {
        std::vector<CurveStep> curve;
        for (auto offset : {0, 100, 200, 300}) {
            Event e;
            e = Event{EventKind::SetChargingCurrent};
            curve.push_back({std::chrono::milliseconds(offset), std::move(e)});
        }
        return curve;
    };

    SECTION("The curve fires ahead of a later pending step") {
        auto ctx = fx.make_ctx();
        seed_with_late_teardown(ctx);
        REQUIRE(fx.timer.curve_timer_arms.size() == 1);
        // Armed for the teardown at +60s while it is the only step.
        CHECK(fx.timer.curve_timer_arms.back() > std::chrono::milliseconds(50000));

        fx.timer.curve_timer_arms.clear();
        const auto placed = ctx->curve.append_steps(make_curve(), false, *ctx);

        // Appended behind the teardown, which is still pending.
        CHECK(placed == 1);
        CHECK(ctx->curve.step_count() == 5);

        // The offset-0 point is due now and fires inline.
        REQUIRE(fx.timer.enqueued_events.size() == 1);

        // The next arm is the following curve point, not the ~60s teardown.
        REQUIRE(fx.timer.curve_timer_arms.size() == 1);
        CHECK(fx.timer.curve_timer_arms.back() > std::chrono::milliseconds(0));
        CHECK(fx.timer.curve_timer_arms.back() <= std::chrono::milliseconds(100));
    }

    SECTION("Curve offsets are measured from the append, not from playback start") {
        auto ctx = fx.make_ctx();
        seed_with_late_teardown(ctx);
        fx.timer.curve_timer_arms.clear();
        fx.timer.enqueued_events.clear();

        // A single point 200 ms after this call. Measured against scenario
        // start it would be long past due and would fire immediately.
        std::vector<CurveStep> curve;
        Event e;
        e = Event{EventKind::SetChargingCurrent};
        curve.push_back({std::chrono::milliseconds(200), std::move(e)});
        ctx->curve.append_steps(std::move(curve), false, *ctx);

        CHECK(fx.timer.enqueued_events.empty());
        REQUIRE(fx.timer.curve_timer_arms.size() == 1);
        CHECK(fx.timer.curve_timer_arms.back() > std::chrono::milliseconds(100));
        CHECK(fx.timer.curve_timer_arms.back() <= std::chrono::milliseconds(200));
    }
}

TEST_CASE("CurvePlayer fires steps in due-time order", "[evctl][curve][order]") {
    using std::chrono::milliseconds;
    TestFixture fx;
    const auto kinds = [&] {
        std::vector<EventKind> out;
        for (const auto& ev : fx.timer.enqueued_events) {
            out.push_back(kind_of(ev));
        }
        return out;
    };

    SECTION("Steps due together fire in insertion order") {
        auto ctx = fx.make_ctx();
        std::vector<CurveStep> steps;
        steps.push_back({milliseconds(100), Event{EventKind::SetChargingCurrent}});
        steps.push_back({milliseconds(200), Event{EventKind::PauseSession}});
        steps.push_back({milliseconds(100), Event{EventKind::ResumeSession}});
        ctx->curve.append_steps(std::move(steps), false, *ctx);

        for (int i = 0; i < 3; ++i) {
            ctx->curve.on_timer_fire(*ctx);
        }

        CHECK(kinds() ==
              std::vector<EventKind>{EventKind::SetChargingCurrent, EventKind::ResumeSession, EventKind::PauseSession});
        CHECK_FALSE(ctx->curve.active());
    }
}

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
    return static_cast<int>(std::count_if(timer.enqueued_events.begin(), timer.enqueued_events.end(),
                                          [kind](const Event& event) { return kind_of(event) == kind; }));
}
StateBase::ContainerType take(StateBase& from, StateBase::Result result) {
    REQUIRE(result.new_state);
    from.leave();
    result.new_state->enter();
    return std::move(result.new_state);
}
} // namespace

TEST_CASE("a session's charging curve ends with the session", "[evctl][session-end][curve][curve]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    const auto ack_topic = fx.topics.everest_to_extern("command_ack");
    const auto rejected_acks = [&] {
        return std::count_if(fx.sink.records.begin(), fx.sink.records.end(), [&](const auto& kv) {
            return kv.first == ack_topic && kv.second.find("Rejected") != std::string::npos;
        });
    };
    // Drives the curve timer the way the runtime does, feeding what it enqueues to `state`;
    // returns how many SetChargingCurrent steps it fed.
    const auto fire_into = [&](StateBase& state, int fires) {
        int set_current = 0;
        for (int i = 0; i < fires; ++i) {
            fx.timer.enqueued_events.clear();
            ctx->curve.on_timer_fire(*ctx);
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
        ctx->curve.on_timer_fire(*ctx);
        REQUIRE(count_kind(fx.timer, EventKind::SetChargingCurrent) >= 2);

        auto held = take(charging, charging.feed(Event{EventKind::StopSession}));
        REQUIRE(held->get_id() == api::FsmState::Plugged);
        fx.sink.records.clear();

        CHECK(fire_into(*held, 20) == 0);
        CHECK(rejected_acks() == 0);
        CHECK_FALSE(ctx->curve.active());
    }
}

TEST_CASE("CurvePlayer loops on its own clock and ends with stop or unplug", "[evctl][curve][controlled-clock]") {
    using std::chrono::milliseconds;
    ControlledClock clock;
    TestFixture fx;
    auto ctx = fx.make_ctx();
    api::AcIecSessionParams params{};
    params.curve = api::ChargingCurve{{{0, 10.0f, true, std::nullopt}, {100, 12.0f, true, std::nullopt}}, true};
    ctx->configured_session = api::SessionConfigParams{params};
    Plugged plugged{*ctx};
    REQUIRE(plugged.feed(Event{BeginSessionEvt{}}).new_state == nullptr);
    Charging charging{*ctx};
    charging.enter();
    REQUIRE(ctx->curve.active());
    for (int elapsed : {100, 200, 300, 400, 500}) {
        clock.set(milliseconds(elapsed));
        fx.timer.enqueued_events.clear();
        fx.timer.curve_timer_arms.clear();
        ctx->curve.on_timer_fire(*ctx);
        CHECK(count_kind(fx.timer, EventKind::SetChargingCurrent) == 2);
        CHECK(fx.timer.curve_timer_arms == std::vector<milliseconds>{milliseconds(100)});
    }

    SECTION("stop leaves the EV plugged and the curve ended") {
        auto held = take(charging, charging.feed(Event{EventKind::StopSession}));
        CHECK(held->get_id() == api::FsmState::Plugged);
    }
    SECTION("unplug ends the curve") {
        auto unplugged = take(charging, charging.feed(pilot_input(::types::board_support_common::Event::Disconnected)));
        CHECK(unplugged->get_id() == api::FsmState::Unplugged);
    }
    CHECK_FALSE(ctx->curve.active());
    fx.timer.enqueued_events.clear();
    clock.set(milliseconds(700));
    ctx->curve.on_timer_fire(*ctx);
    CHECK(fx.timer.enqueued_events.empty());
}
