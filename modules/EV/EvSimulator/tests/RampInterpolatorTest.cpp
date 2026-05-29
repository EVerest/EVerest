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
        RampInterpolator::step(*ctx, t0);
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
        RampInterpolator::step(*ctx, t0 + std::chrono::milliseconds{500});
        auto currents = recorded_currents(fx.mocks.bsp.records);
        REQUIRE_FALSE(currents.empty());
        CHECK(std::abs(currents.back() - 11.0f) < 0.01f);
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
        RampInterpolator::step(*ctx, t0 + std::chrono::milliseconds{500});
        RampInterpolator::step(*ctx, t0 + std::chrono::milliseconds{1000});
        RampInterpolator::step(*ctx, t0 + std::chrono::milliseconds{1500});
        RampInterpolator::step(*ctx, t0 + std::chrono::milliseconds{2000});
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
        RampInterpolator::step(*ctx, t0 + std::chrono::milliseconds{5000});
        auto currents = recorded_currents(fx.mocks.bsp.records);
        REQUIRE_FALSE(currents.empty());
        CHECK(std::abs(currents.back() - 13.7f) < 0.01f);
        CHECK_FALSE(ctx->vars.active_ramp.has_value());
        CHECK(std::abs(ctx->vars.charging_current_a - 13.7f) < 0.01f);
    }

    SECTION("step without active_ramp is a no-op") {
        auto ctx = fx.make_ctx();
        ctx->vars.charging_current_a = 16.0f;
        const auto t0 = std::chrono::steady_clock::time_point{std::chrono::milliseconds{40000}};
        RampInterpolator::step(*ctx, t0);
        CHECK(fx.mocks.bsp.records.empty());
        CHECK(ctx->vars.charging_current_a == 16.0f);
    }
}
