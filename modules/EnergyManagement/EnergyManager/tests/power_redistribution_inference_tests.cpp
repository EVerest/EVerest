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

// The limits the inference reads come from the Market, not from the request, so the tests
// have to build one the way run_optimizer does: globals initialised for the run, then a
// Market over the tree. It owns its request because Market keeps a reference to it.
class MarketFixture {
public:
    MarketFixture(types::energy::EnergyFlowRequest tree, date::utc_clock::time_point at,
                  int schedule_total_duration_h = 1) :
        request(std::move(tree)) {
        globals.init(at, 60, schedule_total_duration_h, 0.5f, 500.0f, false, request);
        market = std::make_unique<Market>(request, U);
    }

    const Market& root() const {
        return *market;
    }

    const Market& evse(const std::string& uuid) const {
        for (auto* m : market->get_list_of_evses()) {
            if (m->energy_flow_request.uuid == uuid) {
                return *m;
            }
        }
        throw std::runtime_error("no such evse in fixture: " + uuid);
    }

private:
    types::energy::EnergyFlowRequest request;
    std::unique_ptr<Market> market;
};

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

// One connector drawing 20 A per phase (13800 W) behind a grid connection that allows
// 30 kW, so the site has headroom and the connector consumes what it is allotted: the
// Saturated case an increase is meant for. Runs the optimizer once per second up to and
// including \p until_s and returns the current enforced at that last run.
//
// With \p with_root_meter the grid connection measures the site itself; without it the site
// figure can only be the sum of the EVSE meters, which happens to be the same number here
// because this site has no load besides the connector.
constexpr float MEASURED_W = 13800.0f;
constexpr float MEASURED_A = 20.0f;
constexpr float CAP_A = MEASURED_A + 2.0f;

float run_drawing_connector(EnergyManagerImpl& impl, bool with_root_meter, int until_s, float evse_max_A = 32.0f) {
    auto evse = test::make_evse_node("evse1", evse_max_A, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, 30000.0f, {evse});

    float current_A = 0.0f;
    for (int t = 0; t <= until_s; t++) {
        test::set_measurement(request.children[0], MEASURED_W, fresh_at(t));
        if (with_root_meter) {
            test::set_root_measurement(request, MEASURED_W, fresh_at(t));
        }
        const auto limits = impl.run_optimizer(request, T0 + std::chrono::seconds(t));
        const auto limit = test::find_limit(limits, "evse1");
        current_A = limit.has_value() ? limit.value().limits_root_side.ac_max_current_A.value().value : 0.0f;
    }
    return current_A;
}

} // namespace

// ---------------------------------------------------------------- limit helpers

TEST(RedistributionHelpers, GridLimitFromCurrentAndPhases) {
    const MarketFixture f(test::make_root_node("grid", 32.0f, std::nullopt, {}), T0);
    const auto limit = get_grid_limit_W(f.root(), U);
    ASSERT_TRUE(limit.has_value());
    EXPECT_FLOAT_EQ(limit.value(), 32.0f * 3 * U);
}

TEST(RedistributionHelpers, GridLimitPrefersTotalPower) {
    const MarketFixture f(test::make_root_node("grid", 32.0f, 11000.0f, {}), T0);
    const auto limit = get_grid_limit_W(f.root(), U);
    ASSERT_TRUE(limit.has_value());
    EXPECT_FLOAT_EQ(limit.value(), 11000.0f);
}

TEST(RedistributionHelpers, NoScheduleMeansNothingIsAvailable) {
    // A node that requests nothing gets Market's zero schedule, so the limit is a known
    // zero rather than an unknown. Either way the inference claims no headroom.
    types::energy::EnergyFlowRequest root;
    root.uuid = "grid";
    root.node_type = types::energy::NodeType::Generic;
    const MarketFixture f(root, T0);
    const auto limit = get_grid_limit_W(f.root(), U);
    ASSERT_TRUE(limit.has_value());
    EXPECT_FLOAT_EQ(limit.value(), 0.0f);
}

