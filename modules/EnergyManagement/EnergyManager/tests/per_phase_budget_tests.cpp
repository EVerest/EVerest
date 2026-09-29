// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <memory>

#include <gtest/gtest.h>

#include <utils/date.hpp>

#include "BrokerPowerRedistribution.hpp"
#include "EnergyManagerTestHelpers.hpp"
#include "Market.hpp"

namespace module {

namespace {

constexpr float U = 230.0f;
const auto AT = Everest::Date::from_rfc3339(test::NOW_TS);

// A Market over a tree, built the way run_optimizer does. It owns its request because
// Market keeps a reference to it.
class MarketFixture {
public:
    explicit MarketFixture(types::energy::EnergyFlowRequest tree) : request(std::move(tree)) {
        globals.init(AT, 60, 1, 0.5f, 500.0f, false, request);
        market = std::make_unique<Market>(request, U);
    }

    Market& root() {
        return *market;
    }

    Market& evse(const std::string& uuid) {
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

// One slot's trade of \p ampere, declared on three phases, with the watt figure a broker
// sends along when the path has a watt limit.
ScheduleRes trade_of(float ampere, std::optional<float> watt = std::nullopt) {
    auto traded = globals.empty_schedule_res;
    for (auto& entry : traded) {
        entry.limits_to_root.ac_max_current_A = {ampere, "TEST"};
        entry.limits_to_root.ac_max_phase_count = {3, "TEST"};
        if (watt.has_value()) {
            entry.limits_to_root.total_power_W = {watt.value(), "TEST"};
        }
    }
    return traded;
}

float available_A(Market& market, const PhaseSet& phases) {
    return market.get_available_energy_import(phases)[0].limits_to_root.ac_max_current_A.value().value;
}

float available_W(Market& market, const PhaseSet& phases = ALL_GRID_PHASES) {
    return market.get_available_energy_import(phases)[0].limits_to_root.total_power_W.value().value;
}

types::energy::EnergyFlowRequest two_evse_tree(float root_A, std::optional<float> root_W = std::nullopt) {
    return test::make_root_node("grid", root_A, root_W,
                                {test::make_evse_node("cp1", 32.0f, 6.0f), test::make_evse_node("cp2", 32.0f, 6.0f)});
}

} // namespace

// ---------------------------------------------------------------- market bookkeeping

TEST(PerPhaseBudget, OnePhaseTradeLeavesTheOtherPhasesFree) {
    MarketFixture f(two_evse_tree(32.0f));

    f.evse("cp1").trade(trade_of(16.0f), {Phase::L1});

    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L1}), 16.0f);
    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L2}), 32.0f);
    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L2, Phase::L3}), 32.0f);
    // A connector on all three phases is bound by the most loaded one.
    EXPECT_FLOAT_EQ(available_A(f.root(), ALL_GRID_PHASES), 16.0f);
}

TEST(PerPhaseBudget, TradesOnDifferentPhasesDoNotAddUp) {
    MarketFixture f(two_evse_tree(32.0f));

    f.evse("cp1").trade(trade_of(16.0f), {Phase::L1});
    f.evse("cp2").trade(trade_of(16.0f), {Phase::L2});

    // 16 A on L1 and 16 A on L2: a three phase EV still has 16 A, not 0.
    EXPECT_FLOAT_EQ(available_A(f.root(), ALL_GRID_PHASES), 16.0f);
    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L3}), 32.0f);
}

TEST(PerPhaseBudget, AllPhaseTradesBehaveAsBefore) {
    MarketFixture f(two_evse_tree(32.0f));

    f.evse("cp1").trade(trade_of(10.0f));
    f.evse("cp2").trade(trade_of(10.0f));

    EXPECT_FLOAT_EQ(available_A(f.root(), ALL_GRID_PHASES), 12.0f);
    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L1}), 12.0f);
}

