// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <utils/date.hpp>

#include "Broker.hpp"
#include "EnergyManagerTestHelpers.hpp"
#include "MeasurementTrackingState.hpp"

namespace module {

// ---------------------------------------------------------------- grid limit helper

TEST(PowerBoostGridLimit, DerivesLimitFromCurrentAndPhases) {
    const auto root = test::make_root_node("grid", 63.0f, std::nullopt, {});

    const auto limit = get_grid_limit_W(root, 230.0f);

    ASSERT_TRUE(limit.has_value());
    // 63A x 3 phases x 230V
    EXPECT_FLOAT_EQ(limit.value(), 43470.0f);
}

TEST(PowerBoostGridLimit, PrefersExplicitTotalPowerLimit) {
    const auto root = test::make_root_node("grid", 63.0f, 20000.0f, {});

    const auto limit = get_grid_limit_W(root, 230.0f);

    ASSERT_TRUE(limit.has_value());
    EXPECT_FLOAT_EQ(limit.value(), 20000.0f);
}

TEST(PowerBoostGridLimit, NulloptWhenRootDeclaresNoLimit) {
    types::energy::EnergyFlowRequest root;
    root.uuid = "grid";
    root.node_type = types::energy::NodeType::Generic;

    EXPECT_FALSE(get_grid_limit_W(root, 230.0f).has_value());
}

// ---------------------------------------------------------------- allocation sum

TEST(PowerBoostAllocation, SumsAmpereBasedAllocations) {
    std::vector<types::energy::EnforcedLimits> limits;

    types::energy::EnforcedLimits a;
    a.uuid = "cp01";
    a.limits_root_side.ac_max_current_A = {16.0f, "TEST"};
    a.limits_root_side.ac_max_phase_count = {3, "TEST"};
    limits.push_back(a);

    types::energy::EnforcedLimits b;
    b.uuid = "cp02";
    b.limits_root_side.ac_max_current_A = {10.0f, "TEST"};
    b.limits_root_side.ac_max_phase_count = {1, "TEST"};
    limits.push_back(b);

    // 16 x 3 x 230 = 11040, 10 x 1 x 230 = 2300
    EXPECT_FLOAT_EQ(sum_allocated_W(limits, 230.0f), 13340.0f);
}

TEST(PowerBoostAllocation, PrefersTotalPowerWhenPresent) {
    std::vector<types::energy::EnforcedLimits> limits;

    types::energy::EnforcedLimits a;
    a.uuid = "cp01";
    a.limits_root_side.ac_max_current_A = {16.0f, "TEST"};
    a.limits_root_side.ac_max_phase_count = {3, "TEST"};
    a.limits_root_side.total_power_W = {5000.0f, "TEST"};
    limits.push_back(a);

    EXPECT_FLOAT_EQ(sum_allocated_W(limits, 230.0f), 5000.0f);
}

TEST(PowerBoostAllocation, EmptyAllocationIsZero) {
    EXPECT_FLOAT_EQ(sum_allocated_W({}, 230.0f), 0.0f);
}

// ---------------------------------------------------------------- state machine

namespace {

MeasurementTrackingInput make_input(float measured_W, int fresh_meters, std::optional<float> grid_limit_W,
                                    float last_allocated_W) {
    MeasurementTrackingInput in;
    in.aggregate.power_W = measured_W;
    in.aggregate.fresh_meters = fresh_meters;
    in.grid_limit_W = grid_limit_W;
    in.last_allocated_W = last_allocated_W;
    in.boost_threshold_W = 500.0f;
    in.boost_step_A = 2.0f;
    in.boost_hysteresis_cycles = 3;
    in.max_boost_offset_A = 63.0f;
    return in;
}

} // namespace

TEST(PowerBoostStateMachine, BoostsOnlyAfterHysteresisCycles) {
    MeasurementTrackingState state;
    // Grid allows 43470W, only 3000W is drawn: ample headroom.
    const auto input = make_input(3000.0f, 1, 43470.0f, 11040.0f);

    state = advance_tracking_state(state, input);
    EXPECT_FLOAT_EQ(state.boost_offset_A, 0.0f);
    EXPECT_EQ(state.underutilized_cycles, 1);

    state = advance_tracking_state(state, input);
    EXPECT_FLOAT_EQ(state.boost_offset_A, 0.0f);
    EXPECT_EQ(state.underutilized_cycles, 2);

    // Third consecutive cycle reaches the hysteresis count and widens the limit.
    state = advance_tracking_state(state, input);
    EXPECT_FLOAT_EQ(state.boost_offset_A, 2.0f);
    EXPECT_EQ(state.underutilized_cycles, 0);
}

TEST(PowerBoostStateMachine, BoostAccumulatesOverRepeatedHysteresisWindows) {
    MeasurementTrackingState state;
    const auto input = make_input(3000.0f, 1, 43470.0f, 11040.0f);

    for (int i = 0; i < 9; i++) {
        state = advance_tracking_state(state, input);
    }

    // Nine cycles with a hysteresis of three yields three boost steps.
    EXPECT_FLOAT_EQ(state.boost_offset_A, 6.0f);
}

TEST(PowerBoostStateMachine, ClampsBoostOffsetToMaximum) {
    MeasurementTrackingState state;
    auto input = make_input(3000.0f, 1, 43470.0f, 11040.0f);
    input.max_boost_offset_A = 5.0f;

    for (int i = 0; i < 30; i++) {
        state = advance_tracking_state(state, input);
    }

    EXPECT_FLOAT_EQ(state.boost_offset_A, 5.0f);
}

TEST(PowerBoostStateMachine, ReleasesBoostWhenHeadroomDisappears) {
    MeasurementTrackingState state;
    state.boost_offset_A = 6.0f;

    // Consumption has risen to just below the grid limit: no headroom left.
    const auto input = make_input(43200.0f, 1, 43470.0f, 43300.0f);

    state = advance_tracking_state(state, input);
    EXPECT_FLOAT_EQ(state.boost_offset_A, 4.0f);

    state = advance_tracking_state(state, input);
    EXPECT_FLOAT_EQ(state.boost_offset_A, 2.0f);

    state = advance_tracking_state(state, input);
    EXPECT_FLOAT_EQ(state.boost_offset_A, 0.0f);

    // Never goes negative.
    state = advance_tracking_state(state, input);
    EXPECT_FLOAT_EQ(state.boost_offset_A, 0.0f);
}

TEST(PowerBoostStateMachine, TransientLossOfHeadroomResetsTheCounter) {
    MeasurementTrackingState state;
    const auto headroom = make_input(3000.0f, 1, 43470.0f, 11040.0f);
    const auto no_headroom = make_input(43200.0f, 1, 43470.0f, 43300.0f);

    state = advance_tracking_state(state, headroom);
    state = advance_tracking_state(state, headroom);
    EXPECT_EQ(state.underutilized_cycles, 2);

    state = advance_tracking_state(state, no_headroom);
    EXPECT_EQ(state.underutilized_cycles, 0);

    // The two earlier cycles must not count towards the next boost.
    state = advance_tracking_state(state, headroom);
    state = advance_tracking_state(state, headroom);
    EXPECT_FLOAT_EQ(state.boost_offset_A, 0.0f);
}

TEST(PowerBoostStateMachine, DoesNotActWithoutAFreshMeasurement) {
    MeasurementTrackingState state;
    state.boost_offset_A = 4.0f;
    state.underutilized_cycles = 2;

    // Every meter is stale: the aggregate says nothing about reality.
    const auto input = make_input(0.0f, 0, 43470.0f, 11040.0f);

    state = advance_tracking_state(state, input);

    // The offset is held rather than boosted or released, and the flag is cleared because we
    // cannot claim anything about reducibility.
    EXPECT_FLOAT_EQ(state.boost_offset_A, 4.0f);
    EXPECT_EQ(state.underutilized_cycles, 0);
    EXPECT_FALSE(state.power_can_be_reduced);
}

TEST(PowerBoostStateMachine, DoesNotBoostWithoutAKnownGridLimit) {
    MeasurementTrackingState state;
    const auto input = make_input(3000.0f, 1, std::nullopt, 11040.0f);

    for (int i = 0; i < 9; i++) {
        state = advance_tracking_state(state, input);
    }

    EXPECT_FLOAT_EQ(state.boost_offset_A, 0.0f);
}

TEST(PowerBoostStateMachine, PowerCanBeReducedWhenAllocationExceedsConsumption) {
    MeasurementTrackingState state;
    // 11040W handed out, 3000W drawn: 8040W could be released.
    const auto input = make_input(3000.0f, 1, 43470.0f, 11040.0f);

    state = advance_tracking_state(state, input);

    EXPECT_TRUE(state.power_can_be_reduced);
}

TEST(PowerBoostStateMachine, PowerCannotBeReducedWhenAllocationMatchesConsumption) {
    MeasurementTrackingState state;
    // 3105W handed out, 3000W drawn: only 105W spare, below the 500W threshold.
    const auto input = make_input(3000.0f, 1, 43470.0f, 3105.0f);

    state = advance_tracking_state(state, input);

    EXPECT_FALSE(state.power_can_be_reduced);
}

TEST(PowerBoostStateMachine, PowerCannotBeReducedBeforeAnyAllocation) {
    MeasurementTrackingState state;
    const auto input = make_input(3000.0f, 1, 43470.0f, 0.0f);

    state = advance_tracking_state(state, input);

    EXPECT_FALSE(state.power_can_be_reduced);
}

TEST(PowerBoostStateMachine, ZeroStepDisablesBoosting) {
    MeasurementTrackingState state;
    auto input = make_input(3000.0f, 1, 43470.0f, 11040.0f);
    input.boost_step_A = 0.0f;

    for (int i = 0; i < 9; i++) {
        state = advance_tracking_state(state, input);
    }

    EXPECT_FLOAT_EQ(state.boost_offset_A, 0.0f);
    // The reducibility flag is independent of boosting and still works.
    EXPECT_TRUE(state.power_can_be_reduced);
}

// ---------------------------------------------------------------- broker config

TEST(PowerBoostBrokerConfig, BoostOffsetDefaultsToZero) {
    Broker::EnergyManagerConfig config;
    EXPECT_FLOAT_EQ(config.boost_offset_A, 0.0f);
}

// ---------------------------------------------------------------- integration
//
// Two things here are easy to get wrong and both silently disable the feature:
//
//  * The measurement must be fresh relative to the optimizer time. set_measurement()
//    defaults the meter timestamp to 12:00:00, so the optimizer runs at 12:00:02 to stay
//    inside the 5s aggregation window. Running at 12:30 would make every reading stale.
//  * Cycle 1 has no measurement attached yet, so it does not count as a headroom cycle.
//    With boost_hysteresis_cycles = 3 the first boost lands on optimizer cycle 4.

namespace {

types::energy::EnergyFlowRequest make_boost_tree(float evse_max_current_A, float grid_max_current_A) {
    auto evse = test::make_evse_node("cp01", evse_max_current_A, 6.0f);
    return test::make_root_node("grid", grid_max_current_A, std::nullopt, {evse});
}

float enforced_current(const std::vector<types::energy::EnforcedLimits>& results, const std::string& uuid) {
    const auto limit = test::find_limit(results, uuid);
    if (not limit.has_value() or not limit.value().limits_root_side.ac_max_current_A.has_value()) {
        return -1.0f;
    }
    return limit.value().limits_root_side.ac_max_current_A.value().value;
}

// WP1.a's arithmetic: enforced_A = cap_W / (phases * voltage).
float expected_current_A(float cap_W, int phases = 3, float voltage = 230.0f) {
    return cap_W / (static_cast<float>(phases) * voltage);
}

EnergyManagerConfig make_boost_config() {
    auto config = test::make_default_config();
    config.use_power_meter_tracking = true;
    config.power_meter_tracking_initial_current_A = 16.0;
    config.power_meter_tracking_margin_W = 200.0;
    config.boost_threshold_W = 500.0;
    config.boost_step_A = 2.0;
    config.boost_hysteresis_cycles = 3;
    return config;
}

// 2s after the default measurement timestamp, so readings are inside the 5s window.
const auto AT = Everest::Date::from_rfc3339("2026-08-04T12:00:02.000Z");

} // namespace

TEST(PowerBoostIntegration, AllocationGrowsAfterHysteresisWhenGridHasHeadroom) {
    auto request = make_boost_tree(32.0f, 63.0f);
    EnergyManagerImpl impl(make_boost_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    // Cycle 1: no measurement yet, so the initial current applies (16A * 690 = 11040W).
    EXPECT_NEAR(enforced_current(impl.run_optimizer(request, AT), "cp01"), 16.0f, 0.01f);

    // The EV settles at 10000W and stays there. Grid headroom is 43470 - 10000 = 33470W
    // throughout, far above the 500W threshold.
    test::set_measurement(request.children[0], 10000.0f);

    // Cycle 2: first cycle with a fresh measurement. Pure tracking, no boost yet.
    EXPECT_NEAR(enforced_current(impl.run_optimizer(request, AT), "cp01"), expected_current_A(10200.0f), 0.01f);
    EXPECT_FLOAT_EQ(impl.get_boost_offset_A(), 0.0f);

    // Cycle 3: second headroom cycle. Still no boost.
    EXPECT_NEAR(enforced_current(impl.run_optimizer(request, AT), "cp01"), expected_current_A(10200.0f), 0.01f);
    EXPECT_FLOAT_EQ(impl.get_boost_offset_A(), 0.0f);

    // Cycle 4 completes the third consecutive headroom cycle, granting one boost step.
    const auto cycle4 = enforced_current(impl.run_optimizer(request, AT), "cp01");
    EXPECT_FLOAT_EQ(impl.get_boost_offset_A(), 2.0f);

    // cap = 10200 + 2A * 3ph * 230V = 11580W
    EXPECT_NEAR(cycle4, expected_current_A(11580.0f), 0.01f);
    EXPECT_GT(cycle4, expected_current_A(10200.0f));
}

TEST(PowerBoostIntegration, BoostingNeverExceedsTheStaticLimit) {
    // The connector's own static limit is 10A; the grid allows far more, so headroom
    // persists indefinitely and the offset keeps growing.
    auto request = make_boost_tree(10.0f, 63.0f);
    EnergyManagerImpl impl(make_boost_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    impl.run_optimizer(request, AT);
    test::set_measurement(request.children[0], 2000.0f);

    // Run long enough that the raw offset would blow far past 10A.
    for (int cycle = 0; cycle < 40; cycle++) {
        const auto current = enforced_current(impl.run_optimizer(request, AT), "cp01");
        EXPECT_LE(current, 10.0f + 0.01f) << "static limit exceeded on cycle " << cycle;
    }

    // It should have saturated at the static limit, not stalled below it.
    EXPECT_NEAR(enforced_current(impl.run_optimizer(request, AT), "cp01"), 10.0f, 0.01f);
    EXPECT_GT(impl.get_boost_offset_A(), 0.0f);
}

TEST(PowerBoostIntegration, ReducingBehaviourFromWp1aStillWorks) {
    auto request = make_boost_tree(32.0f, 63.0f);
    // Disable boosting so this is a pure WP1.a regression check.
    auto config = make_boost_config();
    config.boost_step_A = 0.0;

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(enforced_current(impl.run_optimizer(request, AT), "cp01"), 16.0f, 0.01f);

    test::set_measurement(request.children[0], 6000.0f);
    EXPECT_NEAR(enforced_current(impl.run_optimizer(request, AT), "cp01"), expected_current_A(6200.0f), 0.01f);

    // Consumption rises: the tracking limit must follow it up, still without boosting.
    test::set_measurement(request.children[0], 12000.0f);
    EXPECT_NEAR(enforced_current(impl.run_optimizer(request, AT), "cp01"), expected_current_A(12200.0f), 0.01f);
    EXPECT_FLOAT_EQ(impl.get_boost_offset_A(), 0.0f);
}

TEST(PowerBoostIntegration, PublishesPowerCanBeReducedOnChangeOnly) {
    auto request = make_boost_tree(32.0f, 63.0f);

    std::vector<bool> published;
    EnergyManagerImpl impl(
        make_boost_config(), [](const std::vector<types::energy::EnforcedLimits>&) {},
        [&published](bool value) { published.push_back(value); });

    // Cycle 1: no allocation yet to compare against, so false - but it must still be
    // published once, as EVerest expects every var to have an initial value.
    impl.run_optimizer(request, AT);
    ASSERT_EQ(published.size(), 1U);
    EXPECT_FALSE(published[0]);

    // Cycle 2 compares cycle 1's 11040W allocation against 10000W drawn: a 1040W gap
    // exceeds the 500W threshold, so the allocation is reducible.
    test::set_measurement(request.children[0], 10000.0f);
    impl.run_optimizer(request, AT);
    ASSERT_EQ(published.size(), 2U);
    EXPECT_TRUE(published[1]);
    EXPECT_TRUE(impl.get_power_can_be_reduced());

    // Cycle 3 compares cycle 2's 10200W allocation against 10000W: only the 200W margin is
    // spare, below the threshold, so no longer reducible.
    impl.run_optimizer(request, AT);
    ASSERT_EQ(published.size(), 3U);
    EXPECT_FALSE(published[2]);

    // Cycle 4 is unchanged (still 200W spare), so nothing further is published even though
    // the boost offset changes.
    impl.run_optimizer(request, AT);
    EXPECT_EQ(published.size(), 3U);
}

TEST(PowerBoostIntegration, NoBoostingWhenTrackingIsDisabled) {
    auto request = make_boost_tree(32.0f, 63.0f);
    auto config = make_boost_config();
    config.use_power_meter_tracking = false;

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});

    test::set_measurement(request.children[0], 10000.0f);

    for (int cycle = 0; cycle < 10; cycle++) {
        // The static limit is enforced on every cycle, untouched by measurements.
        EXPECT_NEAR(enforced_current(impl.run_optimizer(request, AT), "cp01"), 32.0f, 0.01f);
        EXPECT_FLOAT_EQ(impl.get_boost_offset_A(), 0.0f);
    }
}

} // namespace module