TEST(RedistributionHelpers, GridLimitUsesTheSlotInForceNotTheFirstOne) {
    // A multi-slot schedule is what an external limit looks like: an OCPP charging profile
    // or any DLM input produces one. Reading slot 0 would report the 32 A that applied at
    // 11:00 while the schedule in force at 12:30 allows 10 A - a 3.2x overstatement of the
    // grid limit, in the direction that fabricates headroom.
    auto root = test::make_root_node("grid", 32.0f, std::nullopt, {}, "2026-08-04T11:00:00.000Z");
    root.schedule_import.push_back(test::make_schedule_entry("2026-08-04T12:00:00.000Z", 10.0f, 0.0f));

    const MarketFixture f(root, T0, 3);
    const auto limit = get_grid_limit_W(f.root(), U);
    ASSERT_TRUE(limit.has_value());
    EXPECT_FLOAT_EQ(limit.value(), 10.0f * 3 * U);
}

TEST(RedistributionHelpers, GridLimitHonoursALimitExpressedOnTheLeavesSide) {
    // get_max_available_energy() takes the minimum of the two sides. A limit that only
    // exists on the leaves side is invisible in limits_to_root, so reading the request
    // directly would report 32 A where only 10 A may actually be drawn.
    auto root = test::make_root_node("grid", 32.0f, std::nullopt, {});
    root.schedule_import[0].limits_to_leaves.ac_max_current_A = {10.0f, "TEST_external"};
    root.schedule_import[0].limits_to_leaves.ac_max_phase_count = {3, "TEST_external"};

    const MarketFixture f(root, T0);
    const auto limit = get_grid_limit_W(f.root(), U);
    ASSERT_TRUE(limit.has_value());
    EXPECT_FLOAT_EQ(limit.value(), 10.0f * 3 * U);
}

TEST(RedistributionHelpers, AllocatedPowerFromAmpere) {
    const auto limit = make_enforced_limit("evse1", 16.0f, 3, std::nullopt);
    const auto allocated = get_allocated_power_W(limit, U);
    ASSERT_TRUE(allocated.has_value());
    EXPECT_FLOAT_EQ(allocated.value(), 16.0f * 3 * U);
}

TEST(RedistributionHelpers, AllocatedPowerAssumesOnePhaseWhenUndeclared) {
    // An undeclared phase count is missing information about a limit, and the safe reading
    // of that is the smaller limit. EnergyNode always declares it, so this only covers a
    // node that does not.
    const auto limit = make_enforced_limit("evse1", 16.0f, std::nullopt, std::nullopt);
    const auto allocated = get_allocated_power_W(limit, U);
    ASSERT_TRUE(allocated.has_value());
    EXPECT_FLOAT_EQ(allocated.value(), 16.0f * 1 * U);
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
    const MarketFixture f(
        test::make_root_node("grid", 32.0f, std::nullopt, {test::make_evse_node("evse1", 32.0f, 6.0f)}), T0);
    const auto b = get_static_bounds_W(f.evse("evse1"), U);
    ASSERT_TRUE(b.min_W.has_value());
    ASSERT_TRUE(b.max_W.has_value());
    EXPECT_FLOAT_EQ(b.min_W.value(), 6.0f * 1 * U);
    EXPECT_FLOAT_EQ(b.max_W.value(), 32.0f * 3 * U);
}