TEST(PerPhaseBudget, WattsCountOnTheDrawnPhasesAboveTheConnector) {
    // 16 A declared on three phases is sent as 11040 W, the figure the connector converts
    // back with its own phase count. A single phase EV draws a third of it.
    MarketFixture f(two_evse_tree(100.0f, 22080.0f));

    f.evse("cp1").trade(trade_of(16.0f, 16.0f * 3.0f * U), {Phase::L1});

    EXPECT_FLOAT_EQ(available_W(f.root()), 22080.0f - 16.0f * U);
    EXPECT_FLOAT_EQ(f.evse("cp1").get_sold_energy()[0].limits_to_root.total_power_W.value().value, 16.0f * 3.0f * U);
}

TEST(PerPhaseBudget, WattsCountOnTheDrawnPhasesAtTheConnectorToo) {
    auto tree = test::make_root_node(
        "grid", 100.0f, std::nullopt,
        {test::make_evse_node("cp1", 32.0f, 6.0f, 11040.0f), test::make_evse_node("cp2", 32.0f, 6.0f)});
    MarketFixture f(tree);

    f.evse("cp1").trade(trade_of(16.0f, 16.0f * 3.0f * U), {Phase::L1});

    EXPECT_FLOAT_EQ(available_W(f.evse("cp1"), {Phase::L1}), 11040.0f - 16.0f * U);
    EXPECT_FLOAT_EQ(f.evse("cp1").get_sold_energy()[0].limits_to_root.total_power_W.value().value, 16.0f * 3.0f * U);
}

TEST(PerPhaseBudget, UnknownPhasesCountAsAllPhases) {
    MarketFixture f(two_evse_tree(32.0f, 22080.0f));

    f.evse("cp1").trade(trade_of(16.0f, 16.0f * 3.0f * U), {});

    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L2}), 16.0f);
    EXPECT_FLOAT_EQ(available_W(f.root()), 22080.0f - 16.0f * 3.0f * U);
    EXPECT_FLOAT_EQ(available_A(f.evse("cp1"), {}), 16.0f);
}

// ---------------------------------------------------------------- through the optimizer

namespace {

std::string at_plus(int seconds) {
    return Everest::Date::to_rfc3339(AT + std::chrono::seconds(seconds));
}

// cp1 single phase on L1 and cp2 single phase on L2, both at 16 A, cp3 three phase at
// 16 A, under a 32 A fuse: every phase carries exactly 32 A.
types::energy::EnergyFlowRequest mixed_tree(const std::string& timestamp, std::optional<float> root_W = std::nullopt) {
    auto cp1 = test::make_evse_node("cp1", 16.0f, 6.0f);
    auto cp2 = test::make_evse_node("cp2", 16.0f, 6.0f);
    auto cp3 = test::make_evse_node("cp3", 16.0f, 6.0f);
    test::set_measurement_current(cp1, 16.0f, 0.0f, 0.0f, timestamp);
    test::set_measurement_current(cp2, 0.0f, 16.0f, 0.0f, timestamp);
    test::set_measurement_current(cp3, 16.0f, 16.0f, 16.0f, timestamp);
    return test::make_root_node("grid", 32.0f, root_W, {cp1, cp2, cp3});
}

EnergyManagerConfig make_config() {
    auto c = test::make_redistribution_config();
    c.redistribution_start_with_lower_limit = false;
    return c;
}

float enforced_current(const std::vector<types::energy::EnforcedLimits>& results, const std::string& uuid) {
    const auto limit = test::find_limit(results, uuid);
    if (not limit.has_value() or not limit.value().limits_root_side.ac_max_current_A.has_value()) {
        return -1.0f;
    }
    return limit.value().limits_root_side.ac_max_current_A.value().value;
}

float total_enforced(const std::vector<types::energy::EnforcedLimits>& results) {
    return enforced_current(results, "cp1") + enforced_current(results, "cp2") + enforced_current(results, "cp3");
}

} // namespace

