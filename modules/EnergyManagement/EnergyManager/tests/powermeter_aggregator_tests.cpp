// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <utils/date.hpp>

#include "EnergyManagerImpl.hpp"
#include "EnergyManagerTestHelpers.hpp"
#include "PowerMeterAggregator.hpp"

namespace module {

namespace {

// Builds a reading whose own measurement timestamp is `age` seconds before `reference`.
types::powermeter::Powermeter make_reading(float total_W, date::utc_clock::time_point reference,
                                           std::chrono::seconds age) {
    types::powermeter::Powermeter p;
    p.timestamp = Everest::Date::to_rfc3339(reference - age);
    p.energy_Wh_import.total = 0.0f;
    types::units::Power power;
    power.total = total_W;
    p.power_W = power;
    return p;
}

// Adds three phase current to a reading, the way an AC meter reports alongside power.
types::powermeter::Powermeter with_current(types::powermeter::Powermeter reading, std::optional<float> l1_A,
                                           std::optional<float> l2_A, std::optional<float> l3_A) {
    types::units::Current current;
    current.L1 = l1_A;
    current.L2 = l2_A;
    current.L3 = l3_A;
    reading.current_A = current;
    return reading;
}

types::powermeter::Powermeter make_per_phase_reading(float l1_W, float l2_W, float l3_W,
                                                     date::utc_clock::time_point reference, std::chrono::seconds age) {
    auto p = make_reading(l1_W + l2_W + l3_W, reference, age);
    p.power_W.value().L1 = l1_W;
    p.power_W.value().L2 = l2_W;
    p.power_W.value().L3 = l3_W;
    return p;
}

const auto NOW = Everest::Date::from_rfc3339("2026-08-04T12:00:00.000Z");

types::energy::EnergyFlowRequest make_node(const std::string& uuid, types::energy::NodeType type) {
    types::energy::EnergyFlowRequest n;
    n.uuid = uuid;
    n.node_type = type;
    return n;
}

} // namespace

// ---------------------------------------------------------------- storage and summing

TEST(PowerMeterAggregatorStorage, EmptyAggregateReportsNoTotal) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FALSE(result.power_W.has_value());
    EXPECT_EQ(result.fresh_meters, 0);
    EXPECT_EQ(result.stale_meters, 0);
    // No meter contributed, so no phase is covered either.
    EXPECT_FALSE(result.current_A.L1.has_value());
}

TEST(PowerMeterAggregatorStorage, NoDataIsDistinguishableFromGenuineZeroPower) {
    PowerMeterAggregator without_meters(std::chrono::seconds(5));

    PowerMeterAggregator with_a_zero_reading(std::chrono::seconds(5));
    with_a_zero_reading.update("cp01", make_reading(0.0f, NOW, std::chrono::seconds(0)));

    // No meter contributed, so there is no total to report - not a total of zero.
    EXPECT_FALSE(without_meters.aggregate(NOW).power_W.has_value());
    // A meter that genuinely measures no flow does report a total, and it is zero.
    const auto measured = with_a_zero_reading.aggregate(NOW).power_W;
    ASSERT_TRUE(measured.has_value());
    EXPECT_FLOAT_EQ(measured.value().total, 0.0f);
}

TEST(PowerMeterAggregatorStorage, SumsMultipleMeters) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::seconds(0)));
    aggregator.update("cp02", make_reading(2500.0f, NOW, std::chrono::seconds(0)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.power_W.value().total, 3500.0f);
    EXPECT_EQ(result.fresh_meters, 2);
}

TEST(PowerMeterAggregatorStorage, UpdateReplacesReadingForSameNode) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::seconds(0)));
    aggregator.update("cp01", make_reading(1800.0f, NOW, std::chrono::seconds(0)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_EQ(aggregator.size(), 1U);
    EXPECT_FLOAT_EQ(result.power_W.value().total, 1800.0f);
    EXPECT_EQ(result.fresh_meters, 1);
}

TEST(PowerMeterAggregatorStorage, ClearDropsAllReadings) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::seconds(0)));
    aggregator.clear();

    EXPECT_EQ(aggregator.size(), 0U);
    EXPECT_FALSE(aggregator.aggregate(NOW).power_W.has_value());
}