TEST(RedistributionHelpers, StaticBoundsPreferTotalPowerForMaximum) {
    const MarketFixture f(
        test::make_root_node("grid", 32.0f, std::nullopt, {test::make_evse_node("evse1", 32.0f, 6.0f, 11000.0f)}), T0);
    const auto b = get_static_bounds_W(f.evse("evse1"), U);
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

TEST(RedistributionClassify, ExportingConnectorMakesNoImportClaim) {
    // Negative is export. This inference only ever looks at schedule_import, so a
    // discharging connector has no import consumption to compare against its import
    // allocation. Clamping to zero instead would read as "using none of its allocation" and
    // offer the whole allocation up for redistribution, which is not what a V2G session is
    // doing.
    const auto c = classify_connector(11040.0f, -500.0f, bounds(1380.0f, 22080.0f), MARGIN);
    EXPECT_EQ(c.connector_class, ConnectorClass::Unknown);
    EXPECT_FLOAT_EQ(c.reducible_W, 0.0f);
}

TEST(RedistributionClassify, AConnectorDrawingNothingIsStillUnderConsuming) {
    // Zero is a measurement, not an absence: a plugged-in car that has stopped drawing can
    // genuinely give its allocation back, down to the minimum purchase.
    const auto c = classify_connector(11040.0f, 0.0f, bounds(1380.0f, 22080.0f), MARGIN);
    EXPECT_EQ(c.connector_class, ConnectorClass::UnderConsuming);
    EXPECT_NEAR(c.reducible_W, 11040.0f - 1380.0f, 0.5f);
}

// ---------------------------------------------------------------- site inference

TEST(RedistributionSite, NoClaimWithoutGridLimit) {
    const auto s =
        infer_site(std::nullopt, make_aggregate(5000.0f, 1, 0), {{"evse1", 11040.0f, 22080.0f}}, MARGIN, GAIN);
    EXPECT_FALSE(s.headroom_W.has_value());
    EXPECT_FLOAT_EQ(s.increase_W, 0.0f);
}

TEST(RedistributionSite, NoClaimWithoutFreshAggregate) {
    const auto s =
        infer_site(22080.0f, make_aggregate(std::nullopt, 0, 0), {{"evse1", 11040.0f, 22080.0f}}, MARGIN, GAIN);
    EXPECT_FALSE(s.measured_W.has_value());
    EXPECT_FALSE(s.headroom_W.has_value());
    EXPECT_FLOAT_EQ(s.increase_W, 0.0f);
}

TEST(RedistributionSite, PartiallyStaleAggregateMakesNoClaim) {
    const auto s = infer_site(22080.0f, make_aggregate(5000.0f, 1, 1), {{"evse1", 11040.0f, 22080.0f}}, MARGIN, GAIN);
    EXPECT_FALSE(s.measured_W.has_value());
    EXPECT_FLOAT_EQ(s.increase_W, 0.0f);
}

TEST(RedistributionSite, HeadroomWithinDeadbandGivesNoIncrease) {
    // 2000 W headroom on a 22080 W grid is below the 2208 W deadband.
    const auto s = infer_site(22080.0f, make_aggregate(20080.0f, 1, 0), {{"evse1", 11040.0f, 22080.0f}}, MARGIN, GAIN);
    ASSERT_TRUE(s.headroom_W.has_value());
    EXPECT_FLOAT_EQ(s.headroom_W.value(), 2000.0f);
    EXPECT_FLOAT_EQ(s.increase_W, 0.0f);
}

TEST(RedistributionSite, IncreaseIsProportionalToHeadroomBeyondDeadband) {
    // Headroom 12080 W, deadband 2208 W: gain 0.5 x 9872 W = 4936 W for the one connector.
    const auto s = infer_site(22080.0f, make_aggregate(10000.0f, 1, 0), {{"evse1", 11040.0f, 22080.0f}}, MARGIN, GAIN);
    EXPECT_EQ(s.saturated_connectors, 1);
    EXPECT_NEAR(s.increase_W, 0.5f * (12080.0f - 2208.0f), 0.5f);
}

TEST(RedistributionSite, IncreaseShrinksAsTheLimitIsApproached) {
    const auto far =
        infer_site(22080.0f, make_aggregate(10000.0f, 1, 0), {{"evse1", 11040.0f, 22080.0f}}, MARGIN, GAIN);
    const auto near =
        infer_site(22080.0f, make_aggregate(18000.0f, 1, 0), {{"evse1", 11040.0f, 22080.0f}}, MARGIN, GAIN);
    EXPECT_GT(far.increase_W, near.increase_W);
    EXPECT_GT(near.increase_W, 0.0f);
}

TEST(RedistributionSite, IncreaseIsSplitEquallyAndClampedToStaticMaximum) {
    // Two saturated connectors, one with only 500 W of room left.
    const std::vector<SaturatedConnector> saturated = {{"roomy", 11040.0f, 22080.0f}, {"tight", 11040.0f, 11540.0f}};
    const auto s = infer_site(44160.0f, make_aggregate(20000.0f, 2, 0), saturated, MARGIN, GAIN);
    // headroom 24160, deadband 4416, gain 0.5 -> 9872 total, 4936 per connector.
    EXPECT_EQ(s.saturated_connectors, 2);
    EXPECT_NEAR(s.increase_W, 4936.0f + 500.0f, 0.5f);
    // The site total is the sum of what the individual connectors may take, which is what a
    // broker acts on: the one with 500 W of room gets 500 W, not its equal share.
    EXPECT_NEAR(s.increase_W_by_connector.at("roomy"), 4936.0f, 0.5f);
    EXPECT_NEAR(s.increase_W_by_connector.at("tight"), 500.0f, 0.5f);
}

TEST(RedistributionSite, AConnectorWithNoRoomGetsNoShareButStillDilutesTheSplit) {
    const std::vector<SaturatedConnector> saturated = {{"roomy", 11040.0f, 22080.0f}, {"full", 11040.0f, 11040.0f}};
    const auto s = infer_site(44160.0f, make_aggregate(20000.0f, 2, 0), saturated, MARGIN, GAIN);
    EXPECT_EQ(s.saturated_connectors, 2);
    // Still halved, because the second connector was saturated and had to be offered a
    // share; it simply could not use it. A grant of 0 W is not recorded: an entry means
    // "you may draw more".
    EXPECT_NEAR(s.increase_W_by_connector.at("roomy"), 4936.0f, 0.5f);
    EXPECT_EQ(s.increase_W_by_connector.count("full"), 0u);
    EXPECT_NEAR(s.increase_W, 4936.0f, 0.5f);
}

TEST(RedistributionSite, ConnectorWithoutAStaticMaximumIsNotACandidate) {
    // There is nothing to clamp an increase against, so it must neither receive a share nor
    // dilute anybody else's.
    ConnectorInference connector;
    connector.connector_class = ConnectorClass::Saturated;
    connector.allocated_W = 11040.0f;

    StaticBoundsW unbounded;
    EXPECT_FALSE(to_saturated_connector("evse1", connector, unbounded).has_value());

    StaticBoundsW bounded;
    bounded.max_W = 22080.0f;
    const auto candidate = to_saturated_connector("evse1", connector, bounded);
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate.value().uuid, "evse1");
    EXPECT_FLOAT_EQ(candidate.value().allocated_W, 11040.0f);
    EXPECT_FLOAT_EQ(candidate.value().max_W, 22080.0f);
}

