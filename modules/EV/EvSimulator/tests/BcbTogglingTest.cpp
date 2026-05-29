// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/states/BcbToggling.hpp"
#include "../main/FsmContext.hpp"
#include "../main/states/Paused.hpp"
#include "../main/states/V2GNegotiating.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <everest_api_types/ev_simulator/codec.hpp>

#include <algorithm>
#include <chrono>
#include <string>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

namespace {

bool contains_substr(const std::vector<std::string>& records, const std::string& needle) {
    return std::any_of(records.begin(), records.end(),
                       [&](const std::string& r) { return r.find(needle) != std::string::npos; });
}

int count_substr(const std::vector<std::string>& records, const std::string& needle) {
    return static_cast<int>(std::count_if(records.begin(), records.end(),
                                          [&](const std::string& r) { return r.find(needle) != std::string::npos; }));
}

} // namespace

TEST_CASE("EvSimulator BcbToggling cycle B<->C * 3 rounds -> V2GNegotiating", "[evsim][group2]") {
    TestFixture fx;

    SECTION("enter sets CP=B, arms 250ms, publishes BcbToggling") {
        auto ctx = fx.make_ctx();
        ctx->vars.bcb_remaining = 6;
        BcbToggling s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));
        REQUIRE_FALSE(fx.timer.state_timer_arms.empty());
        CHECK(fx.timer.state_timer_arms.back() == std::chrono::milliseconds(250));
    }

    SECTION("enter defaults bcb_remaining to 6 when unset") {
        auto ctx = fx.make_ctx();
        ctx->vars.bcb_remaining = 0;
        BcbToggling s{*ctx};
        s.enter();
        CHECK(ctx->vars.bcb_remaining == 6);
    }

    SECTION("enter preserves pre-seeded bcb_remaining value") {
        auto ctx = fx.make_ctx();
        ctx->vars.bcb_remaining = 6;
        BcbToggling s{*ctx};
        s.enter();
        CHECK(ctx->vars.bcb_remaining == 6);
    }

    SECTION("6 StateDeadlines cycle B<->C and final one transitions to V2GNegotiating") {
        auto ctx = fx.make_ctx();
        ctx->vars.bcb_remaining = 6;
        BcbToggling s{*ctx};
        s.enter();
        fx.mocks.bsp.clear(); // clear the set_cp(B) from enter()

        // Cycle: 6 ticks. remaining starts at 6 → even → C; then 5 → odd → B; ...
        // Expected sequence in CP per tick: C, B, C, B, C, B (3 round-trips); last tick transitions.
        for (int i = 0; i < 5; ++i) {
            auto result = s.feed(Event{EventKind::StateDeadline});
            CHECK_FALSE(result.new_state);
        }
        // The 6th deadline performs the final edge and transitions.
        auto result = s.feed(Event{EventKind::StateDeadline});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::V2GNegotiating);

        // Verify cycle: 3 set_cp(C) and 3 set_cp(B) edges.
        CHECK(count_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=C)") == 3);
        CHECK(count_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)") == 3);
    }

    SECTION("After 5 StateDeadlines, bcb_remaining=1 and timer re-armed at 250ms") {
        auto ctx = fx.make_ctx();
        ctx->vars.bcb_remaining = 6;
        BcbToggling s{*ctx};
        for (int i = 0; i < 5; ++i) {
            s.feed(Event{EventKind::StateDeadline});
        }
        CHECK(ctx->vars.bcb_remaining == 1);
        // Each non-final tick re-arms the timer.
        CHECK(std::count(fx.timer.state_timer_arms.begin(), fx.timer.state_timer_arms.end(),
                         std::chrono::milliseconds(250)) >= 5);
    }

    SECTION("Paused.feed(ResumeSession) seeds bcb_remaining=6 and transitions to BcbToggling") {
        auto ctx = fx.make_ctx();
        Paused p{*ctx};
        auto result = p.feed(Event{EventKind::ResumeSession});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::BcbToggling);
        CHECK(ctx->vars.bcb_remaining == 6);
    }
}