TEST(PowerMeterAggregatorStorage, SumsPerPhaseWhenAllMetersReportIt) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_per_phase_reading(1000.0f, 900.0f, 800.0f, NOW, std::chrono::seconds(0)));
    aggregator.update("cp02", make_per_phase_reading(500.0f, 400.0f, 300.0f, NOW, std::chrono::seconds(0)));

    const auto result = aggregator.aggregate(NOW);

    ASSERT_TRUE(result.power_W.has_value());
    EXPECT_FLOAT_EQ(result.power_W.value().L1.value(), 1500.0f);
    EXPECT_FLOAT_EQ(result.power_W.value().L2.value(), 1300.0f);
    EXPECT_FLOAT_EQ(result.power_W.value().L3.value(), 1100.0f);
}

TEST(PowerMeterAggregatorStorage, PerPhaseUnavailableIfAnyMeterOmitsIt) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_per_phase_reading(1000.0f, 900.0f, 800.0f, NOW, std::chrono::seconds(0)));
    aggregator.update("cp02", make_reading(1200.0f, NOW, std::chrono::seconds(0)));

    const auto result = aggregator.aggregate(NOW);

    ASSERT_TRUE(result.power_W.has_value());
    EXPECT_FALSE(result.power_W.value().L1.has_value());
    EXPECT_FALSE(result.power_W.value().L2.has_value());
    EXPECT_FALSE(result.power_W.value().L3.has_value());
    // The total is still a correct sum across both meters.
    EXPECT_FLOAT_EQ(result.power_W.value().total, 3900.0f);
}

TEST(PowerMeterAggregatorStorage, SumsThePhasesEveryContributingMeterReports) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    // A three phase meter next to a single phase one: only L1 is covered by both, so only
    // L1 can be summed. L2 and L3 are unset rather than reporting a sum that silently
    // omits cp02 - the single phase meter does not report zero on L2, it reports nothing.
    aggregator.update("cp01", make_per_phase_reading(1000.0f, 900.0f, 800.0f, NOW, std::chrono::seconds(0)));
    auto single_phase = make_reading(500.0f, NOW, std::chrono::seconds(0));
    single_phase.power_W.value().L1 = 500.0f;
    aggregator.update("cp02", single_phase);

    const auto result = aggregator.aggregate(NOW);

    ASSERT_TRUE(result.power_W.has_value());
    EXPECT_FLOAT_EQ(result.power_W.value().total, 3200.0f);
    ASSERT_TRUE(result.power_W.value().L1.has_value());
    EXPECT_FLOAT_EQ(result.power_W.value().L1.value(), 1500.0f);
    EXPECT_FALSE(result.power_W.value().L2.has_value());
    EXPECT_FALSE(result.power_W.value().L3.has_value());
}

TEST(PowerMeterAggregatorStorage, ReadingWithoutPowerValueCountsAsStale) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    auto no_power = make_reading(0.0f, NOW, std::chrono::seconds(0));
    no_power.power_W.reset();
    aggregator.update("cp01", no_power);
    aggregator.update("cp02", make_reading(1200.0f, NOW, std::chrono::seconds(0)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_EQ(result.stale_meters, 1);
    EXPECT_EQ(result.fresh_meters, 1);
    EXPECT_FLOAT_EQ(result.power_W.value().total, 1200.0f);
}

TEST(PowerMeterAggregatorStorage, NegativePowerFromExportingMeterReducesTheSum) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_reading(3000.0f, NOW, std::chrono::seconds(0)));
    // A bidirectional meter reporting export: the aggregate is a net sum.
    aggregator.update("cp02", make_reading(-1000.0f, NOW, std::chrono::seconds(0)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.power_W.value().total, 2000.0f);
    EXPECT_EQ(result.fresh_meters, 2);
}