TEST(RedistributionSite, ConnectorWithoutAnAllocationIsNotACandidate) {
    ConnectorInference connector;
    connector.connector_class = ConnectorClass::Saturated;

    StaticBoundsW bounded;
    bounded.max_W = 22080.0f;
    EXPECT_FALSE(to_saturated_connector("evse1", connector, bounded).has_value());
}

TEST(RedistributionSite, NoIncreaseWithoutSaturatedConnectors) {
    const auto s = infer_site(22080.0f, make_aggregate(10000.0f, 1, 0), {}, MARGIN, GAIN);
    ASSERT_TRUE(s.headroom_W.has_value());
    EXPECT_FLOAT_EQ(s.increase_W, 0.0f);
}

TEST(RedistributionSite, ZeroGainDisablesIncrease) {
    const auto s = infer_site(22080.0f, make_aggregate(10000.0f, 1, 0), {{"evse1", 11040.0f, 22080.0f}}, MARGIN, 0.0f);
    EXPECT_FLOAT_EQ(s.increase_W, 0.0f);
}

// ---------------------------------------------------------------- per session context

TEST(RedistributionContext, ClearResetsInferenceState) {
    BrokerContext context;
    context.last_allocated_W = 11040.0f;
    context.under_consuming.update(true, T0, std::chrono::seconds(0));
    ASSERT_TRUE(context.under_consuming.held());

    context.clear();

    EXPECT_FALSE(context.last_allocated_W.has_value());
    EXPECT_FALSE(context.under_consuming.held());
    // The stretch is forgotten, not released: a session that ended has no condition left
    // to have stopped holding, so the next session starts silent.
    EXPECT_EQ(context.under_consuming.update(false, T0, std::chrono::seconds(0)), HoldLatch::Edge::None);
}

// ---------------------------------------------------------------- hold latch

TEST(HoldLatchTest, ReportsNothingBeforeTheHoldTimeIsUp) {
    HoldLatch latch;

    EXPECT_EQ(latch.update(true, T0, std::chrono::seconds(10)), HoldLatch::Edge::None);
    EXPECT_FALSE(latch.held());
    EXPECT_EQ(latch.update(true, T0 + std::chrono::seconds(9), std::chrono::seconds(10)), HoldLatch::Edge::None);
    EXPECT_FALSE(latch.held());
}

