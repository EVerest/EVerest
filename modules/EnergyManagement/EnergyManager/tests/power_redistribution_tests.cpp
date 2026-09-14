// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <fmt/core.h>
#include <utils/date.hpp>

#include "BrokerPowerRedistribution.hpp"
#include "EnergyManagerTestHelpers.hpp"

namespace module {

namespace {

constexpr float U = 230.0f;
constexpr float MARGIN = 0.1f;
constexpr float GAIN = 0.5f;

const auto T0 = Everest::Date::from_rfc3339("2026-08-04T12:30:00.000Z");

types::energy::EnforcedLimits make_enforced_limit(const std::string& uuid, std::optional<float> current_A,
                                                  std::optional<int> phases, std::optional<float> total_power_W) {
    types::energy::EnforcedLimits l;
    l.uuid = uuid;
    if (current_A.has_value()) {
        l.limits_root_side.ac_max_current_A = {current_A.value(), "TEST"};
    }
    if (phases.has_value()) {
        l.limits_root_side.ac_max_phase_count = {phases.value(), "TEST"};
    }
    if (total_power_W.has_value()) {
        l.limits_root_side.total_power_W = {total_power_W.value(), "TEST"};
    }
    return l;
}

PowerMeterAggregator::AggregateResult make_aggregate(std::optional<float> total_W, int fresh, int stale) {
    PowerMeterAggregator::AggregateResult a;
    if (total_W.has_value()) {
        types::units::Power p;
        p.total = total_W.value();
        a.power_W = p;
    }
    a.fresh_meters = fresh;
    a.stale_meters = stale;
    return a;
}

StaticBoundsW bounds(std::optional<float> min_W, std::optional<float> max_W) {
    return StaticBoundsW{min_W, max_W};
}

EnergyManagerConfig make_redistribution_config(int hold_time_s = 10) {
    auto config = test::make_default_config();
    config.broker_strategy = "PowerRedistribution";
    config.power_redistribution_hold_time_s = hold_time_s;
    return config;
}

// A meter timestamp equal to the run time keeps every reading fresh for the aggregator.
const std::string FRESH = "2026-08-04T12:30:00.000Z";

// Meter timestamp for a run \p seconds after T0. The site inference goes through the
// aggregator, whose 5 s window would otherwise declare the readings stale.
std::string fresh_at(int seconds) {
    return fmt::format("2026-08-04T12:30:{:02d}.000Z", seconds);
}

} // namespace

// ---------------------------------------------------------------- limit helpers

TEST(RedistributionHelpers, GridLimitFromCurrentAndPhases) {
    const auto root = test::make_root_node("grid", 32.0f, std::nullopt, {});
    const auto limit = get_grid_limit_W(root, U);
    ASSERT_TRUE(limit.has_value());
    EXPECT_FLOAT_EQ(limit.value(), 32.0f * 3 * U);
}

TEST(RedistributionHelpers, GridLimitPrefersTotalPower) {
    const auto root = test::make_root_node("grid", 32.0f, 11000.0f, {});
    const auto limit = get_grid_limit_W(root, U);
    ASSERT_TRUE(limit.has_value());
    EXPECT_FLOAT_EQ(limit.value(), 11000.0f);
}

TEST(RedistributionHelpers, GridLimitNulloptWithoutSchedule) {
    types::energy::EnergyFlowRequest root;
    root.node_type = types::energy::NodeType::Generic;
    EXPECT_FALSE(get_grid_limit_W(root, U).has_value());
}

TEST(RedistributionHelpers, AllocatedPowerFromAmpere) {
    const auto limit = make_enforced_limit("evse1", 16.0f, 3, std::nullopt);
    const auto allocated = get_allocated_power_W(limit, U);
    ASSERT_TRUE(allocated.has_value());
    EXPECT_FLOAT_EQ(allocated.value(), 16.0f * 3 * U);
}

TEST(RedistributionHelpers, AllocatedPowerAssumesThreePhasesWhenUndeclared) {
    const auto limit = make_enforced_limit("evse1", 16.0f, std::nullopt, std::nullopt);
    const auto allocated = get_allocated_power_W(limit, U);
    ASSERT_TRUE(allocated.has_value());
    EXPECT_FLOAT_EQ(allocated.value(), 16.0f * 3 * U);
}

TEST(RedistributionHelpers, AllocatedPowerPrefersTotalPower) {
    const auto limit = make_enforced_limit("evse1", 16.0f, 3, 7000.0f);
    const auto allocated = get_allocated_power_W(limit, U);
    ASSERT_TRUE(allocated.has_value());
    EXPECT_FLOAT_EQ(allocated.value(), 7000.0f);
}

TEST(RedistributionHelpers, AllocatedPowerNulloptWithoutAnyLimit) {
    const auto limit = make_enforced_limit("evse1", std::nullopt, std::nullopt, std::nullopt);
    EXPECT_FALSE(get_allocated_power_W(limit, U).has_value());
}

TEST(RedistributionHelpers, StaticBoundsUseMinPhasesForMinimum) {
    // make_evse_node declares max 3 phases, min 1 phase.
    const auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    const auto b = get_static_bounds_W(evse, U);
    ASSERT_TRUE(b.min_W.has_value());
    ASSERT_TRUE(b.max_W.has_value());
    EXPECT_FLOAT_EQ(b.min_W.value(), 6.0f * 1 * U);
    EXPECT_FLOAT_EQ(b.max_W.value(), 32.0f * 3 * U);
}

TEST(RedistributionHelpers, StaticBoundsPreferTotalPowerForMaximum) {
    const auto evse = test::make_evse_node("evse1", 32.0f, 6.0f, 11000.0f);
    const auto b = get_static_bounds_W(evse, U);
    ASSERT_TRUE(b.max_W.has_value());
    EXPECT_FLOAT_EQ(b.max_W.value(), 11000.0f);
}

// ---------------------------------------------------------------- connector classification

TEST(RedistributionClassify, UnknownWithoutAllocation) {
    const auto c = classify_connector(std::nullopt, 4000.0f, bounds(1380.0f, 22080.0f), MARGIN);
    EXPECT_EQ(c.connector_class, ConnectorClass::Unknown);
    EXPECT_FLOAT_EQ(c.reducible_W, 0.0f);
}

TEST(RedistributionClassify, UnknownWithoutMeasurement) {
    const auto c = classify_connector(11040.0f, std::nullopt, bounds(1380.0f, 22080.0f), MARGIN);
    EXPECT_EQ(c.connector_class, ConnectorClass::Unknown);
}

TEST(RedistributionClassify, UnderConsumingBeyondMargin) {
    // Allotted 11040 W, drawing 4000 W: gap 7040 W >> 10 % of 11040.
    const auto c = classify_connector(11040.0f, 4000.0f, bounds(1380.0f, 22080.0f), MARGIN);
    EXPECT_EQ(c.connector_class, ConnectorClass::UnderConsuming);
    // Target is measured x 1.1 = 4400 W, so 6640 W could be released.
    EXPECT_NEAR(c.reducible_W, 11040.0f - 4400.0f, 0.5f);
    EXPECT_FALSE(c.held);
}

TEST(RedistributionClassify, ReducibleIsFlooredAtMinimumPurchase) {
    // Drawing almost nothing: target 110 W would starve the session; floor at min 1380 W.
    const auto c = classify_connector(11040.0f, 100.0f, bounds(1380.0f, 22080.0f), MARGIN);
    EXPECT_EQ(c.connector_class, ConnectorClass::UnderConsuming);
    EXPECT_NEAR(c.reducible_W, 11040.0f - 1380.0f, 0.5f);
}

TEST(RedistributionClassify, GapWithinMarginIsSaturated) {
    // 5 % below the allocation is inside the 10 % deadband; room up to the maximum remains.
    const auto c = classify_connector(11040.0f, 10488.0f, bounds(1380.0f, 22080.0f), MARGIN);
    EXPECT_EQ(c.connector_class, ConnectorClass::Saturated);
    EXPECT_FLOAT_EQ(c.reducible_W, 0.0f);
}

TEST(RedistributionClassify, ExactlyAtMarginIsNotUnderConsuming) {
    const auto c = classify_connector(10000.0f, 9000.0f, bounds(1380.0f, 22080.0f), MARGIN);
    EXPECT_EQ(c.connector_class, ConnectorClass::Saturated);
}

TEST(RedistributionClassify, ConsumingAtStaticMaximumIsAtMaximum) {
    const auto c = classify_connector(22080.0f, 22000.0f, bounds(1380.0f, 22080.0f), MARGIN);
    EXPECT_EQ(c.connector_class, ConnectorClass::AtMaximum);
}

TEST(RedistributionClassify, ConsumingWithoutKnownMaximumIsSaturated) {
    const auto c = classify_connector(22080.0f, 22000.0f, bounds(std::nullopt, std::nullopt), MARGIN);
    EXPECT_EQ(c.connector_class, ConnectorClass::Saturated);
}

TEST(RedistributionClassify, NegativeMeasurementCountsAsZero) {
    // An exporting meter reads negative; the released amount is bounded by the minimum.
    const auto c = classify_connector(11040.0f, -500.0f, bounds(1380.0f, 22080.0f), MARGIN);
    EXPECT_EQ(c.connector_class, ConnectorClass::UnderConsuming);
    EXPECT_NEAR(c.reducible_W, 11040.0f - 1380.0f, 0.5f);
}

// ---------------------------------------------------------------- site inference

TEST(RedistributionSite, NoClaimWithoutGridLimit) {
    const auto s = infer_site(std::nullopt, make_aggregate(5000.0f, 1, 0), {{11040.0f, 22080.0f}}, MARGIN, GAIN);
    EXPECT_FALSE(s.headroom_W.has_value());
    EXPECT_FLOAT_EQ(s.increase_W, 0.0f);
}

TEST(RedistributionSite, NoClaimWithoutFreshAggregate) {
    const auto s = infer_site(22080.0f, make_aggregate(std::nullopt, 0, 0), {{11040.0f, 22080.0f}}, MARGIN, GAIN);
    EXPECT_FALSE(s.measured_W.has_value());
    EXPECT_FALSE(s.headroom_W.has_value());
    EXPECT_FLOAT_EQ(s.increase_W, 0.0f);
}

TEST(RedistributionSite, PartiallyStaleAggregateMakesNoClaim) {
    const auto s = infer_site(22080.0f, make_aggregate(5000.0f, 1, 1), {{11040.0f, 22080.0f}}, MARGIN, GAIN);
    EXPECT_FALSE(s.measured_W.has_value());
    EXPECT_FLOAT_EQ(s.increase_W, 0.0f);
}

TEST(RedistributionSite, HeadroomWithinDeadbandGivesNoIncrease) {
    // 2000 W headroom on a 22080 W grid is below the 2208 W deadband.
    const auto s = infer_site(22080.0f, make_aggregate(20080.0f, 1, 0), {{11040.0f, 22080.0f}}, MARGIN, GAIN);
    ASSERT_TRUE(s.headroom_W.has_value());
    EXPECT_FLOAT_EQ(s.headroom_W.value(), 2000.0f);
    EXPECT_FLOAT_EQ(s.increase_W, 0.0f);
}

TEST(RedistributionSite, IncreaseIsProportionalToHeadroomBeyondDeadband) {
    // Headroom 12080 W, deadband 2208 W: gain 0.5 x 9872 W = 4936 W for the one connector.
    const auto s = infer_site(22080.0f, make_aggregate(10000.0f, 1, 0), {{11040.0f, 22080.0f}}, MARGIN, GAIN);
    EXPECT_EQ(s.saturated_connectors, 1);
    EXPECT_NEAR(s.increase_W, 0.5f * (12080.0f - 2208.0f), 0.5f);
}

TEST(RedistributionSite, IncreaseShrinksAsTheLimitIsApproached) {
    const auto far = infer_site(22080.0f, make_aggregate(10000.0f, 1, 0), {{11040.0f, 22080.0f}}, MARGIN, GAIN);
    const auto near = infer_site(22080.0f, make_aggregate(18000.0f, 1, 0), {{11040.0f, 22080.0f}}, MARGIN, GAIN);
    EXPECT_GT(far.increase_W, near.increase_W);
    EXPECT_GT(near.increase_W, 0.0f);
}

TEST(RedistributionSite, IncreaseIsSplitEquallyAndClampedToStaticMaximum) {
    // Two saturated connectors, one with only 500 W of room left.
    const std::vector<SaturatedConnector> saturated = {{11040.0f, 22080.0f}, {11040.0f, 11540.0f}};
    const auto s = infer_site(44160.0f, make_aggregate(20000.0f, 2, 0), saturated, MARGIN, GAIN);
    // headroom 24160, deadband 4416, gain 0.5 -> 9872 total, 4936 per connector.
    EXPECT_EQ(s.saturated_connectors, 2);
    EXPECT_NEAR(s.increase_W, 4936.0f + 500.0f, 0.5f);
}

TEST(RedistributionSite, NoIncreaseWithoutSaturatedConnectors) {
    const auto s = infer_site(22080.0f, make_aggregate(10000.0f, 1, 0), {}, MARGIN, GAIN);
    ASSERT_TRUE(s.headroom_W.has_value());
    EXPECT_FLOAT_EQ(s.increase_W, 0.0f);
}

TEST(RedistributionSite, ZeroGainDisablesIncrease) {
    const auto s = infer_site(22080.0f, make_aggregate(10000.0f, 1, 0), {{11040.0f, 22080.0f}}, MARGIN, 0.0f);
    EXPECT_FLOAT_EQ(s.increase_W, 0.0f);
}

// ---------------------------------------------------------------- per session context

TEST(RedistributionContext, ClearResetsInferenceState) {
    BrokerContext context;
    context.last_allocated_W = 11040.0f;
    context.under_consuming_since = T0;
    context.reduce_reported = true;

    context.clear();

    EXPECT_FALSE(context.last_allocated_W.has_value());
    EXPECT_FALSE(context.under_consuming_since.has_value());
    EXPECT_FALSE(context.reduce_reported);
}

// ---------------------------------------------------------------- optimizer integration

TEST(RedistributionIntegration, InferenceEmptyWithFastChargingStrategy) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4000.0f, FRESH);

    EnergyManagerImpl impl(test::make_default_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(1));

    const auto inference = impl.get_redistribution_inference();
    EXPECT_TRUE(inference.connectors.empty());
    EXPECT_FALSE(inference.site.grid_limit_W.has_value());
}