TEST(PowerMeterAggregatorCurrent, SumsPerPhaseCurrentAcrossMeters) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", with_current(make_reading(1000.0f, NOW, std::chrono::seconds(0)), 16.0f, 15.0f, 14.0f));
    aggregator.update("cp02", with_current(make_reading(2000.0f, NOW, std::chrono::seconds(0)), 10.0f, 9.0f, 8.0f));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.current_A.L1.value(), 26.0f);
    EXPECT_FLOAT_EQ(result.current_A.L2.value(), 24.0f);
    EXPECT_FLOAT_EQ(result.current_A.L3.value(), 22.0f);
}

TEST(PowerMeterAggregatorCurrent, SinglePhaseMeterLeavesTheUnmeasuredPhasesUnset) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    // The single phase meter reports nothing on L2/L3 - not zero - so those sums would
    // silently cover only cp01 and must stay unset.
    aggregator.update("cp01", with_current(make_reading(1000.0f, NOW, std::chrono::seconds(0)), 16.0f, 15.0f, 14.0f));
    aggregator.update(
        "cp02", with_current(make_reading(2000.0f, NOW, std::chrono::seconds(0)), 10.0f, std::nullopt, std::nullopt));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.current_A.L1.value(), 26.0f);
    EXPECT_FALSE(result.current_A.L2.has_value());
    EXPECT_FALSE(result.current_A.L3.has_value());
}

TEST(PowerMeterAggregatorCurrent, MeterReportingNoCurrentAtAllLeavesEveryPhaseUnset) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", with_current(make_reading(1000.0f, NOW, std::chrono::seconds(0)), 16.0f, 15.0f, 14.0f));
    // A meter that publishes power but no current at all: the site current is unknown.
    aggregator.update("cp02", make_reading(2000.0f, NOW, std::chrono::seconds(0)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FALSE(result.current_A.L1.has_value());
    // The power total is unaffected - it does not depend on current.
    EXPECT_FLOAT_EQ(result.power_W.value().total, 3000.0f);
}

TEST(PowerMeterAggregatorCurrent, StaleMeterIsExcludedFromCurrentSums) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", with_current(make_reading(1000.0f, NOW, std::chrono::seconds(0)), 16.0f, 15.0f, 14.0f));
    aggregator.update("cp02", with_current(make_reading(2000.0f, NOW, std::chrono::seconds(60)), 10.0f, 9.0f, 8.0f));

    const auto result = aggregator.aggregate(NOW);

    // Only the fresh meter contributes, exactly as for power.
    EXPECT_FLOAT_EQ(result.current_A.L1.value(), 16.0f);
    EXPECT_EQ(result.stale_meters, 1);
}

TEST(PowerMeterAggregatorCurrent, SumsDcCurrent) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    auto dc = make_reading(20000.0f, NOW, std::chrono::seconds(0));
    types::units::Current current;
    current.DC = 50.0f;
    dc.current_A = current;
    aggregator.update("cp01", dc);

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.current_A.DC.value(), 50.0f);
    EXPECT_FALSE(result.current_A.L1.has_value());
}

// ---------------------------------------------------------------- windowed staleness

TEST(PowerMeterAggregatorWindow, ExcludesReadingOlderThanWindow) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::seconds(1)));
    aggregator.update("cp02", make_reading(2500.0f, NOW, std::chrono::seconds(30)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.power_W.value().total, 1000.0f);
    EXPECT_EQ(result.fresh_meters, 1);
    EXPECT_EQ(result.stale_meters, 1);
}

TEST(PowerMeterAggregatorWindow, SumsMixedAgeReadingsWithinWindow) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    // Three meters reporting at different times, all inside the window.
    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::seconds(0)));
    aggregator.update("cp02", make_reading(2000.0f, NOW, std::chrono::seconds(2)));
    aggregator.update("cp03", make_reading(3000.0f, NOW, std::chrono::seconds(4)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.power_W.value().total, 6000.0f);
    EXPECT_EQ(result.fresh_meters, 3);
    EXPECT_EQ(result.stale_meters, 0);
}

TEST(PowerMeterAggregatorWindow, ReadingExactlyAtWindowEdgeIsStale) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::seconds(5)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FALSE(result.power_W.has_value());
    EXPECT_EQ(result.stale_meters, 1);
}