TEST(PerPhaseBudgetLoop, SinglePhaseEvsOnDifferentPhasesLeaveRoomForAThreePhaseOne) {
    EnergyManagerImpl impl(make_config(), [](const auto&) {});

    const auto results = impl.run_optimizer(mixed_tree(at_plus(0)), AT);

    // Counted on one pot the three would have to share 32 A; per phase each gets its 16 A.
    EXPECT_FLOAT_EQ(enforced_current(results, "cp1"), 16.0f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp2"), 16.0f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp3"), 16.0f);
}

TEST(PerPhaseBudgetLoop, FastChargingKeepsOnePot) {
    auto config = make_config();
    config.broker_strategy = "FastCharging";
    EnergyManagerImpl impl(config, [](const auto&) {});

    const auto results = impl.run_optimizer(mixed_tree(at_plus(0)), AT);

    EXPECT_FLOAT_EQ(total_enforced(results), 32.0f);
}

TEST(PerPhaseBudgetLoop, APhaseIsDroppedOnlyAfterTheHold) {
    auto config = make_config();
    config.redistribution_reduction_hold_s = 10;
    EnergyManagerImpl impl(config, [](const auto&) {});

    // The first reading of a single phase draw is not trusted yet: one pot.
    const auto first = impl.run_optimizer(mixed_tree(at_plus(0)), AT);
    EXPECT_FLOAT_EQ(total_enforced(first), 32.0f);

    const auto later = impl.run_optimizer(mixed_tree(at_plus(10)), AT + std::chrono::seconds(10));
    EXPECT_FLOAT_EQ(enforced_current(later, "cp3"), 16.0f);
    EXPECT_FLOAT_EQ(total_enforced(later), 48.0f);
}

TEST(PerPhaseBudgetLoop, AWattLimitCountsASinglePhaseEvOnce) {
    EnergyManagerImpl impl(make_config(), [](const auto&) {});

    // 2 x 16 A x 230 V: exactly what the two single phase EVs draw. Counted on three
    // phases each would take 11040 W and the two would be left 3680 W.
    auto tree = mixed_tree(at_plus(0), 2.0f * 16.0f * U);
    tree.children.pop_back();
    const auto results = impl.run_optimizer(tree, AT);

    EXPECT_FLOAT_EQ(enforced_current(results, "cp1"), 16.0f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp2"), 16.0f);
}

TEST(PerPhaseBudgetLoop, AConnectorsOwnWattLimitCountsASinglePhaseEvOnce) {
    EnergyManagerImpl impl(make_config(), [](const auto&) {});

    // 4600 W is 20 A on one phase. Booked on the three declared phases the connector would
    // stop near 6.7 A.
    auto cp1 = test::make_evse_node("cp1", 32.0f, 6.0f, 4600.0f);
    test::set_measurement_current(cp1, 20.0f, 0.0f, 0.0f, at_plus(0));
    const auto results = impl.run_optimizer(test::make_root_node("grid", 100.0f, std::nullopt, {cp1}), AT);

    EXPECT_NEAR(enforced_current(results, "cp1"), 20.0f, 0.5f);
}

// ---------------------------------------------------------------- limits that differ per phase

namespace {

types::energy::PhaseCurrentsWithSource per_phase(std::optional<float> l1, std::optional<float> l2,
                                                 std::optional<float> l3) {
    types::energy::PhaseCurrentsWithSource limit;
    limit.L1 = l1;
    limit.L2 = l2;
    limit.L3 = l3;
    limit.source = "PER_PHASE";
    return limit;
}

void set_per_phase_limit(types::energy::EnergyFlowRequest& node, const types::energy::PhaseCurrentsWithSource& limit) {
    node.schedule_import[0].limits_to_root.ac_max_current_per_phase_A = limit;
}

} // namespace

TEST(PerPhaseLimit, BindsOnlyTheConnectorsOnItsPhase) {
    auto tree = two_evse_tree(32.0f);
    set_per_phase_limit(tree, per_phase(std::nullopt, 10.0f, std::nullopt));
    MarketFixture f(tree);

    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L1}), 32.0f);
    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L2}), 10.0f);
    EXPECT_FLOAT_EQ(available_A(f.root(), ALL_GRID_PHASES), 10.0f);
}