TEST(RedistributionIntegration, FirstRunOfASessionIsUnknown) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4000.0f, FRESH);

    EnergyManagerImpl impl(make_redistribution_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);

    const auto inference = impl.get_redistribution_inference();
    ASSERT_EQ(inference.connectors.count("evse1"), 1u);
    // No previous allocation to compare against yet.
    EXPECT_EQ(inference.connectors.at("evse1").connector_class, ConnectorClass::Unknown);
    EXPECT_FALSE(inference.connectors.at("evse1").allocated_W.has_value());
}

TEST(RedistributionIntegration, ComparesMeasurementWithPreviousAllocation) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4000.0f, FRESH);

    EnergyManagerImpl impl(make_redistribution_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(1));

    const auto c = impl.get_redistribution_inference().connectors.at("evse1");
    ASSERT_TRUE(c.allocated_W.has_value());
    EXPECT_FLOAT_EQ(c.allocated_W.value(), 32.0f * 3 * U);
    ASSERT_TRUE(c.measured_W.has_value());
    EXPECT_FLOAT_EQ(c.measured_W.value(), 4000.0f);
    EXPECT_EQ(c.connector_class, ConnectorClass::UnderConsuming);
    EXPECT_FALSE(c.held);
}

TEST(RedistributionIntegration, ReduceCandidateIsHeldOnlyAfterHoldTime) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4000.0f, FRESH);

    EnergyManagerImpl impl(make_redistribution_config(10), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(1)); // condition starts here
    EXPECT_FALSE(impl.get_redistribution_inference().connectors.at("evse1").held);

    impl.run_optimizer(request, T0 + std::chrono::seconds(6));
    EXPECT_FALSE(impl.get_redistribution_inference().connectors.at("evse1").held);

    impl.run_optimizer(request, T0 + std::chrono::seconds(11));
    EXPECT_TRUE(impl.get_redistribution_inference().connectors.at("evse1").held);
}