TEST(PowerMeterAggregatorWindow, SubSecondAgesResolveAtMillisecondPrecision) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    auto fresh = make_reading(1000.0f, NOW, std::chrono::seconds(0));
    fresh.timestamp = Everest::Date::to_rfc3339(NOW - std::chrono::milliseconds(4999));
    aggregator.update("cp01", fresh);

    auto stale = make_reading(2000.0f, NOW, std::chrono::seconds(0));
    stale.timestamp = Everest::Date::to_rfc3339(NOW - std::chrono::milliseconds(5001));
    aggregator.update("cp02", stale);

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.power_W.value().total, 1000.0f);
    EXPECT_EQ(result.fresh_meters, 1);
    EXPECT_EQ(result.stale_meters, 1);
}

TEST(PowerMeterAggregatorWindow, ZeroWindowDisablesTheFilter) {
    PowerMeterAggregator aggregator(std::chrono::seconds(0));

    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::hours(3)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.power_W.value().total, 1000.0f);
    EXPECT_EQ(result.fresh_meters, 1);
    EXPECT_EQ(result.stale_meters, 0);
}

TEST(PowerMeterAggregatorWindow, FutureTimestampCountsAsFresh) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    // Minor clock skew between meter and controller must not discard the reading.
    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::seconds(-1)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.power_W.value().total, 1000.0f);
    EXPECT_EQ(result.fresh_meters, 1);
}

TEST(PowerMeterAggregatorWindow, UnparsableTimestampIsStale) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    auto broken = make_reading(1000.0f, NOW, std::chrono::seconds(0));
    broken.timestamp = "not-a-timestamp";
    aggregator.update("cp01", broken);
    aggregator.update("cp02", make_reading(700.0f, NOW, std::chrono::seconds(0)));

    PowerMeterAggregator::AggregateResult result;
    ASSERT_NO_THROW(result = aggregator.aggregate(NOW));

    EXPECT_FLOAT_EQ(result.power_W.value().total, 700.0f);
    EXPECT_EQ(result.fresh_meters, 1);
    EXPECT_EQ(result.stale_meters, 1);
}

TEST(PowerMeterAggregatorWindow, StaleMeterIsExcludedFromPerPhaseSums) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_per_phase_reading(1000.0f, 900.0f, 800.0f, NOW, std::chrono::seconds(0)));
    aggregator.update("cp02", make_per_phase_reading(500.0f, 400.0f, 300.0f, NOW, std::chrono::seconds(60)));

    const auto result = aggregator.aggregate(NOW);

    ASSERT_TRUE(result.power_W.has_value());
    EXPECT_FLOAT_EQ(result.power_W.value().L1.value(), 1000.0f);
    EXPECT_FLOAT_EQ(result.power_W.value().L2.value(), 900.0f);
    EXPECT_FLOAT_EQ(result.power_W.value().L3.value(), 800.0f);
    EXPECT_EQ(result.stale_meters, 1);
}

// ---------------------------------------------------------------- tree collection

TEST(CollectLeafMeasurements, CollectsFromEvseNodesOnly) {
    auto grid = make_node("grid", types::energy::NodeType::Generic);
    // The grid meter measures the sum of the two EVSE meters; counting it would double.
    grid.energy_usage_root = make_reading(9999.0f, NOW, std::chrono::seconds(0));

    auto cp01 = make_node("cp01", types::energy::NodeType::Evse);
    cp01.energy_usage_leaves = make_reading(1000.0f, NOW, std::chrono::seconds(0));

    auto cp02 = make_node("cp02", types::energy::NodeType::Evse);
    cp02.energy_usage_leaves = make_reading(2000.0f, NOW, std::chrono::seconds(0));

    grid.children = {cp01, cp02};

    PowerMeterAggregator aggregator(std::chrono::seconds(5));
    collect_leaf_measurements(grid, aggregator);

    EXPECT_EQ(aggregator.size(), 2U);
    EXPECT_FLOAT_EQ(aggregator.aggregate(NOW).power_W.value().total, 3000.0f);
}

