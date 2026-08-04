// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <utils/date.hpp>

#include "EnergyManagerImpl.hpp"
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

TEST(PowerMeterAggregatorStorage, EmptyAggregateIsZero) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.power_W, 0.0f);
    EXPECT_EQ(result.fresh_meters, 0);
    EXPECT_EQ(result.stale_meters, 0);
    EXPECT_FALSE(result.per_phase_available);
}

TEST(PowerMeterAggregatorStorage, SumsMultipleMeters) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::seconds(0)));
    aggregator.update("cp02", make_reading(2500.0f, NOW, std::chrono::seconds(0)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.power_W, 3500.0f);
    EXPECT_EQ(result.fresh_meters, 2);
}

TEST(PowerMeterAggregatorStorage, UpdateReplacesReadingForSameNode) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::seconds(0)));
    aggregator.update("cp01", make_reading(1800.0f, NOW, std::chrono::seconds(0)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_EQ(aggregator.size(), 1U);
    EXPECT_FLOAT_EQ(result.power_W, 1800.0f);
    EXPECT_EQ(result.fresh_meters, 1);
}

TEST(PowerMeterAggregatorStorage, ClearDropsAllReadings) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::seconds(0)));
    aggregator.clear();

    EXPECT_EQ(aggregator.size(), 0U);
    EXPECT_FLOAT_EQ(aggregator.aggregate(NOW).power_W, 0.0f);
}

TEST(PowerMeterAggregatorStorage, SumsPerPhaseWhenAllMetersReportIt) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_per_phase_reading(1000.0f, 900.0f, 800.0f, NOW, std::chrono::seconds(0)));
    aggregator.update("cp02", make_per_phase_reading(500.0f, 400.0f, 300.0f, NOW, std::chrono::seconds(0)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_TRUE(result.per_phase_available);
    EXPECT_FLOAT_EQ(result.power_L1_W, 1500.0f);
    EXPECT_FLOAT_EQ(result.power_L2_W, 1300.0f);
    EXPECT_FLOAT_EQ(result.power_L3_W, 1100.0f);
}

TEST(PowerMeterAggregatorStorage, PerPhaseUnavailableIfAnyMeterOmitsIt) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_per_phase_reading(1000.0f, 900.0f, 800.0f, NOW, std::chrono::seconds(0)));
    aggregator.update("cp02", make_reading(1200.0f, NOW, std::chrono::seconds(0)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FALSE(result.per_phase_available);
    // The total is still a correct sum across both meters.
    EXPECT_FLOAT_EQ(result.power_W, 3900.0f);
}

// ---------------------------------------------------------------- windowed staleness

TEST(PowerMeterAggregatorWindow, ExcludesReadingOlderThanWindow) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::seconds(1)));
    aggregator.update("cp02", make_reading(2500.0f, NOW, std::chrono::seconds(30)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.power_W, 1000.0f);
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

    EXPECT_FLOAT_EQ(result.power_W, 6000.0f);
    EXPECT_EQ(result.fresh_meters, 3);
    EXPECT_EQ(result.stale_meters, 0);
}

TEST(PowerMeterAggregatorWindow, ReadingExactlyAtWindowEdgeIsStale) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::seconds(5)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.power_W, 0.0f);
    EXPECT_EQ(result.stale_meters, 1);
}

TEST(PowerMeterAggregatorWindow, ZeroWindowDisablesTheFilter) {
    PowerMeterAggregator aggregator(std::chrono::seconds(0));

    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::hours(3)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.power_W, 1000.0f);
    EXPECT_EQ(result.fresh_meters, 1);
    EXPECT_EQ(result.stale_meters, 0);
}