TEST(HoldLatchTest, ReportsHeldOnceTheHoldTimeIsUpAndThenStaysQuiet) {
    HoldLatch latch;
    latch.update(true, T0, std::chrono::seconds(10));

    EXPECT_EQ(latch.update(true, T0 + std::chrono::seconds(10), std::chrono::seconds(10)), HoldLatch::Edge::Held);
    EXPECT_TRUE(latch.held());
    // Once per stretch, not once per optimizer run.
    EXPECT_EQ(latch.update(true, T0 + std::chrono::seconds(11), std::chrono::seconds(10)), HoldLatch::Edge::None);
    EXPECT_EQ(latch.update(true, T0 + std::chrono::seconds(60), std::chrono::seconds(10)), HoldLatch::Edge::None);
}

TEST(HoldLatchTest, ReportsReleaseOnceAndOnlyAfterAReport) {
    HoldLatch latch;
    latch.update(true, T0, std::chrono::seconds(10));
    ASSERT_EQ(latch.update(true, T0 + std::chrono::seconds(10), std::chrono::seconds(10)), HoldLatch::Edge::Held);

    EXPECT_EQ(latch.update(false, T0 + std::chrono::seconds(11), std::chrono::seconds(10)), HoldLatch::Edge::Released);
    EXPECT_FALSE(latch.held());
    EXPECT_EQ(latch.update(false, T0 + std::chrono::seconds(12), std::chrono::seconds(10)), HoldLatch::Edge::None);
}

TEST(HoldLatchTest, AnInterruptedStretchNeverReportsAndStartsOver) {
    HoldLatch latch;
    latch.update(true, T0, std::chrono::seconds(10));

    // Nothing was reported, so dropping the condition has nothing to release either.
    EXPECT_EQ(latch.update(false, T0 + std::chrono::seconds(5), std::chrono::seconds(10)), HoldLatch::Edge::None);

    // And the clock restarts rather than crediting the first five seconds.
    latch.update(true, T0 + std::chrono::seconds(6), std::chrono::seconds(10));
    EXPECT_EQ(latch.update(true, T0 + std::chrono::seconds(11), std::chrono::seconds(10)), HoldLatch::Edge::None);
    EXPECT_EQ(latch.update(true, T0 + std::chrono::seconds(16), std::chrono::seconds(10)), HoldLatch::Edge::Held);
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
    // The allocation the inference compares against is the capped one, not the 32 A fuse
    // limit: the first drawing run of a session starts the connector at its minimum current
    // plus the margin (6 A + 2 A), and the reduction hold keeps it there for the second.
    EXPECT_FLOAT_EQ(c.allocated_W.value(), 8.0f * 3 * U);
    ASSERT_TRUE(c.measured_W.has_value());
    EXPECT_FLOAT_EQ(c.measured_W.value(), 4000.0f);
    EXPECT_EQ(c.connector_class, ConnectorClass::UnderConsuming);
    EXPECT_FALSE(c.held);
}

TEST(RedistributionIntegration, ReduceCandidateIsHeldOnlyAfterHoldTime) {
    // The meter has to keep reporting for the condition to keep holding: a reading is only
    // usable while it is fresh, so a live under-consuming connector refreshes its timestamp
    // on every run just as a real one does.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_redistribution_config(10), [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto run_at = [&](int seconds) {
        test::set_measurement(request.children[0], 4000.0f, fresh_at(seconds));
        impl.run_optimizer(request, T0 + std::chrono::seconds(seconds));
    };

    run_at(0);
    run_at(1); // condition starts here
    EXPECT_FALSE(impl.get_redistribution_inference().connectors.at("evse1").held);

    run_at(6);
    EXPECT_FALSE(impl.get_redistribution_inference().connectors.at("evse1").held);

    run_at(11);
    EXPECT_TRUE(impl.get_redistribution_inference().connectors.at("evse1").held);
}