TEST(RedistributionIntegration, TransientCaughtUpRestartsTheHold) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_redistribution_config(10), [](const std::vector<types::energy::EnforcedLimits>&) {});
    test::set_measurement(request.children[0], 4000.0f, FRESH);
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(1));
    impl.run_optimizer(request, T0 + std::chrono::seconds(6));

    // EV briefly draws its full allocation.
    test::set_measurement(request.children[0], 22000.0f, FRESH);
    impl.run_optimizer(request, T0 + std::chrono::seconds(7));
    EXPECT_EQ(impl.get_redistribution_inference().connectors.at("evse1").connector_class, ConnectorClass::AtMaximum);

    test::set_measurement(request.children[0], 4000.0f, FRESH);
    impl.run_optimizer(request, T0 + std::chrono::seconds(8));
    impl.run_optimizer(request, T0 + std::chrono::seconds(12));
    // Only 4 s since the condition returned: not held, despite 11 s since it first appeared.
    EXPECT_FALSE(impl.get_redistribution_inference().connectors.at("evse1").held);
}

TEST(RedistributionIntegration, ZeroHoldTimeHoldsImmediately) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4000.0f, FRESH);

    EnergyManagerImpl impl(make_redistribution_config(0), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(1));

    EXPECT_TRUE(impl.get_redistribution_inference().connectors.at("evse1").held);
}

