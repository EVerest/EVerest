// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <algorithm>

#include <utils/date.hpp>

#include "EnergyManagerImpl.hpp"
#include "EnergyManagerTestHelpers.hpp"
#include "Market.hpp"
#include "Offer.hpp"
#include "PhaseAllocation.hpp"

namespace module {

namespace {

const auto AT = Everest::Date::from_rfc3339("2026-08-04T12:30:00.000Z");

// globals must be initialised before any Market or Offer is constructed.
void init_globals(const types::energy::EnergyFlowRequest& request, const EnergyManagerConfig& config) {
    globals.init(AT, config.schedule_interval_duration, config.schedule_total_duration, config.slice_ampere,
                 config.slice_watt, false, request);
}

// Enforced per phase allocation for one connector, or nullopt if it has none.
std::optional<PhaseAllocation> enforced_per_phase(const std::vector<types::energy::EnforcedLimits>& results,
                                                  const std::string& uuid) {
    const auto limit = test::find_limit(results, uuid);
    if (not limit.has_value() or not limit.value().limits_root_side.ac_max_current_per_phase_A.has_value()) {
        return std::nullopt;
    }
    const auto& p = limit.value().limits_root_side.ac_max_current_per_phase_A.value();
    return PhaseAllocation{p.L1.has_value() ? p.L1.value().value : 0.f, p.L2.has_value() ? p.L2.value().value : 0.f,
                           p.L3.has_value() ? p.L3.value().value : 0.f};
}

float enforced_current(const std::vector<types::energy::EnforcedLimits>& results, const std::string& uuid) {
    const auto limit = test::find_limit(results, uuid);
    if (not limit.has_value() or not limit.value().limits_root_side.ac_max_current_A.has_value()) {
        return 0.0f;
    }
    return limit.value().limits_root_side.ac_max_current_A.value().value;
}

// Marks a connector as charging on a single phase (L1 under this plan's model).
void make_single_phase(types::energy::EnergyFlowRequest& evse) {
    evse.schedule_import[0].limits_to_root.ac_number_of_active_phases = 1;
    evse.schedule_import[0].limits_to_root.ac_max_phase_count = {1, "TEST"};
}

} // namespace

// ---------------------------------------------------------------- market / offer propagation

TEST(PerPhaseMarket, PerPhaseLimitReachesTheOffer) {
    auto config = test::make_default_config();

    // The connector declares an unbalanced per phase limit.
    auto evse = test::make_evse_node("cp01", 32.0f, 6.0f);
    evse.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({20.0f, 10.0f, 32.0f}, "TEST_cp01");
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    init_globals(request, config);
    Market market(request, static_cast<float>(config.nominal_ac_voltage));
    const auto evses = market.get_list_of_evses();
    ASSERT_EQ(evses.size(), 1U);

    // This is the propagation gate: without the two whitelist changes the offer has no per
    // phase field at all and every later per phase test is unreachable.
    Offer offer(*evses[0]);
    ASSERT_TRUE(offer.import_offer[0].limits_to_root.ac_max_current_per_phase_A.has_value());

    const auto& per_phase = offer.import_offer[0].limits_to_root.ac_max_current_per_phase_A.value();
    ASSERT_TRUE(per_phase.L1.has_value());
    ASSERT_TRUE(per_phase.L2.has_value());
    EXPECT_FLOAT_EQ(per_phase.L1.value().value, 20.0f);
    EXPECT_FLOAT_EQ(per_phase.L2.value().value, 10.0f);
}

TEST(PerPhaseMarket, TighterPerPhaseLimitOfAnAncestorWins) {
    auto config = test::make_default_config();

    auto evse = test::make_evse_node("cp01", 32.0f, 6.0f);
    evse.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({32.0f, 32.0f, 32.0f}, "TEST_cp01");

    // The grid node is unbalanced: other loads occupy L2 and L3.
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    request.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({32.0f, 10.0f, 10.0f}, "TEST_grid");

    init_globals(request, config);
    Market market(request, static_cast<float>(config.nominal_ac_voltage));
    const auto evses = market.get_list_of_evses();
    ASSERT_EQ(evses.size(), 1U);

    Offer offer(*evses[0]);
    const auto& per_phase = offer.import_offer[0].limits_to_root.ac_max_current_per_phase_A.value();

    EXPECT_FLOAT_EQ(per_phase.L1.value().value, 32.0f);
    EXPECT_FLOAT_EQ(per_phase.L2.value().value, 10.0f);
    EXPECT_FLOAT_EQ(per_phase.L3.value().value, 10.0f);
}

TEST(PerPhaseMarket, GetRootWalksToTheTopOfTheTree) {
    auto config = test::make_default_config();
    auto cp01 = test::make_evse_node("cp01", 32.0f, 6.0f);
    auto cp02 = test::make_evse_node("cp02", 32.0f, 6.0f);
    const auto request = test::make_root_node("grid", 63.0f, std::nullopt, {cp01, cp02});

    init_globals(request, config);
    Market market(request, static_cast<float>(config.nominal_ac_voltage));
    const auto evses = market.get_list_of_evses();

    ASSERT_EQ(evses.size(), 2U);
    EXPECT_EQ(evses[0]->get_root(), &market);
    EXPECT_EQ(evses[1]->get_root(), &market);
    EXPECT_EQ(market.get_root(), &market);
}

TEST(PerPhaseMarket, SoldPerPhaseStartsAtZero) {
    auto config = test::make_default_config();
    auto evse = test::make_evse_node("cp01", 32.0f, 6.0f);
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    init_globals(request, config);
    Market market(request, static_cast<float>(config.nominal_ac_voltage));

    const auto sold = market.get_sold_per_phase_A(0);
    EXPECT_FLOAT_EQ(sold.l1_A, 0.0f);
    EXPECT_FLOAT_EQ(sold.l2_A, 0.0f);
    EXPECT_FLOAT_EQ(sold.l3_A, 0.0f);
}

TEST(PerPhaseMarket, SymmetricTradeIsVisibleOnEveryPhase) {
    auto config = test::make_default_config();
    auto evse = test::make_evse_node("cp01", 32.0f, 6.0f);
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    init_globals(request, config);
    Market market(request, static_cast<float>(config.nominal_ac_voltage));

    // A trade expressed with the legacy symmetric field only.
    ScheduleRes trade = globals.empty_schedule_res;
    trade[0].limits_to_root.ac_max_current_A = {10.0f, "TEST"};
    trade[0].limits_to_root.ac_max_phase_count = {3, "TEST"};
    market.trade(trade);

    const auto sold = market.get_sold_per_phase_A(0);
    EXPECT_FLOAT_EQ(sold.l1_A, 10.0f);
    EXPECT_FLOAT_EQ(sold.l2_A, 10.0f);
    EXPECT_FLOAT_EQ(sold.l3_A, 10.0f);
}

TEST(PerPhaseMarket, PerPhaseTradesAccumulatePerPhase) {
    auto config = test::make_default_config();
    auto evse = test::make_evse_node("cp01", 32.0f, 6.0f);
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    init_globals(request, config);
    Market market(request, static_cast<float>(config.nominal_ac_voltage));

    // 16A on L1 only.
    ScheduleRes trade_l1 = globals.empty_schedule_res;
    trade_l1[0].limits_to_root.ac_max_current_A = {16.0f, "TEST"};
    trade_l1[0].limits_to_root.ac_max_phase_count = {1, "TEST"};
    trade_l1[0].limits_to_root.ac_max_current_per_phase_A = to_per_phase_limit({16.0f, 0.0f, 0.0f}, "TEST");
    market.trade(trade_l1);

    // 10A on L2 only.
    ScheduleRes trade_l2 = globals.empty_schedule_res;
    trade_l2[0].limits_to_root.ac_max_current_A = {10.0f, "TEST"};
    trade_l2[0].limits_to_root.ac_max_phase_count = {1, "TEST"};
    trade_l2[0].limits_to_root.ac_max_current_per_phase_A = to_per_phase_limit({0.0f, 10.0f, 0.0f}, "TEST");
    market.trade(trade_l2);

    const auto sold = market.get_sold_per_phase_A(0);
    EXPECT_FLOAT_EQ(sold.l1_A, 16.0f);
    EXPECT_FLOAT_EQ(sold.l2_A, 10.0f);
    EXPECT_FLOAT_EQ(sold.l3_A, 0.0f);
}

TEST(PerPhaseMarket, MixedSymmetricAndPerPhaseTradesSumCorrectly) {
    auto config = test::make_default_config();
    auto evse = test::make_evse_node("cp01", 32.0f, 6.0f);
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    init_globals(request, config);
    Market market(request, static_cast<float>(config.nominal_ac_voltage));

    // A legacy three phase trade of 6A on all phases.
    ScheduleRes symmetric = globals.empty_schedule_res;
    symmetric[0].limits_to_root.ac_max_current_A = {6.0f, "TEST"};
    symmetric[0].limits_to_root.ac_max_phase_count = {3, "TEST"};
    market.trade(symmetric);

    // A per phase trade of 16A on L1 only.
    ScheduleRes per_phase = globals.empty_schedule_res;
    per_phase[0].limits_to_root.ac_max_current_A = {16.0f, "TEST"};
    per_phase[0].limits_to_root.ac_max_phase_count = {1, "TEST"};
    per_phase[0].limits_to_root.ac_max_current_per_phase_A = to_per_phase_limit({16.0f, 0.0f, 0.0f}, "TEST");
    market.trade(per_phase);

    // The symmetric trade counts on all three phases, the per phase trade only on L1.
    const auto sold = market.get_sold_per_phase_A(0);
    EXPECT_FLOAT_EQ(sold.l1_A, 22.0f);
    EXPECT_FLOAT_EQ(sold.l2_A, 6.0f);
    EXPECT_FLOAT_EQ(sold.l3_A, 6.0f);
}

// ---------------------------------------------------------------- broker per phase buying

TEST(PerPhaseBroker, UnbalancedNodeLimitCapsAtWeakestPhase) {
    auto config = test::make_default_config();

    // The connector itself would take 32A on all three phases.
    auto evse = test::make_evse_node("cp01", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    // The grid connection is unbalanced: other building load occupies L2 and L3.
    request.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({32.0f, 10.0f, 10.0f}, "TEST_grid");

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto results = impl.run_optimizer(request, AT);

    // A three phase EVSE has one current setpoint, so the weakest phase governs: 10A, not
    // 32A. Without per phase limits the optimizer would have granted 32A and overloaded
    // L2/L3.
    EXPECT_NEAR(enforced_current(results, "cp01"), 10.0f, 0.6f);

    const auto per_phase = enforced_per_phase(results, "cp01");
    ASSERT_TRUE(per_phase.has_value());
    EXPECT_NEAR(per_phase.value().l1_A, per_phase.value().l2_A, 0.01f);
    EXPECT_NEAR(per_phase.value().l2_A, per_phase.value().l3_A, 0.01f);
}

TEST(PerPhaseBroker, AllocationIsSymmetricAcrossOccupiedPhases) {
    auto config = test::make_default_config();

    auto evse = test::make_evse_node("cp01", 16.0f, 6.0f);
    evse.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({16.0f, 16.0f, 16.0f}, "TEST_cp01");
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto per_phase = enforced_per_phase(impl.run_optimizer(request, AT), "cp01");

    ASSERT_TRUE(per_phase.has_value());
    // One EVSE, one duty cycle: never an asymmetric grant.
    EXPECT_FLOAT_EQ(per_phase.value().imbalance_A(), 0.0f);
    EXPECT_NEAR(per_phase.value().l1_A, 16.0f, 0.6f);
}

TEST(PerPhaseBroker, SinglePhaseConnectorDoesNotConsumeUnusedPhases) {
    auto config = test::make_default_config();

    // A single phase connector pinned to exactly 16A.
    auto cp01 = test::make_evse_node("cp01", 16.0f, 16.0f);
    make_single_phase(cp01);
    cp01.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({16.0f, 0.0f, 0.0f}, "TEST_cp01");
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {cp01});

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto per_phase = enforced_per_phase(impl.run_optimizer(request, AT), "cp01");

    ASSERT_TRUE(per_phase.has_value());
    EXPECT_NEAR(per_phase.value().l1_A, 16.0f, 0.01f);
    // The structural win: L2 and L3 budget is untouched. The legacy symmetric model booked
    // 16A against all three phases at every ancestor node.
    EXPECT_FLOAT_EQ(per_phase.value().l2_A, 0.0f);
    EXPECT_FLOAT_EQ(per_phase.value().l3_A, 0.0f);
}

TEST(PerPhaseBroker, DeadPhaseBlocksAThreePhaseConnector) {
    auto config = test::make_default_config();

    auto evse = test::make_evse_node("cp01", 32.0f, 6.0f);
    // L1 carries no capacity at all.
    evse.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({0.0f, 32.0f, 32.0f}, "TEST_cp01");
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto results = impl.run_optimizer(request, AT);

    // Correct outcome: a three phase EVSE cannot charge with a dead phase, and its minimum
    // current purchase is refused rather than silently reduced, so nothing is allocated.
    EXPECT_FLOAT_EQ(enforced_current(results, "cp01"), 0.0f);
}

TEST(PerPhaseBroker, SymmetricOnlyTreeProducesNoPerPhaseField) {
    auto config = test::make_default_config();
    auto cp01 = test::make_evse_node("cp01", 16.0f, 6.0f);
    auto cp02 = test::make_evse_node("cp02", 16.0f, 6.0f);
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {cp01, cp02});

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto results = impl.run_optimizer(request, AT);