TEST(CollectLeafMeasurements, RecursesThroughNestedClusters) {
    auto cp01 = make_node("cp01", types::energy::NodeType::Evse);
    cp01.energy_usage_leaves = make_reading(1000.0f, NOW, std::chrono::seconds(0));
    auto cp02 = make_node("cp02", types::energy::NodeType::Evse);
    cp02.energy_usage_leaves = make_reading(1500.0f, NOW, std::chrono::seconds(0));

    auto cluster0 = make_node("cluster0", types::energy::NodeType::Generic);
    cluster0.children = {cp01, cp02};

    auto cp07 = make_node("cp07", types::energy::NodeType::Evse);
    cp07.energy_usage_leaves = make_reading(500.0f, NOW, std::chrono::seconds(0));

    auto cluster1 = make_node("cluster1", types::energy::NodeType::Generic);
    cluster1.children = {cp07};

    auto grid = make_node("grid", types::energy::NodeType::Generic);
    grid.children = {cluster0, cluster1};

    PowerMeterAggregator aggregator(std::chrono::seconds(5));
    collect_leaf_measurements(grid, aggregator);

    EXPECT_EQ(aggregator.size(), 3U);
    EXPECT_FLOAT_EQ(aggregator.aggregate(NOW).power_W.value().total, 3000.0f);
}

TEST(CollectLeafMeasurements, FallsBackToRootMeasurementOnEvseNode) {
    auto cp01 = make_node("cp01", types::energy::NodeType::Evse);
    cp01.energy_usage_root = make_reading(1200.0f, NOW, std::chrono::seconds(0));

    PowerMeterAggregator aggregator(std::chrono::seconds(5));
    collect_leaf_measurements(cp01, aggregator);

    EXPECT_EQ(aggregator.size(), 1U);
    EXPECT_FLOAT_EQ(aggregator.aggregate(NOW).power_W.value().total, 1200.0f);
}

TEST(CollectLeafMeasurements, DoesNotRecurseBelowAnEvseNode) {
    // An EVSE's own meter already covers everything downstream of it, so a child meter
    // below an EVSE must not be counted a second time.
    auto child = make_node("cp01_sub", types::energy::NodeType::Evse);
    child.energy_usage_leaves = make_reading(1000.0f, NOW, std::chrono::seconds(0));

    auto cp01 = make_node("cp01", types::energy::NodeType::Evse);
    cp01.energy_usage_leaves = make_reading(3000.0f, NOW, std::chrono::seconds(0));
    cp01.children = {child};

    PowerMeterAggregator aggregator(std::chrono::seconds(5));
    collect_leaf_measurements(cp01, aggregator);

    EXPECT_EQ(aggregator.size(), 1U);
    EXPECT_FLOAT_EQ(aggregator.aggregate(NOW).power_W.value().total, 3000.0f);
}

TEST(CollectLeafMeasurements, SkipsEvseNodesWithoutMeasurement) {
    auto cp01 = make_node("cp01", types::energy::NodeType::Evse);
    auto cp02 = make_node("cp02", types::energy::NodeType::Evse);
    cp02.energy_usage_leaves = make_reading(800.0f, NOW, std::chrono::seconds(0));

    auto grid = make_node("grid", types::energy::NodeType::Generic);
    grid.children = {cp01, cp02};

    PowerMeterAggregator aggregator(std::chrono::seconds(5));
    collect_leaf_measurements(grid, aggregator);

    // A connector with no meter at all is absent, not a stale entry.
    EXPECT_EQ(aggregator.size(), 1U);
    EXPECT_EQ(aggregator.aggregate(NOW).stale_meters, 0);
    EXPECT_FLOAT_EQ(aggregator.aggregate(NOW).power_W.value().total, 800.0f);
}

// ---------------------------------------------------------------- optimizer wiring

namespace {

// Every manifest option comes from the shared factory, so a newly added option is set here
// too instead of being read uninitialised; only the value under test is overridden.
EnergyManagerConfig make_aggregation_config() {
    auto c = test::make_default_config();
    c.power_meter_aggregation_window_s = 5;
    return c;
}

} // namespace