TEST(RedistributionIntegration, UnplugClearsTheComparison) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4000.0f, FRESH);

    EnergyManagerImpl impl(make_redistribution_config(0), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(1));
    ASSERT_EQ(impl.get_redistribution_inference().connectors.at("evse1").connector_class,
              ConnectorClass::UnderConsuming);

    request.children[0].evse_state = types::energy::EvseState::Unplugged;
    impl.run_optimizer(request, T0 + std::chrono::seconds(2));
    EXPECT_EQ(impl.get_redistribution_inference().connectors.at("evse1").connector_class, ConnectorClass::Unknown);

    // The next session starts from scratch: its first run has nothing to compare against,
    // so the allocation given while unplugged does not show up as a gap.
    request.children[0].evse_state = types::energy::EvseState::Charging;
    impl.run_optimizer(request, T0 + std::chrono::seconds(3));
    EXPECT_EQ(impl.get_redistribution_inference().connectors.at("evse1").connector_class, ConnectorClass::Unknown);
}

TEST(RedistributionIntegration, IncreaseProposedWhenSaturatedConnectorAndGridHeadroom) {
    // Two connectors share a 40 A fuse: each gets 20 A (13800 W) and draws it fully, while
    // the grid limit of 40 A (27600 W) leaves no headroom.
    auto evse1 = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto evse2 = test::make_evse_node("evse2", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse1, evse2});

    EnergyManagerImpl impl(make_redistribution_config(0), [](const std::vector<types::energy::EnforcedLimits>&) {});

    test::set_measurement(request.children[0], 13800.0f, FRESH);
    test::set_measurement(request.children[1], 13800.0f, FRESH);
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(1));

    auto inference = impl.get_redistribution_inference();
    EXPECT_EQ(inference.connectors.at("evse1").connector_class, ConnectorClass::Saturated);
    EXPECT_EQ(inference.connectors.at("evse2").connector_class, ConnectorClass::Saturated);
    ASSERT_TRUE(inference.site.headroom_W.has_value());
    EXPECT_NEAR(inference.site.headroom_W.value(), 0.0f, 1.0f);
    EXPECT_FLOAT_EQ(inference.site.increase_W, 0.0f);

    // evse2 leaves but the fixture keeps its 32 A request, so trading still splits the fuse:
    // evse1 stays allotted 13800 W and draws it, while the site now measures only 13800 W.
    request.children[1].evse_state = types::energy::EvseState::Unplugged;
    request.children[1].energy_usage_leaves.reset();
    impl.run_optimizer(request, T0 + std::chrono::seconds(2));
    impl.run_optimizer(request, T0 + std::chrono::seconds(3));

    inference = impl.get_redistribution_inference();
    EXPECT_EQ(inference.connectors.at("evse1").connector_class, ConnectorClass::Saturated);
    EXPECT_EQ(inference.connectors.at("evse2").connector_class, ConnectorClass::Unknown);
    EXPECT_EQ(inference.site.saturated_connectors, 1);
    ASSERT_TRUE(inference.site.headroom_W.has_value());
    EXPECT_NEAR(inference.site.headroom_W.value(), 13800.0f, 1.0f);
    // gain 0.5 x (13800 - 2760 deadband) = 5520 W, within evse1's room of 8280 W.
    EXPECT_NEAR(inference.site.increase_W, 5520.0f, 1.0f);
}