TEST(RedistributionIntegration, TransientCaughtUpRestartsTheHold) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_redistribution_config(10), [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto run_at = [&](int seconds, float measured_W) {
        test::set_measurement(request.children[0], measured_W, fresh_at(seconds));
        impl.run_optimizer(request, T0 + std::chrono::seconds(seconds));
    };

    run_at(0, 4000.0f);
    run_at(1, 4000.0f);
    run_at(6, 4000.0f);

    // EV briefly draws everything it was allotted. The cap holds the allocation below the
    // connector's static maximum, so catching up reads as Saturated rather than AtMaximum;
    // what the hold turns on is that the connector is no longer under-consuming.
    run_at(7, 22000.0f);
    EXPECT_EQ(impl.get_redistribution_inference().connectors.at("evse1").connector_class, ConnectorClass::Saturated);

    run_at(8, 4000.0f);
    run_at(12, 4000.0f);
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

// ---------------------------------------------------------------- stale measurements

// The connector path and the site path look at the same meters and must agree about which
// of them are alive. Absence already failed safe; staleness is the realistic failure,
// because EnergyNode and EvseManager republish the last reading they received in every
// request - a dead meter looks exactly like a live one holding steady.

TEST(RedistributionIntegration, StaleConnectorMeasurementMakesNoClaim) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    // The meter reports 4 kW and then stops updating its timestamp.
    test::set_measurement(request.children[0], 4000.0f, FRESH);

    EnergyManagerImpl impl(make_redistribution_config(0), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);

    // Still fresh one second in: the connector is genuinely under-consuming.
    impl.run_optimizer(request, T0 + std::chrono::seconds(1));
    {
        const auto c = impl.get_redistribution_inference().connectors.at("evse1");
        EXPECT_EQ(c.connector_class, ConnectorClass::UnderConsuming);
        EXPECT_GT(c.reducible_W, 0.0f);
        EXPECT_TRUE(c.held);
    }

    // Five minutes later the tree is byte-for-byte identical and the reading is five
    // minutes old. Nothing may be concluded from it.
    impl.run_optimizer(request, T0 + std::chrono::seconds(300));
    {
        const auto c = impl.get_redistribution_inference().connectors.at("evse1");
        EXPECT_EQ(c.connector_class, ConnectorClass::Unknown);
        EXPECT_FLOAT_EQ(c.reducible_W, 0.0f);
        EXPECT_FALSE(c.held);
    }
}

TEST(RedistributionIntegration, ConnectorAndSiteAgreeAboutAStaleMeter) {
    // The site already refused to claim on a stale meter while the connector did not. Both
    // now go through the same rule, so neither makes a claim.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4000.0f, FRESH);

    EnergyManagerImpl impl(make_redistribution_config(0), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(300));

    const auto inference = impl.get_redistribution_inference();
    EXPECT_FALSE(inference.site.measured_W.has_value());
    EXPECT_FALSE(inference.site.headroom_W.has_value());
    EXPECT_FLOAT_EQ(inference.site.increase_W, 0.0f);
    EXPECT_EQ(inference.connectors.at("evse1").connector_class, ConnectorClass::Unknown);
}

TEST(RedistributionIntegration, AMeterWithNoUsableTimestampMakesNoClaim) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4000.0f, "not a timestamp");

    EnergyManagerImpl impl(make_redistribution_config(0), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(1));

    EXPECT_EQ(impl.get_redistribution_inference().connectors.at("evse1").connector_class, ConnectorClass::Unknown);
}

// ---------------------------------------------------------------- what measures the site

TEST(RedistributionIntegration, SitePrefersTheGridConnectionsOwnMeter) {
    // 34 kW through the connection, of which the single EVSE draws 4 kW - the other 30 kW
    // is building load that no EVSE meter can see. Summing the EVSE meters would report
    // 4 kW of site consumption and 30 kW of headroom that is already spent.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 100.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4000.0f, FRESH);
    test::set_root_measurement(request, 34000.0f, FRESH);

    EnergyManagerImpl impl(make_redistribution_config(0), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(1));

    const auto site = impl.get_redistribution_inference().site;
    EXPECT_EQ(site.meter_source, SiteMeterSource::RootMeter);
    ASSERT_TRUE(site.measured_W.has_value());
    EXPECT_FLOAT_EQ(site.measured_W.value(), 34000.0f);
    ASSERT_TRUE(site.grid_limit_W.has_value());
    EXPECT_FLOAT_EQ(site.grid_limit_W.value(), 100.0f * 3 * U);
    ASSERT_TRUE(site.headroom_W.has_value());
    EXPECT_FLOAT_EQ(site.headroom_W.value(), 100.0f * 3 * U - 34000.0f);
}