    // Nothing in the tree declared per phase limits, so results stay legacy shaped.
    ASSERT_FALSE(results.empty());
    for (const auto& r : results) {
        EXPECT_FALSE(r.limits_root_side.ac_max_current_per_phase_A.has_value());
    }
}

// ---------------------------------------------------------------- symmetry enforcement

TEST(PerPhaseSymmetryEnforcement, ImbalanceIsCappedWhenEnabled) {
    auto config = test::make_default_config();
    config.phase_symmetry_enabled = true;
    config.max_phase_imbalance_A = 10.0;

    // A single phase connector that would happily take 32A on L1 alone.
    auto evse = test::make_evse_node("cp01", 32.0f, 6.0f);
    make_single_phase(evse);
    evse.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({32.0f, 0.0f, 0.0f}, "TEST_cp01");
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto per_phase = enforced_per_phase(impl.run_optimizer(request, AT), "cp01");

    ASSERT_TRUE(per_phase.has_value());
    // L2 and L3 carry nothing, so L1 may not exceed the imbalance limit.
    EXPECT_LE(per_phase.value().l1_A, 10.0f + 0.01f);
    EXPECT_GT(per_phase.value().l1_A, 0.0f);
}

TEST(PerPhaseSymmetryEnforcement, DisabledFlagAllowsFullImbalance) {
    auto config = test::make_default_config();
    config.phase_symmetry_enabled = false;
    config.max_phase_imbalance_A = 10.0;

    auto evse = test::make_evse_node("cp01", 32.0f, 6.0f);
    make_single_phase(evse);
    evse.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({32.0f, 0.0f, 0.0f}, "TEST_cp01");
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto per_phase = enforced_per_phase(impl.run_optimizer(request, AT), "cp01");

    ASSERT_TRUE(per_phase.has_value());
    EXPECT_NEAR(per_phase.value().l1_A, 32.0f, 0.6f);
}