TEST(RedistributionIntegration, IncreaseUsesGainOverHeadroomBeyondDeadband) {
    // One connector limited to 16 A by its own schedule (11040 W) drawing it fully; the grid
    // allows 32 A (22080 W). Headroom 11040 W, deadband 2208 W -> gain 0.5 x 8832 = 4416 W,
    // but the connector's static maximum is already reached, so nothing can be increased.
    auto evse = test::make_evse_node("evse1", 16.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 11040.0f, FRESH);

    EnergyManagerImpl impl(make_redistribution_config(0), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(1));

    const auto inference = impl.get_redistribution_inference();
    EXPECT_EQ(inference.connectors.at("evse1").connector_class, ConnectorClass::AtMaximum);
    ASSERT_TRUE(inference.site.headroom_W.has_value());
    EXPECT_NEAR(inference.site.headroom_W.value(), 11040.0f, 1.0f);
    EXPECT_EQ(inference.site.saturated_connectors, 0);
    EXPECT_FLOAT_EQ(inference.site.increase_W, 0.0f);
}

TEST(RedistributionIntegration, IncreaseHeldOnlyAfterHoldTime) {
    // Grid declares 32 A but a total_power_W of 30000 W, so the trading limit (22080 W from
    // ampere) is below the grid's watt capacity: evse1 saturates at 22080 W with headroom.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, 30000.0f, {evse});
    test::set_measurement(request.children[0], 22000.0f, FRESH);

    EnergyManagerImpl impl(make_redistribution_config(10), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(1));

    auto inference = impl.get_redistribution_inference();
    ASSERT_TRUE(inference.site.grid_limit_W.has_value());
    EXPECT_FLOAT_EQ(inference.site.grid_limit_W.value(), 30000.0f);
    EXPECT_EQ(inference.connectors.at("evse1").connector_class, ConnectorClass::AtMaximum);
    // At its static maximum the connector cannot take more: no increase even with headroom.
    EXPECT_FLOAT_EQ(inference.site.increase_W, 0.0f);
}