TEST(RedistributionIntegration, SiteFallsBackToTheEvseSumWithoutARootMeter) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 100.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4000.0f, FRESH);

    EnergyManagerImpl impl(make_redistribution_config(0), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(1));

    const auto site = impl.get_redistribution_inference().site;
    EXPECT_EQ(site.meter_source, SiteMeterSource::LeafSum);
    ASSERT_TRUE(site.measured_W.has_value());
    EXPECT_FLOAT_EQ(site.measured_W.value(), 4000.0f);
}

TEST(RedistributionIntegration, StaleRootMeterMakesNoSiteClaim) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 100.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4000.0f, FRESH);
    test::set_root_measurement(request, 34000.0f, FRESH);

    EnergyManagerImpl impl(make_redistribution_config(0), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0 + std::chrono::seconds(300));

    const auto site = impl.get_redistribution_inference().site;
    EXPECT_EQ(site.meter_source, SiteMeterSource::RootMeter);
    EXPECT_FALSE(site.measured_W.has_value());
    EXPECT_FLOAT_EQ(site.increase_W, 0.0f);
}

// ---------------------------------------------------------------- never above the fuse

TEST(RedistributionIntegration, NoIncreaseAtTheFuseLimitWithHouseLoad) {
    // The test asked for on PR 2628 and deferred as "measures only": a tree at its fuse
    // limit must propose no increase. All three inputs are the ones that used to be read
    // wrong - a multi-slot schedule that tightens, a limit on the leaves side, and a site
    // meter that sees a load no EVSE reports.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f, std::nullopt, "2026-08-04T11:00:00.000Z");
    auto request = test::make_root_node("grid", 100.0f, std::nullopt, {evse}, "2026-08-04T11:00:00.000Z");
    // From 12:00 an external limit tightens the connection to 32 A, expressed leaves side.
    auto tightened = test::make_schedule_entry("2026-08-04T12:00:00.000Z", 100.0f, 0.0f);
    tightened.limits_to_leaves.ac_max_current_A = {32.0f, "TEST_external"};
    tightened.limits_to_leaves.ac_max_phase_count = {3, "TEST_external"};
    request.schedule_import.push_back(tightened);

    // The connection is drawing its full 32 A: 4 kW of EVSE and the rest house load.
    test::set_measurement(request.children[0], 4000.0f, FRESH);
    test::set_root_measurement(request, 32.0f * 3 * U, FRESH);

    auto config = make_redistribution_config(0);
    config.schedule_total_duration = 3;
    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, T0);
    impl.run_optimizer(request, T0 + std::chrono::seconds(1));

    const auto site = impl.get_redistribution_inference().site;
    ASSERT_TRUE(site.grid_limit_W.has_value());
    EXPECT_FLOAT_EQ(site.grid_limit_W.value(), 32.0f * 3 * U);
    ASSERT_TRUE(site.headroom_W.has_value());
    EXPECT_NEAR(site.headroom_W.value(), 0.0f, 1.0f);
    EXPECT_FLOAT_EQ(site.increase_W, 0.0f);
}

TEST(RedistributionIntegration, TheReduceSideDoesNotAffectAllocations) {
    // The increase side of the inference moves allocations by design (see the
    // SiteDistribution tests). The reduce side does not, and must not: lowering a
    // connector is the measurement based limit's job, and this classification only reports
    // on it. Neither connector here is saturated, so nothing is ever granted, and two
    // brokers that differ only in the hold time must allocate identically.
    auto make_request = []() {
        auto evse1 = test::make_evse_node("evse1", 32.0f, 6.0f);
        auto evse2 = test::make_evse_node("evse2", 32.0f, 6.0f);
        auto request = test::make_root_node("grid", 40.0f, 30000.0f, {evse1, evse2});
        // Both draw far less than the measurement based limit allows, so neither is ever
        // saturated and the site has nobody to hand its headroom to.
        test::set_measurement(request.children[0], 3000.0f, FRESH);
        test::set_measurement(request.children[1], 4000.0f, FRESH);
        return request;
    };
    const auto request = make_request();

    EnergyManagerImpl reporting_at_once(make_redistribution_config(0),
                                        [](const std::vector<types::energy::EnforcedLimits>&) {});
    EnergyManagerImpl reporting_after_hold(make_redistribution_config(10),
                                           [](const std::vector<types::energy::EnforcedLimits>&) {});

    for (int run = 0; run < 3; run++) {
        const auto at = T0 + std::chrono::seconds(run);
        const auto a = reporting_at_once.run_optimizer(request, at);
        const auto b = reporting_after_hold.run_optimizer(request, at);
        for (const auto* uuid : {"evse1", "evse2"}) {
            const auto la = test::find_limit(a, uuid);
            const auto lb = test::find_limit(b, uuid);
            ASSERT_TRUE(la.has_value());
            ASSERT_TRUE(lb.has_value());
            EXPECT_FLOAT_EQ(la.value().limits_root_side.ac_max_current_A.value().value,
                            lb.value().limits_root_side.ac_max_current_A.value().value);
        }
    }
    // The two disagree about what to report, and still allocated the same.
    EXPECT_EQ(reporting_at_once.get_redistribution_inference().connectors.at("evse1").connector_class,
              ConnectorClass::UnderConsuming);
    EXPECT_TRUE(reporting_at_once.get_redistribution_inference().connectors.at("evse1").held);
    EXPECT_FALSE(reporting_after_hold.get_redistribution_inference().connectors.at("evse1").held);
}

