// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Tests for RampInterpolator: linear current ramp executed on each tick when
// ctx.vars.active_ramp is set. The interpolator takes an injected `now` so
// tests can advance time deterministically without touching steady_clock.

#include "../main/RampInterpolator.hpp"
#include "../main/FsmContext.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace module;
using namespace module::test;

namespace {

// Collect every recorded set_ac_max_current call as a parsed float, in order.
std::vector<float> recorded_currents(const std::vector<std::string>& records) {
    const std::string needle = "set_ac_max_current(current=";
    std::vector<float> out;
    for (const auto& r : records) {
        auto pos = r.find(needle);
        if (pos == std::string::npos) {
            continue;
        }
        auto start = pos + needle.size();
        auto end = r.find(')', start);
        if (end == std::string::npos) {
            continue;
        }
        out.push_back(std::stof(r.substr(start, end - start)));
    }
    return out;
}

bool contains_three_phases(const std::vector<std::string>& records, bool value) {
    const std::string needle = std::string{"set_three_phases(three_phases="} + (value ? "true" : "false") + ")";
    return std::any_of(records.begin(), records.end(),
                       [&](const std::string& r) { return r.find(needle) != std::string::npos; });
}

} // namespace

TEST_CASE("RampInterpolator advances charging_current_a linearly", "[evsim][ramp]") {
    TestFixture fx;

    SECTION("step before start_at leaves vars unchanged and issues no BSP call") {
        auto ctx = fx.make_ctx();
        ctx->vars.charging_current_a = 6.0f;
        ctx->vars.three_phases = true;
        const auto t0 = std::chrono::steady_clock::time_point{std::chrono::milliseconds{10000}};
        ActiveRamp r;
        r.start_a = 6.0f;
        r.target_a = 16.0f;
        r.three_phases = true;
        r.start_at = t0;
        r.end_at = t0 + std::chrono::milliseconds{1000};
        ctx->vars.active_ramp = r;
        // No-op: at start_at the interpolated value still equals start_a, but
        // the ramp must remain active for subsequent ticks.
        ramp_step(*ctx, t0);
        REQUIRE(ctx->vars.active_ramp.has_value());
        auto currents = recorded_currents(fx.mocks.bsp.records);
        REQUIRE(currents.size() == 1);
        CHECK(currents.front() == 6.0f);
    }

    SECTION("step at midpoint commands an intermediate current") {
        auto ctx = fx.make_ctx();
        ctx->vars.charging_current_a = 6.0f;
        const auto t0 = std::chrono::steady_clock::time_point{std::chrono::milliseconds{10000}};
        ActiveRamp r;
        r.start_a = 6.0f;
        r.target_a = 16.0f;
        r.three_phases = true;
        r.start_at = t0;
        r.end_at = t0 + std::chrono::milliseconds{1000};
        ctx->vars.active_ramp = r;
        ramp_step(*ctx, t0 + std::chrono::milliseconds{500});
        auto currents = recorded_currents(fx.mocks.bsp.records);
        REQUIRE_FALSE(currents.empty());
        CHECK(std::abs(currents.back() - 11.0f) < 0.01f);
        CHECK(ctx->vars.active_ramp.has_value());
    }

    SECTION("step with now strictly before start_at clamps to start_a (no reverse overshoot)") {
        // Future-armed ramp / clock skew: now < start_at makes elapsed_ms
        // negative. Pre-fix the unclamped t drove `current` below start_a in
        // the wrong direction; post-fix t is clamped to 0 so the BSP sees
        // exactly start_a.
        auto ctx = fx.make_ctx();
        ctx->vars.charging_current_a = 6.0f;
        const auto t0 = std::chrono::steady_clock::time_point{std::chrono::milliseconds{10000}};
        ActiveRamp r;
        r.start_a = 6.0f;
        r.target_a = 16.0f;
        r.three_phases = true;
        r.start_at = t0;
        r.end_at = t0 + std::chrono::milliseconds{1000};
        ctx->vars.active_ramp = r;

        ramp_step(*ctx, t0 - std::chrono::milliseconds{500});

        auto currents = recorded_currents(fx.mocks.bsp.records);
        REQUIRE(currents.size() == 1);
        CHECK(std::abs(currents.front() - 6.0f) < 0.01f);
        CHECK(currents.front() >= 6.0f); // never below start_a
        CHECK(ctx->vars.active_ramp.has_value());
    }

    SECTION("SetChargingCurrent with ramp_ms=2000 issues intermediate bsp_set_ac_max_current at tick rate") {
        auto ctx = fx.make_ctx();
        ctx->vars.charging_current_a = 6.0f;
        const auto t0 = std::chrono::steady_clock::time_point{std::chrono::milliseconds{20000}};
        ActiveRamp r;
        r.start_a = 6.0f;
        r.target_a = 16.0f;
        r.three_phases = false;
        r.start_at = t0;
        r.end_at = t0 + std::chrono::milliseconds{2000};
        ctx->vars.active_ramp = r;
        // Simulate four 500 ms ticks: 0.25 / 0.5 / 0.75 / 1.0 of the ramp.
        ramp_step(*ctx, t0 + std::chrono::milliseconds{500});
        ramp_step(*ctx, t0 + std::chrono::milliseconds{1000});
        ramp_step(*ctx, t0 + std::chrono::milliseconds{1500});
        ramp_step(*ctx, t0 + std::chrono::milliseconds{2000});
        auto currents = recorded_currents(fx.mocks.bsp.records);
        REQUIRE(currents.size() == 4);
        CHECK(std::abs(currents[0] - 8.5f) < 0.01f);
        CHECK(std::abs(currents[1] - 11.0f) < 0.01f);
        CHECK(std::abs(currents[2] - 13.5f) < 0.01f);
        CHECK(std::abs(currents[3] - 16.0f) < 0.01f);
        // The final tick lands on/after end_at and must clear the ramp.
        CHECK_FALSE(ctx->vars.active_ramp.has_value());
        CHECK(contains_three_phases(fx.mocks.bsp.records, false));
    }

    SECTION("Ramp under an EVSE ceiling clamps the BSP current but tracks desired") {
        // The ramp drives the EV desired (charging_current_a) up to the raw
        // target while the BSP only ever sees min(desired, ceiling). A
        // regression pushing the raw interpolated value straight to the BSP
        // would be invisible to the other ramp tests (none set a ceiling).
        auto ctx = fx.make_ctx();
        ctx->vars.charging_current_a = 6.0f;
        ctx->vars.evse_ac_max_current_a = 10.0f; // EVSE ceiling below the target
        const auto t0 = std::chrono::steady_clock::time_point{std::chrono::milliseconds{40000}};
        ActiveRamp r;
        r.start_a = 6.0f;
        r.target_a = 16.0f;
        r.three_phases = false;
        r.start_at = t0;
        r.end_at = t0 + std::chrono::milliseconds{2000};
        ctx->vars.active_ramp = r;
        // Four 500 ms ticks: interpolated 8.5 / 11 / 13.5 / 16, clamped at 10.
        ramp_step(*ctx, t0 + std::chrono::milliseconds{500});
        ramp_step(*ctx, t0 + std::chrono::milliseconds{1000});
        ramp_step(*ctx, t0 + std::chrono::milliseconds{1500});
        ramp_step(*ctx, t0 + std::chrono::milliseconds{2000});
        auto currents = recorded_currents(fx.mocks.bsp.records);
        REQUIRE(currents.size() == 4);
        CHECK(std::abs(currents[0] - 8.5f) < 0.01f);  // below ceiling -> verbatim
        CHECK(std::abs(currents[1] - 10.0f) < 0.01f); // clamped
        CHECK(std::abs(currents[2] - 10.0f) < 0.01f); // clamped
        CHECK(std::abs(currents[3] - 10.0f) < 0.01f); // clamped
        // Desired tracked the raw ramp target, not the clamped value.
        CHECK(std::abs(ctx->vars.charging_current_a - 16.0f) < 0.01f);
    }

    SECTION("Ramp end value matches target_a within float tolerance") {
        auto ctx = fx.make_ctx();
        ctx->vars.charging_current_a = 0.0f;
        const auto t0 = std::chrono::steady_clock::time_point{std::chrono::milliseconds{30000}};
        ActiveRamp r;
        r.start_a = 0.0f;
        r.target_a = 13.7f;
        r.three_phases = true;
        r.start_at = t0;
        r.end_at = t0 + std::chrono::milliseconds{2000};
        ctx->vars.active_ramp = r;
        // Advance well past end_at — interpolator must snap to target_a.
        ramp_step(*ctx, t0 + std::chrono::milliseconds{5000});
        auto currents = recorded_currents(fx.mocks.bsp.records);
        REQUIRE_FALSE(currents.empty());
        CHECK(std::abs(currents.back() - 13.7f) < 0.01f);
        CHECK_FALSE(ctx->vars.active_ramp.has_value());
        CHECK(std::abs(ctx->vars.charging_current_a - 13.7f) < 0.01f);
    }

    SECTION("degenerate ramp (end_at before start_at) snaps to target and clears") {
        // A non-positive total_ms can only be reached when `now < end_at`
        // (otherwise the now>=end_at branch handles it first) yet
        // `end_at <= start_at`. Place start_at in the future relative to now,
        // with end_at strictly before start_at: now(0) < end_at(50) < start_at(100).
        auto ctx = fx.make_ctx();
        ctx->vars.charging_current_a = 6.0f;
        const auto now = std::chrono::steady_clock::time_point{std::chrono::milliseconds{0}};
        ActiveRamp r;
        r.start_a = 6.0f;
        r.target_a = 16.0f;
        r.three_phases = true;
        r.start_at = now + std::chrono::milliseconds{100};
        r.end_at = now + std::chrono::milliseconds{50}; // end before start -> total_ms < 0
        ctx->vars.active_ramp = r;

        ramp_step(*ctx, now);

        // Snapped straight to target; ramp cleared so no further ticks process it.
        auto currents = recorded_currents(fx.mocks.bsp.records);
        REQUIRE(currents.size() == 1);
        CHECK(currents.front() == 16.0f);
        CHECK_FALSE(ctx->vars.active_ramp.has_value());
        CHECK(std::abs(ctx->vars.charging_current_a - 16.0f) < 0.01f);
    }

    SECTION("zero-length ramp (end_at == start_at, both ahead of now) snaps to target") {
        auto ctx = fx.make_ctx();
        ctx->vars.charging_current_a = 6.0f;
        const auto now = std::chrono::steady_clock::time_point{std::chrono::milliseconds{0}};
        ActiveRamp r;
        r.start_a = 4.0f;
        r.target_a = 12.0f;
        r.three_phases = false;
        r.start_at = now + std::chrono::milliseconds{100};
        r.end_at = now + std::chrono::milliseconds{100}; // total_ms == 0
        ctx->vars.active_ramp = r;

        ramp_step(*ctx, now);

        auto currents = recorded_currents(fx.mocks.bsp.records);
        REQUIRE(currents.size() == 1);
        CHECK(currents.front() == 12.0f);
        CHECK_FALSE(ctx->vars.active_ramp.has_value());
    }

    SECTION("step without active_ramp is a no-op") {
        auto ctx = fx.make_ctx();
        ctx->vars.charging_current_a = 16.0f;
        const auto t0 = std::chrono::steady_clock::time_point{std::chrono::milliseconds{40000}};
        ramp_step(*ctx, t0);
        CHECK(fx.mocks.bsp.records.empty());
        CHECK(ctx->vars.charging_current_a == 16.0f);
    }
}