TEST(RedistributionIntegration, IncreaseReportedAfterHoldForSaturatedConnector) {
    // Two connectors on a shared 40 A fuse node below a 30 kW grid: each gets 20 A (13800 W),
    // both draw it, both are Saturated (max 32 A each), and the grid has headroom.
    auto evse1 = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto evse2 = test::make_evse_node("evse2", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, 30000.0f, {evse1, evse2});

    EnergyManagerImpl impl(make_redistribution_config(10), [](const std::vector<types::energy::EnforcedLimits>&) {});
    auto run = [&](int seconds, float measured_W) {
        test::set_measurement(request.children[0], measured_W, fresh_at(seconds));
        test::set_measurement(request.children[1], measured_W, fresh_at(seconds));
        impl.run_optimizer(request, T0 + std::chrono::seconds(seconds));
        return impl.get_redistribution_inference();
    };

    run(0, 13800.0f);
    auto inference = run(1, 13800.0f);
    EXPECT_EQ(inference.connectors.at("evse1").connector_class, ConnectorClass::Saturated);
    EXPECT_EQ(inference.site.saturated_connectors, 2);
    // headroom 30000 - 27600 = 2400 W is below the 3000 W deadband: nothing yet.
    EXPECT_FLOAT_EQ(inference.site.increase_W, 0.0f);

    // Both EVs taper to 13000 W: still within the margin (5.8 %) so Saturated, headroom 4000 W.
    inference = run(2, 13000.0f);
    EXPECT_EQ(inference.site.saturated_connectors, 2);
    EXPECT_NEAR(inference.site.increase_W, 0.5f * (4000.0f - 3000.0f), 1.0f);
    EXPECT_FALSE(inference.site.held);

    EXPECT_FALSE(run(7, 13000.0f).site.held);
    EXPECT_TRUE(run(12, 13000.0f).site.held);

    // Headroom gone: the report clears and the hold starts over.
    inference = run(13, 13800.0f);
    EXPECT_FLOAT_EQ(inference.site.increase_W, 0.0f);
    EXPECT_FALSE(inference.site.held);
    EXPECT_FALSE(run(14, 13000.0f).site.held);
}