TEST(PerPhaseLimit, NeverRaisesTheLimitForAllPhases) {
    auto tree = two_evse_tree(32.0f);
    set_per_phase_limit(tree, per_phase(40.0f, 40.0f, 40.0f));
    MarketFixture f(tree);

    EXPECT_FLOAT_EQ(available_A(f.root(), ALL_GRID_PHASES), 32.0f);
}

TEST(PerPhaseLimit, SoldCurrentCountsAgainstEachPhasesOwnLimit) {
    auto tree = two_evse_tree(32.0f);
    set_per_phase_limit(tree, per_phase(20.0f, 25.0f, std::nullopt));
    MarketFixture f(tree);

    f.evse("cp1").trade(trade_of(16.0f), {Phase::L2});

    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L1}), 20.0f);
    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L2}), 9.0f);
    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L3}), 32.0f);
    EXPECT_FLOAT_EQ(available_A(f.root(), ALL_GRID_PHASES), 9.0f);
}

TEST(PerPhaseLimit, LeavesSideLimitsMergeWithTheRootSide) {
    auto tree = two_evse_tree(32.0f);
    tree.schedule_import[0].limits_to_root.ac_max_current_per_phase_A = per_phase(20.0f, std::nullopt, std::nullopt);
    tree.schedule_import[0].limits_to_leaves.ac_max_current_per_phase_A = per_phase(25.0f, 12.0f, std::nullopt);
    MarketFixture f(tree);

    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L1}), 20.0f);
    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L2}), 12.0f);
    EXPECT_FLOAT_EQ(available_A(f.root(), {Phase::L3}), 32.0f);
}

TEST(PerPhaseLimit, GridLimitAddsUpThePhases) {
    auto tree = two_evse_tree(32.0f);
    set_per_phase_limit(tree, per_phase(32.0f, 20.0f, 10.0f));
    MarketFixture f(tree);

    const auto limit = get_grid_limit_W(f.root(), U);
    ASSERT_TRUE(limit.has_value());
    EXPECT_FLOAT_EQ(limit.value(), (32.0f + 20.0f + 10.0f) * U);
}

TEST(PerPhaseLimitLoop, ASinglePhaseEvIsHeldByItsGridPhase) {
    EnergyManagerImpl impl(make_config(), [](const auto&) {});

    // L1 32 A, L2 10 A, L3 32 A. cp1 is single phase on grid L2, as a rotated charger reports
    // it with its rotation applied; cp2 is three phase. Both load L2, which carries 10 A.
    auto cp1 = test::make_evse_node("cp1", 32.0f, 6.0f);
    auto cp2 = test::make_evse_node("cp2", 32.0f, 6.0f);
    test::set_measurement_current(cp1, 0.0f, 16.0f, 0.0f, at_plus(0));
    test::set_measurement_current(cp2, 16.0f, 16.0f, 16.0f, at_plus(0));
    auto tree = test::make_root_node("grid", 32.0f, std::nullopt, {cp1, cp2});
    set_per_phase_limit(tree, per_phase(32.0f, 10.0f, 32.0f));
    const auto results = impl.run_optimizer(tree, AT);

    EXPECT_LE(enforced_current(results, "cp1") + enforced_current(results, "cp2"), 10.0f + 1e-3f);
}

TEST(PerPhaseLimitLoop, EachSinglePhaseEvGetsWhatItsPhaseAllows) {
    EnergyManagerImpl impl(make_config(), [](const auto&) {});

    // cp1 draws on L1, cp2 on L2, and only L2 is limited to 10 A.
    auto tree = mixed_tree(at_plus(0));
    tree.children.pop_back();
    set_per_phase_limit(tree, per_phase(std::nullopt, 10.0f, std::nullopt));
    const auto results = impl.run_optimizer(tree, AT);

    EXPECT_FLOAT_EQ(enforced_current(results, "cp1"), 16.0f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp2"), 10.0f);
}

} // namespace module