TEST(PerPhaseSymmetryEnforcement, BalancedThreePhaseIsUnaffected) {
    auto config = test::make_default_config();
    config.phase_symmetry_enabled = true;
    config.max_phase_imbalance_A = 1.0;

    auto evse = test::make_evse_node("cp01", 16.0f, 6.0f);
    evse.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({16.0f, 16.0f, 16.0f}, "TEST_cp01");
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto per_phase = enforced_per_phase(impl.run_optimizer(request, AT), "cp01");

    ASSERT_TRUE(per_phase.has_value());
    // A perfectly balanced three phase draw never violates symmetry.
    EXPECT_NEAR(per_phase.value().l1_A, 16.0f, 0.6f);
    EXPECT_FLOAT_EQ(per_phase.value().imbalance_A(), 0.0f);
}

TEST(PerPhaseSymmetryEnforcement, SymmetryCanPreventASecondSessionFromStarting) {
    auto config = test::make_default_config();
    config.phase_symmetry_enabled = true;
    config.max_phase_imbalance_A = 10.0;

    // Both single phase connectors are modelled on L1, so their currents add on one phase.
    auto cp01 = test::make_evse_node("cp01", 16.0f, 6.0f);
    make_single_phase(cp01);
    cp01.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({16.0f, 0.0f, 0.0f}, "TEST_cp01");

    auto cp02 = test::make_evse_node("cp02", 16.0f, 6.0f);
    make_single_phase(cp02);
    cp02.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({16.0f, 0.0f, 0.0f}, "TEST_cp02");

    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {cp01, cp02});

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto results = impl.run_optimizer(request, AT);

    const float total_l1 = enforced_current(results, "cp01") + enforced_current(results, "cp02");

    // The hard constraint holds: L1 never exceeds the imbalance limit.
    EXPECT_LE(total_l1, 10.0f + 0.01f);
    // With a 6A minimum each, two sessions cannot both fit under a 10A ceiling. One is
    // refused rather than being granted a current below its minimum - refusing is correct,
    // an EVSE cannot signal a duty cycle below ac_min_current_A.
    const float lower = std::min(enforced_current(results, "cp01"), enforced_current(results, "cp02"));
    EXPECT_FLOAT_EQ(lower, 0.0f);
}