TEST(PowerMeterAggregatorWindow, FutureTimestampCountsAsFresh) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    // Minor clock skew between meter and controller must not discard the reading.
    aggregator.update("cp01", make_reading(1000.0f, NOW, std::chrono::seconds(-1)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_FLOAT_EQ(result.power_W, 1000.0f);
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

    EXPECT_FLOAT_EQ(result.power_W, 700.0f);
    EXPECT_EQ(result.fresh_meters, 1);
    EXPECT_EQ(result.stale_meters, 1);
}

TEST(PowerMeterAggregatorWindow, StaleMeterIsExcludedFromPerPhaseSums) {
    PowerMeterAggregator aggregator(std::chrono::seconds(5));

    aggregator.update("cp01", make_per_phase_reading(1000.0f, 900.0f, 800.0f, NOW, std::chrono::seconds(0)));
    aggregator.update("cp02", make_per_phase_reading(500.0f, 400.0f, 300.0f, NOW, std::chrono::seconds(60)));

    const auto result = aggregator.aggregate(NOW);

    EXPECT_TRUE(result.per_phase_available);
    EXPECT_FLOAT_EQ(result.power_L1_W, 1000.0f);
    EXPECT_FLOAT_EQ(result.power_L2_W, 900.0f);
    EXPECT_FLOAT_EQ(result.power_L3_W, 800.0f);
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
    EXPECT_FLOAT_EQ(aggregator.aggregate(NOW).power_W, 3000.0f);
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
    EXPECT_FLOAT_EQ(aggregator.aggregate(NOW).power_W, 3000.0f);
}

TEST(CollectLeafMeasurements, FallsBackToRootMeasurementOnEvseNode) {
    auto cp01 = make_node("cp01", types::energy::NodeType::Evse);
    cp01.energy_usage_root = make_reading(1200.0f, NOW, std::chrono::seconds(0));

    PowerMeterAggregator aggregator(std::chrono::seconds(5));
    collect_leaf_measurements(cp01, aggregator);

    EXPECT_EQ(aggregator.size(), 1U);
    EXPECT_FLOAT_EQ(aggregator.aggregate(NOW).power_W, 1200.0f);
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
    EXPECT_FLOAT_EQ(aggregator.aggregate(NOW).power_W, 800.0f);
}

// ---------------------------------------------------------------- optimizer wiring

namespace {

EnergyManagerConfig make_aggregation_config() {
    EnergyManagerConfig c;
    c.nominal_ac_voltage = 230.0;
    c.update_interval = 1;
    c.schedule_interval_duration = 60;
    c.schedule_total_duration = 1;
    c.slice_ampere = 0.5;
    c.slice_watt = 500;
    c.debug = false;
    c.switch_3ph1ph_while_charging_mode = "Never";
    c.switch_3ph1ph_max_nr_of_switches_per_session = 0;
    c.switch_3ph1ph_switch_limit_stickyness = "DontChange";
    c.switch_3ph1ph_power_hysteresis_W = 200;
    c.switch_3ph1ph_time_hysteresis_s = 0;
    c.power_meter_aggregation_window_s = 5;
    return c;
}

types::energy::ScheduleReqEntry make_entry(const std::string& timestamp, float max_current_A, float min_current_A) {
    types::energy::ScheduleReqEntry e;
    e.timestamp = timestamp;
    e.limits_to_root.ac_max_current_A = {max_current_A, "TEST_max_current"};
    e.limits_to_root.ac_min_current_A = {min_current_A, "TEST_min_current"};
    e.limits_to_root.ac_max_phase_count = {3, "TEST_max_phases"};
    e.limits_to_root.ac_min_phase_count = {1, "TEST_min_phases"};
    e.limits_to_root.ac_number_of_active_phases = 3;
    return e;
}

} // namespace

TEST(AggregatorWiring, RunOptimizerRefreshesTheLeafAggregate) {
    const std::string ts = "2026-08-04T12:00:00.000Z";
    const auto at = Everest::Date::from_rfc3339("2026-08-04T12:00:02.000Z");

    auto cp01 = make_node("cp01", types::energy::NodeType::Evse);
    cp01.evse_state = types::energy::EvseState::Charging;
    cp01.schedule_import = {make_entry(ts, 32.0f, 6.0f)};
    cp01.schedule_export = {make_entry(ts, 0.0f, 0.0f)};
    cp01.energy_usage_leaves = make_reading(1000.0f, at, std::chrono::seconds(1));

    auto cp02 = make_node("cp02", types::energy::NodeType::Evse);
    cp02.evse_state = types::energy::EvseState::Charging;
    cp02.schedule_import = {make_entry(ts, 32.0f, 6.0f)};
    cp02.schedule_export = {make_entry(ts, 0.0f, 0.0f)};
    // This one is well outside the 5s window and must be excluded.
    cp02.energy_usage_leaves = make_reading(2500.0f, at, std::chrono::seconds(60));

    auto grid = make_node("grid", types::energy::NodeType::Generic);
    grid.schedule_import = {make_entry(ts, 63.0f, 0.0f)};
    grid.schedule_export = {make_entry(ts, 0.0f, 0.0f)};
    grid.children = {cp01, cp02};

    EnergyManagerImpl impl(make_aggregation_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    impl.run_optimizer(grid, at);

    const auto& aggregate = impl.get_leaf_aggregate();
    EXPECT_FLOAT_EQ(aggregate.power_W, 1000.0f);
    EXPECT_EQ(aggregate.fresh_meters, 1);
    EXPECT_EQ(aggregate.stale_meters, 1);
}

TEST(AggregatorWiring, AggregateDoesNotAccumulateAcrossRuns) {
    const std::string ts = "2026-08-04T12:00:00.000Z";
    const auto at = Everest::Date::from_rfc3339("2026-08-04T12:00:02.000Z");

    auto cp01 = make_node("cp01", types::energy::NodeType::Evse);
    cp01.evse_state = types::energy::EvseState::Charging;
    cp01.schedule_import = {make_entry(ts, 32.0f, 6.0f)};
    cp01.schedule_export = {make_entry(ts, 0.0f, 0.0f)};
    cp01.energy_usage_leaves = make_reading(1000.0f, at, std::chrono::seconds(1));

    auto grid = make_node("grid", types::energy::NodeType::Generic);
    grid.schedule_import = {make_entry(ts, 63.0f, 0.0f)};
    grid.schedule_export = {make_entry(ts, 0.0f, 0.0f)};
    grid.children = {cp01};

    EnergyManagerImpl impl(make_aggregation_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    impl.run_optimizer(grid, at);
    impl.run_optimizer(grid, at);

    // Two runs over one meter must still report one meter, not two.
    EXPECT_FLOAT_EQ(impl.get_leaf_aggregate().power_W, 1000.0f);
    EXPECT_EQ(impl.get_leaf_aggregate().fresh_meters, 1);
}

} // namespace module