// A config option the factories forget is read uninitialised by EnergyManagerImpl - the
// aggregation window becomes a garbage number of seconds - so the factories must set every
// manifest option, and these assert the manifest defaults for the two easiest to forget.

TEST(EnergyManagerConfigFactories, DefaultConfigSetsTheAggregationWindow) {
    EXPECT_EQ(test::make_default_config().power_meter_aggregation_window_s, 5);
}

TEST(EnergyManagerConfigFactories, AggregationConfigSetsTheBrokerStrategy) {
    EXPECT_EQ(make_aggregation_config().broker_strategy, "FastCharging");
}

TEST(AggregatorWiring, RunOptimizerRefreshesTheLeafAggregate) {
    const std::string ts = "2026-08-04T12:00:00.000Z";
    const auto at = Everest::Date::from_rfc3339("2026-08-04T12:00:02.000Z");

    auto cp01 = make_node("cp01", types::energy::NodeType::Evse);
    cp01.evse_state = types::energy::EvseState::Charging;
    cp01.schedule_import = {test::make_schedule_entry(ts, 32.0f, 6.0f)};
    cp01.schedule_export = {test::make_schedule_entry(ts, 0.0f, 0.0f)};
    cp01.energy_usage_leaves = make_reading(1000.0f, at, std::chrono::seconds(1));

    auto cp02 = make_node("cp02", types::energy::NodeType::Evse);
    cp02.evse_state = types::energy::EvseState::Charging;
    cp02.schedule_import = {test::make_schedule_entry(ts, 32.0f, 6.0f)};
    cp02.schedule_export = {test::make_schedule_entry(ts, 0.0f, 0.0f)};
    // This one is well outside the 5s window and must be excluded.
    cp02.energy_usage_leaves = make_reading(2500.0f, at, std::chrono::seconds(60));

    auto grid = make_node("grid", types::energy::NodeType::Generic);
    grid.schedule_import = {test::make_schedule_entry(ts, 63.0f, 0.0f)};
    grid.schedule_export = {test::make_schedule_entry(ts, 0.0f, 0.0f)};
    grid.children = {cp01, cp02};

    EnergyManagerImpl impl(make_aggregation_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    impl.run_optimizer(grid, at);

    const auto& aggregate = impl.get_leaf_aggregate();
    EXPECT_FLOAT_EQ(aggregate.power_W.value().total, 1000.0f);
    EXPECT_EQ(aggregate.fresh_meters, 1);
    EXPECT_EQ(aggregate.stale_meters, 1);
}

TEST(AggregatorWiring, AggregateDoesNotAccumulateAcrossRuns) {
    const std::string ts = "2026-08-04T12:00:00.000Z";
    const auto at = Everest::Date::from_rfc3339("2026-08-04T12:00:02.000Z");

    auto cp01 = make_node("cp01", types::energy::NodeType::Evse);
    cp01.evse_state = types::energy::EvseState::Charging;
    cp01.schedule_import = {test::make_schedule_entry(ts, 32.0f, 6.0f)};
    cp01.schedule_export = {test::make_schedule_entry(ts, 0.0f, 0.0f)};
    cp01.energy_usage_leaves = make_reading(1000.0f, at, std::chrono::seconds(1));

    auto grid = make_node("grid", types::energy::NodeType::Generic);
    grid.schedule_import = {test::make_schedule_entry(ts, 63.0f, 0.0f)};
    grid.schedule_export = {test::make_schedule_entry(ts, 0.0f, 0.0f)};
    grid.children = {cp01};

    EnergyManagerImpl impl(make_aggregation_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    impl.run_optimizer(grid, at);
    impl.run_optimizer(grid, at);

    // Two runs over one meter must still report one meter, not two.
    EXPECT_FLOAT_EQ(impl.get_leaf_aggregate().power_W.value().total, 1000.0f);
    EXPECT_EQ(impl.get_leaf_aggregate().fresh_meters, 1);
}

} // namespace module