// ---------------------------------------------------------------- handing out the headroom

// The aggregated site measurement reaching an allocation, which is the one thing the
// inference above does not do by itself.

TEST(SiteDistribution, TheGrantReachesTheConnectorOnceTheHeadroomHasHeld) {
    EnergyManagerImpl before(make_redistribution_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    EXPECT_NEAR(run_drawing_connector(before, true, 10), CAP_A, 0.01f);

    // The grant is written by the run that sees the hold expire and acted on by the next:
    // the inference needs this run's allocations, which only exist once trading is over.
    EnergyManagerImpl after(make_redistribution_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto distributed = run_drawing_connector(after, true, 12);
    EXPECT_GT(distributed, CAP_A + 2.0f);
    EXPECT_LE(distributed, 32.0f);
}

TEST(SiteDistribution, ALeafSumIsHandedOutTheSameWay) {
    // No grid meter: the site figure is the sum of the EVSE meters. It covers less of the
    // site, which the log says, but the grid limit of the tree bounds the result either way.
    EnergyManagerImpl impl(make_redistribution_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    EXPECT_GT(run_drawing_connector(impl, false, 12), CAP_A + 2.0f);
}

TEST(SiteDistribution, AGrantNeverLiftsAConnectorAboveItsOwnMaximum) {
    // The connector may draw 24 A; the site has far more than 2 A of headroom to offer it.
    EnergyManagerImpl impl(make_redistribution_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    EXPECT_NEAR(run_drawing_connector(impl, true, 12, 24.0f), 24.0f, 0.01f);
}

TEST(SiteDistribution, ZeroGainHandsOutNothing) {
    // The way to keep the measurement based limit without the site ever relaxing it.
    auto config = make_redistribution_config();
    config.power_redistribution_gain = 0.0;
    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    EXPECT_NEAR(run_drawing_connector(impl, true, 20), CAP_A, 0.01f);
}

TEST(SiteDistribution, AGrantNeverExceedsWhatFastChargingWouldHaveAllocated) {
    // The invariant that survives the inference acting on allocations: a grant only relaxes
    // the broker's own cap back towards the offer, so it can never reach past the limits
    // the market derived from the tree. Whatever the site infers, the fuse still decides.
    EnergyManagerImpl redistributing(make_redistribution_config(),
                                     [](const std::vector<types::energy::EnforcedLimits>&) {});
    EnergyManagerImpl fast_charging(test::make_default_config(),
                                    [](const std::vector<types::energy::EnforcedLimits>&) {});

    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, 30000.0f, {evse});
    for (int t = 0; t <= 20; t++) {
        test::set_measurement(request.children[0], MEASURED_W, fresh_at(t));
        test::set_root_measurement(request, MEASURED_W, fresh_at(t));
        const auto at = T0 + std::chrono::seconds(t);
        const auto a = redistributing.run_optimizer(request, at);
        const auto b = fast_charging.run_optimizer(request, at);
        const auto redistributed = test::find_limit(a, "evse1");
        const auto unrestricted = test::find_limit(b, "evse1");
        ASSERT_TRUE(redistributed.has_value());
        ASSERT_TRUE(unrestricted.has_value());
        EXPECT_LE(redistributed.value().limits_root_side.ac_max_current_A.value().value,
                  unrestricted.value().limits_root_side.ac_max_current_A.value().value + 0.01f);
    }
}

} // namespace module