TEST(RedistributionIntegration, AllocationsIdenticalToFastCharging) {
    // The inference must never touch the allocation, whatever it concludes.
    auto make_request = []() {
        auto evse1 = test::make_evse_node("evse1", 32.0f, 6.0f);
        auto evse2 = test::make_evse_node("evse2", 32.0f, 6.0f);
        auto request = test::make_root_node("grid", 40.0f, 30000.0f, {evse1, evse2});
        test::set_measurement(request.children[0], 3000.0f, FRESH);
        test::set_measurement(request.children[1], 13000.0f, FRESH);
        return request;
    };
    const auto request = make_request();

    EnergyManagerImpl inferring(make_redistribution_config(0),
                                [](const std::vector<types::energy::EnforcedLimits>&) {});
    EnergyManagerImpl statik(test::make_default_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    for (int run = 0; run < 3; run++) {
        const auto at = T0 + std::chrono::seconds(run);
        const auto a = inferring.run_optimizer(request, at);
        const auto b = statik.run_optimizer(request, at);
        for (const auto* uuid : {"evse1", "evse2"}) {
            const auto la = test::find_limit(a, uuid);
            const auto lb = test::find_limit(b, uuid);
            ASSERT_TRUE(la.has_value());
            ASSERT_TRUE(lb.has_value());
            EXPECT_FLOAT_EQ(la.value().limits_root_side.ac_max_current_A.value().value,
                            lb.value().limits_root_side.ac_max_current_A.value().value);
        }
    }
    EXPECT_EQ(inferring.get_redistribution_inference().connectors.at("evse1").connector_class,
              ConnectorClass::UnderConsuming);
}

} // namespace module