// ---------------------------------------------------------------- per phase slice trading

TEST(PerPhaseSliceTrading, FreedCapacityOnAPhaseGoesToAnotherConnectorInOneCycle) {
    auto config = test::make_default_config();
    config.phase_symmetry_enabled = false;

    // cp01 is pinned to 6A on L1 by its own limit, leaving 26A of L1 capacity for cp02,
    // which wants as much as it can get.
    auto cp01 = test::make_evse_node("cp01", 6.0f, 6.0f);
    make_single_phase(cp01);
    cp01.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({6.0f, 0.0f, 0.0f}, "TEST_cp01");

    auto cp02 = test::make_evse_node("cp02", 32.0f, 6.0f);
    make_single_phase(cp02);
    cp02.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({32.0f, 0.0f, 0.0f}, "TEST_cp02");

    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {cp01, cp02});
    request.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({32.0f, 32.0f, 32.0f}, "TEST_grid");

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    // A single optimizer run must complete the redistribution.
    const auto results = impl.run_optimizer(request, AT);

    const float cp01_A = enforced_current(results, "cp01");
    const float cp02_A = enforced_current(results, "cp02");

    EXPECT_NEAR(cp01_A, 6.0f, 0.01f);
    // cp02 picks up most of the remaining L1 capacity, not merely an equal share.
    EXPECT_GT(cp02_A, 20.0f);
    // Together they must not exceed the grid's per phase limit.
    EXPECT_LE(cp01_A + cp02_A, 32.0f + 0.01f);
}

TEST(PerPhaseSliceTrading, AvailabilityShrinksSoTradingConverges) {
    auto config = test::make_default_config();

    auto cp01 = test::make_evse_node("cp01", 32.0f, 6.0f);
    cp01.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({32.0f, 32.0f, 32.0f}, "TEST_cp01");
    auto request = test::make_root_node("grid", 16.0f, std::nullopt, {cp01});
    request.schedule_import[0].limits_to_root.ac_max_current_per_phase_A =
        to_per_phase_limit({16.0f, 16.0f, 16.0f}, "TEST_grid");

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto results = impl.run_optimizer(request, AT);

    // The connector is capped by the grid node, not by its own 32A limit. If per phase
    // availability did not shrink each round this would exceed 16A or spin for 100 rounds.
    EXPECT_NEAR(enforced_current(results, "cp01"), 16.0f, 0.6f);
}

} // namespace module
