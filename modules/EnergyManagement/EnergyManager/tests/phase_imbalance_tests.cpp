// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <algorithm>

#include <gtest/gtest.h>

#include <utils/date.hpp>

#include "EnergyManagerTestHelpers.hpp"
#include "PhaseImbalance.hpp"

namespace module {

namespace {

constexpr float LIMIT_A = 20.0f;
constexpr float VOLTAGE = 230.0f;
const auto AT_LATER = date::utc_clock::time_point{} + std::chrono::seconds(60);

ImbalanceConnector make_connector(const std::string& uuid, std::set<Phase> draws_on, float measured_A,
                                  float min_A = 6.0f, std::optional<float> cap_A = std::nullopt) {
    ImbalanceConnector c;
    c.uuid = uuid;
    c.draws_on = std::move(draws_on);
    c.measured_A = measured_A;
    c.min_A = min_A;
    c.cap_A = cap_A;
    return c;
}

std::optional<float> cap_of(const ImbalanceResult& result, const std::string& uuid) {
    for (const auto& cap : result.caps) {
        if (cap.uuid == uuid) {
            return cap.new_cap_A;
        }
    }
    return std::nullopt;
}

bool is_released(const ImbalanceResult& result, const std::string& uuid) {
    return std::find(result.released.begin(), result.released.end(), uuid) != result.released.end();
}

ObservedMeasurement measurement_with_current(std::optional<float> l1, std::optional<float> l2,
                                             std::optional<float> l3) {
    ObservedMeasurement m;
    m.current_A.L1 = l1;
    m.current_A.L2 = l2;
    m.current_A.L3 = l3;
    return m;
}

} // namespace

// ---------------------------------------------------------------- phases drawn on

TEST(PhasesDrawnOn, PerPhaseCurrentAboveTheNoiseFloorCounts) {
    const auto phases = phases_drawn_on(measurement_with_current(16.0f, 0.4f, std::nullopt), VOLTAGE);
    EXPECT_EQ(phases, (std::set<Phase>{Phase::L1}));
}

TEST(PhasesDrawnOn, PerPhasePowerIsConvertedWithNominalVoltage) {
    ObservedMeasurement m;
    types::units::Power power;
    power.total = 4600.0f;
    power.L1 = 2300.0f;
    power.L2 = 2300.0f;
    power.L3 = 0.0f;
    m.power_W = power;
    EXPECT_EQ(phases_drawn_on(m, VOLTAGE), (std::set<Phase>{Phase::L1, Phase::L2}));
    EXPECT_FLOAT_EQ(measured_current_on(m, {Phase::L1, Phase::L2}, VOLTAGE), 10.0f);
}

TEST(PhasesDrawnOn, TotalOnlyMeasurementIsNotSpread) {
    // A total says nothing about which phase carries it; a symmetric guess would put a
    // single-phase EV on every phase and out of reach of the correction.
    ObservedMeasurement m;
    types::units::Power power;
    power.total = 3680.0f;
    m.power_W = power;
    EXPECT_TRUE(phases_drawn_on(m, VOLTAGE).empty());
    EXPECT_FLOAT_EQ(measured_current_on(m, {Phase::L1}, VOLTAGE), 0.0f);
}

TEST(PhasesDrawnOn, MeasuredCurrentIsTheHighestOfTheDrawnPhases) {
    const auto m = measurement_with_current(15.0f, 16.0f, 14.0f);
    EXPECT_FLOAT_EQ(measured_current_on(m, phases_drawn_on(m, VOLTAGE), VOLTAGE), 16.0f);
}

// ---------------------------------------------------------------- worked examples
//
// All with a 20 A limit and the 1 A margin, so the caps share 19 A of difference.

TEST(PhaseImbalance, ExampleOnePhaseInvolved) {
    // L1 66 A, L2 90 A, L3 66 A. CP02 and CP05 single-phase on L2 at 16 A, CP06 on L1. The
    // uncontrolled load is 50/58/66 A. L2 over L1: 19 - 8 + CP06's 16 A leaves 27 A for the
    // two on L2, 13.5 A each; L2 over L3 allows the same.
    const PhaseCurrents site{66.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L2}, 16.0f),
        make_connector("cp05", {Phase::L2}, 16.0f),
        make_connector("cp06", {Phase::L1}, 16.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_FLOAT_EQ(cap_of(result, "cp02").value(), 13.5f);
    EXPECT_FLOAT_EQ(cap_of(result, "cp05").value(), 13.5f);
    EXPECT_GE(cap_of(result, "cp06").value(), 16.0f);
    EXPECT_EQ(result.reference, Phase::L1);
    EXPECT_FLOAT_EQ(result.L2.imbalance_A, 24.0f);
    EXPECT_FLOAT_EQ(result.L2.overshoot_A, 4.0f);
    EXPECT_FLOAT_EQ(result.L2.corrected_A, 5.0f);
    EXPECT_FLOAT_EQ(result.L2.residual_A, 0.0f);
}

TEST(PhaseImbalance, ExampleTwoPhasesInvolved) {
    // L1 90 A, L2 90 A, L3 66 A: CP02 and CP05 on L1, CP06 and CP09 on L2, all at 16 A.
    // Against L3 each pair may add 27 A: 13.5 A each.
    const PhaseCurrents site{90.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L1}, 16.0f),
        make_connector("cp05", {Phase::L1}, 16.0f),
        make_connector("cp06", {Phase::L2}, 16.0f),
        make_connector("cp09", {Phase::L2}, 16.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    ASSERT_EQ(result.caps.size(), 4u);
    for (const auto* uuid : {"cp02", "cp05", "cp06", "cp09"}) {
        EXPECT_FLOAT_EQ(cap_of(result, uuid).value(), 13.5f) << uuid;
    }
    EXPECT_EQ(result.reference, Phase::L3);
}

// ---------------------------------------------------------------- who takes part

TEST(PhaseImbalance, ThreePhaseLoadsAreNeverCapped) {
    // A three-phase load changes no difference; what is left is uncontrolled.
    const PhaseCurrents site{66.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp01", {Phase::L1, Phase::L2, Phase::L3}, 16.0f),
        make_connector("cp02", {Phase::L1, Phase::L2, Phase::L3}, 16.0f, 6.0f, 12.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_TRUE(result.caps.empty());
    EXPECT_TRUE(is_released(result, "cp02"));
    EXPECT_FLOAT_EQ(result.L2.residual_A, 4.0f);
}

TEST(PhaseImbalance, TwoPhaseLoadCountsOnlyWhereItAddsToADifference) {
    // cp02 on L1+L2 does not change L2 - L1, so it holds L1 up but takes nothing of the
    // L2-over-L1 budget: cp05 alone gets 19 - (58 - 50) = 11 A there.
    const PhaseCurrents site{66.0f, 90.0f, 70.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L1, Phase::L2}, 16.0f),
        make_connector("cp05", {Phase::L2}, 16.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_FLOAT_EQ(cap_of(result, "cp05").value(), 11.0f);
    EXPECT_FLOAT_EQ(cap_of(result, "cp02").value(), 16.0f);
}

// ---------------------------------------------------------------- equal shares

TEST(PhaseImbalance, SharesAreEqualAndRoomIsHandedOutOnlyOnceFree) {
    // cp02 at 7 A and cp05 at 17 A on L2 share 19 A: 9.5 A each. cp05 still draws its 17 A
    // until it follows, so cp02 cannot rise yet and is held at what it draws.
    const PhaseCurrents site{66.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L2}, 7.0f),
        make_connector("cp05", {Phase::L2}, 17.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_FLOAT_EQ(cap_of(result, "cp05").value(), 9.5f);
    EXPECT_FLOAT_EQ(cap_of(result, "cp02").value(), 7.0f);
}

TEST(PhaseImbalance, UnequalCapsConvergeToEqualShares) {
    // Held at 6 A and 12 A with 19 A to share: cp05 gives up to 9.5 A at once, cp02 rises
    // only into what cp05 frees once it has followed, 8 A while cp05 still draws 12 A.
    const PhaseCurrents site{66.0f, 84.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L2}, 6.0f, 6.0f, 6.0f),
        make_connector("cp05", {Phase::L2}, 12.0f, 6.0f, 12.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_FLOAT_EQ(cap_of(result, "cp05").value(), 9.5f);
    EXPECT_FLOAT_EQ(cap_of(result, "cp02").value(), 8.0f);
    EXPECT_TRUE(result.released.empty());
}

TEST(PhaseImbalance, CapsAtTheirShareStay) {
    const PhaseCurrents site{66.0f, 85.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L2}, 9.5f, 6.0f, 9.5f),
        make_connector("cp05", {Phase::L2}, 9.5f, 6.0f, 9.5f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_TRUE(result.caps.empty());
}

TEST(PhaseImbalance, SmallChangesAreSkipped) {
    // 0.4 A more room than the caps use: 0.2 A each, below the deadband.
    const PhaseCurrents site{66.0f, 84.6f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L2}, 9.5f, 6.0f, 9.5f),
        make_connector("cp05", {Phase::L2}, 9.5f, 6.0f, 9.5f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_TRUE(result.caps.empty());
}

TEST(PhaseImbalance, SelfLimitedConnectorLeavesItsShareToTheOthers) {
    // cp02 draws 5 A under a 12 A cap: it wants 7 A, and cp05 gets the other 12 A.
    const PhaseCurrents site{66.0f, 80.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L2}, 5.0f, 6.0f, 12.0f),
        make_connector("cp05", {Phase::L2}, 9.0f, 6.0f, 9.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_FLOAT_EQ(cap_of(result, "cp02").value(), 7.0f);
    EXPECT_FLOAT_EQ(cap_of(result, "cp05").value(), 12.0f);
}

TEST(PhaseImbalance, RampingConnectorIsNotTakenForSelfLimited) {
    // cp02 was just given 9.5 A and still ramps at 2.5 A: within the hold that is not its
    // choice, so it keeps its share. Once the hold has passed, the same reading is.
    const PhaseCurrents site{66.0f, 78.0f, 66.0f};
    auto ramping = make_connector("cp02", {Phase::L2}, 2.5f, 6.0f, 9.5f);
    ramping.settling = true;
    const auto cp05 = make_connector("cp05", {Phase::L2}, 9.5f, 6.0f, 9.5f);

    const auto during_hold = correct_phase_imbalance(site, {ramping, cp05}, LIMIT_A);
    EXPECT_FALSE(cap_of(during_hold, "cp02").has_value());

    ramping.settling = false;
    const auto after_hold = correct_phase_imbalance(site, {ramping, cp05}, LIMIT_A);
    EXPECT_FLOAT_EQ(cap_of(after_hold, "cp02").value(), 6.0f);
}

TEST(PhaseImbalance, SettlingCapIsNotRaised) {
    const PhaseCurrents site{66.0f, 78.0f, 66.0f};
    auto cp02 = make_connector("cp02", {Phase::L2}, 6.0f, 6.0f, 6.0f);
    cp02.settling = true;
    const std::vector<ImbalanceConnector> connectors{cp02, make_connector("cp05", {Phase::L2}, 6.0f, 6.0f, 6.0f)};

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_FALSE(cap_of(result, "cp02").has_value());
    EXPECT_FLOAT_EQ(cap_of(result, "cp05").value(), 9.5f);
}

// ---------------------------------------------------------------- arrivals and pauses

TEST(PhaseImbalance, ArrivalWaitsUntilTheOthersHaveMadeRoom) {
    // cp04 and cp05 share L1 at 9.5 A each. cp06 plugs in and draws nothing yet: it may
    // start on L1, so the three share the 19 A, 6.33 A each. cp04 and cp05 are lowered at
    // once, but cp06 waits at 0 while they still draw 9.5 A.
    const PhaseCurrents site{85.0f, 66.0f, 66.0f};
    auto cp06 = make_connector("cp06", {}, 0.0f);
    cp06.arrived_at = AT_LATER;
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp04", {Phase::L1}, 9.5f, 6.0f, 9.5f),
        make_connector("cp05", {Phase::L1}, 9.5f, 6.0f, 9.5f),
        cp06,
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_NEAR(cap_of(result, "cp04").value(), 19.0f / 3.0f, 1e-4);
    EXPECT_NEAR(cap_of(result, "cp05").value(), 19.0f / 3.0f, 1e-4);
    EXPECT_FLOAT_EQ(cap_of(result, "cp06").value(), 0.0f);
}

TEST(PhaseImbalance, WaitingArrivalStartsOnceTheRoomIsFree) {
    // The next run: cp04 and cp05 followed to 6.33 A, and cp06 gets its share.
    const float share = 19.0f / 3.0f;
    const PhaseCurrents site{66.0f + 2.0f * share, 66.0f, 66.0f};
    auto cp06 = make_connector("cp06", {}, 0.0f, 6.0f, 0.0f);
    cp06.arrived_at = AT_LATER;
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp04", {Phase::L1}, share, 6.0f, share),
        make_connector("cp05", {Phase::L1}, share, 6.0f, share),
        cp06,
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    ASSERT_EQ(result.caps.size(), 1u);
    EXPECT_NEAR(cap_of(result, "cp06").value(), share, 1e-4);
}

TEST(PhaseImbalance, NewestIsPausedWhenTheSharesFallBelowTheMinimum) {
    // A fourth EV on L2: 19 A over four is 4.75 A, below 6 A. The newest is paused and the
    // other three keep 6.33 A each.
    const float share = 19.0f / 3.0f;
    const PhaseCurrents site{66.0f, 66.0f + 3.0f * share + 6.0f, 66.0f};
    auto cp09 = make_connector("cp09", {Phase::L2}, 6.0f);
    cp09.arrived_at = AT_LATER;
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L2}, share, 6.0f, share),
        make_connector("cp05", {Phase::L2}, share, 6.0f, share),
        make_connector("cp06", {Phase::L2}, share, 6.0f, share),
        cp09,
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_FLOAT_EQ(cap_of(result, "cp09").value(), 0.0f);
    EXPECT_FALSE(cap_of(result, "cp02").has_value());
}

TEST(PhaseImbalance, UncontrolledSkewPausesWhatCannotFit) {
    // 18 A of the difference sit in load the manager does not control: 1 A is left, below
    // cp02's minimum, so cp02 is paused rather than breaking the limit.
    const PhaseCurrents site{66.0f, 100.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{make_connector("cp02", {Phase::L2}, 16.0f)};

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_FLOAT_EQ(cap_of(result, "cp02").value(), 0.0f);
    EXPECT_FLOAT_EQ(result.L2.residual_A, 0.0f);
}

TEST(PhaseImbalance, UncontrolledSkewAboveTheLimitIsAResidual) {
    const PhaseCurrents site{66.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{make_connector("cp06", {Phase::L1}, 16.0f)};

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_FLOAT_EQ(result.L2.residual_A, 20.0f);
}

// ---------------------------------------------------------------- nothing to decide

TEST(PhaseImbalance, UnknownPhaseTakesPartInNoBudget) {
    // L3 unknown: only L1 and L2 are compared, never against a zero nobody measured.
    const PhaseCurrents site{66.0f, 90.0f, std::nullopt};
    const std::vector<ImbalanceConnector> connectors{make_connector("cp02", {Phase::L2}, 16.0f)};

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_EQ(result.reference, Phase::L1);
    EXPECT_FLOAT_EQ(cap_of(result, "cp02").value(), 11.0f);
    EXPECT_FLOAT_EQ(result.L3.overshoot_A, 0.0f);
}

TEST(PhaseImbalance, FewerThanTwoKnownPhasesMeansNothingIsDecided) {
    const PhaseCurrents site{std::nullopt, 90.0f, std::nullopt};
    const std::vector<ImbalanceConnector> connectors{make_connector("cp02", {Phase::L2}, 16.0f)};

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_FALSE(result.reference.has_value());
    EXPECT_TRUE(result.caps.empty());
}

// ---------------------------------------------------------------- through the optimizer

namespace {

const auto AT = Everest::Date::from_rfc3339(test::NOW_TS);

std::string at_plus(int seconds) {
    return Everest::Date::to_rfc3339(AT + std::chrono::seconds(seconds));
}

// The grid connection's own meter, carrying the whole site per phase. The aggregator only
// counts a reading that reports power, so a total is set alongside the currents.
void set_root_current(types::energy::EnergyFlowRequest& node, float l1, float l2, float l3,
                      const std::string& timestamp) {
    types::powermeter::Powermeter p;
    p.timestamp = timestamp;
    p.energy_Wh_import.total = 0.0f;
    types::units::Power power;
    power.total = (l1 + l2 + l3) * VOLTAGE;
    p.power_W = power;
    types::units::Current current;
    current.L1 = l1;
    current.L2 = l2;
    current.L3 = l3;
    p.current_A = current;
    node.energy_usage_root = p;
}

// Worked example 1 as a tree: cp02 and cp05 single-phase on L2, cp06 single-phase on L1,
// cp02 and cp05 drawing \p cp02_A and \p cp05_A, cp06 16 A, the site at (66, \p l2, 66) A on the grid meter. The
// schedules keep the helpers' default timestamp, half an hour before the run: a slot is only in force once the run is
// past its start. \p timestamp is the measurements' time.
types::energy::EnergyFlowRequest make_example_tree(float l2, float cp02_A, float cp05_A, const std::string& timestamp) {
    auto cp02 = test::make_evse_node("cp02", 16.0f, 6.0f);
    auto cp05 = test::make_evse_node("cp05", 16.0f, 6.0f);
    auto cp06 = test::make_evse_node("cp06", 16.0f, 6.0f);
    test::set_measurement_current(cp02, std::nullopt, cp02_A, std::nullopt, timestamp);
    test::set_measurement_current(cp05, std::nullopt, cp05_A, std::nullopt, timestamp);
    test::set_measurement_current(cp06, 16.0f, std::nullopt, std::nullopt, timestamp);
    auto root = test::make_root_node("grid", 100.0f, std::nullopt, {cp02, cp05, cp06});
    set_root_current(root, 66.0f, l2, 66.0f, timestamp);
    return root;
}

types::energy::EnergyFlowRequest make_example_tree(float l2, float evse_A, const std::string& timestamp) {
    return make_example_tree(l2, evse_A, evse_A, timestamp);
}

// PowerRedistribution tracking the measurement from the full allocation down, so the
// enforced limit is measured plus the 2 A margin unless the imbalance cap is lower.
EnergyManagerConfig make_imbalance_config() {
    auto c = test::make_redistribution_config();
    c.redistribution_start_with_lower_limit = false;
    c.phase_symmetry_enabled = true;
    c.max_phase_imbalance_A = LIMIT_A;
    c.phase_imbalance_hold_s = 10;
    return c;
}

float enforced_current(const std::vector<types::energy::EnforcedLimits>& results, const std::string& uuid) {
    const auto limit = test::find_limit(results, uuid);
    if (not limit.has_value() or not limit.value().limits_root_side.ac_max_current_A.has_value()) {
        return -1.0f;
    }
    return limit.value().limits_root_side.ac_max_current_A.value().value;
}

} // namespace

TEST(PhaseImbalanceLoop, CapBindsInTheRunThatComputedIt) {
    EnergyManagerImpl impl(make_imbalance_config(), [](const auto&) {});

    const auto results = impl.run_optimizer(make_example_tree(90.0f, 16.0f, at_plus(0)), AT);

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 13.5f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp05"), 13.5f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp06"), 16.0f);
    EXPECT_FLOAT_EQ(impl.get_phase_imbalance().L2.overshoot_A, 4.0f);
}

TEST(PhaseImbalanceLoop, FlagOffLeavesTheContextsUntouched) {
    auto config = make_imbalance_config();
    config.phase_symmetry_enabled = false;
    EnergyManagerImpl impl(config, [](const auto&) {});

    const auto results = impl.run_optimizer(make_example_tree(90.0f, 16.0f, at_plus(0)), AT);

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 16.0f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp05"), 16.0f);
    EXPECT_FALSE(impl.get_phase_imbalance().reference.has_value());
}

TEST(PhaseImbalanceLoop, FastChargingIgnoresTheFlag) {
    auto config = make_imbalance_config();
    config.broker_strategy = "FastCharging";
    EnergyManagerImpl impl(config, [](const auto&) {});

    const auto results = impl.run_optimizer(make_example_tree(90.0f, 16.0f, at_plus(0)), AT);

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 16.0f);
    EXPECT_FALSE(impl.get_phase_imbalance().reference.has_value());
}

TEST(PhaseImbalanceLoop, AReadingThatHasNotFollowedYetChangesNothing) {
    EnergyManagerImpl impl(make_imbalance_config(), [](const auto&) {});
    impl.run_optimizer(make_example_tree(90.0f, 16.0f, at_plus(0)), AT);

    // 5 s later the EVs still draw 16 A: the caps already account for it.
    const auto results = impl.run_optimizer(make_example_tree(90.0f, 16.0f, at_plus(5)), AT + std::chrono::seconds(5));

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 13.5f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp05"), 13.5f);
    EXPECT_FALSE(cap_of(impl.get_phase_imbalance(), "cp02").has_value());
    EXPECT_FALSE(cap_of(impl.get_phase_imbalance(), "cp05").has_value());
}

TEST(PhaseImbalanceLoop, MoreUncontrolledLoadLowersTheCaps) {
    EnergyManagerImpl impl(make_imbalance_config(), [](const auto&) {});
    impl.run_optimizer(make_example_tree(90.0f, 16.0f, at_plus(0)), AT);

    // The EVs followed to 14 A, but L2 carries 88 A: 2 A more elsewhere on L2.
    const auto results =
        impl.run_optimizer(make_example_tree(88.0f, 14.0f, at_plus(10)), AT + std::chrono::seconds(10));

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 12.5f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp05"), 12.5f);
}

TEST(PhaseImbalanceLoop, CapsRiseWhenThePhaseGainsRoom) {
    EnergyManagerImpl impl(make_imbalance_config(), [](const auto&) {});
    impl.run_optimizer(make_example_tree(90.0f, 16.0f, at_plus(0)), AT);

    // A load on L2 left: the caps rise to 21.5 A, and the EVs' own 16 A maximum is what
    // binds.
    const auto results =
        impl.run_optimizer(make_example_tree(70.0f, 14.0f, at_plus(10)), AT + std::chrono::seconds(10));

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 16.0f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp05"), 16.0f);
    EXPECT_FLOAT_EQ(cap_of(impl.get_phase_imbalance(), "cp02").value(), 21.5f);
}

TEST(PhaseImbalanceLoop, ALateArrivalIsSharedEqually) {
    EnergyManagerImpl impl(make_imbalance_config(), [](const auto&) {});

    // cp02 at 15 A and cp05 at 8.5 A share 19 A: cp02 goes to 9.5 A at once, cp05 is held
    // at its 8.5 A until cp02 has followed.
    const auto first = impl.run_optimizer(make_example_tree(89.5f, 15.0f, 8.5f, at_plus(0)), AT);
    EXPECT_FLOAT_EQ(enforced_current(first, "cp02"), 9.5f);
    EXPECT_FLOAT_EQ(enforced_current(first, "cp05"), 8.5f);

    const auto second =
        impl.run_optimizer(make_example_tree(84.0f, 9.5f, 8.5f, at_plus(10)), AT + std::chrono::seconds(10));

    EXPECT_FLOAT_EQ(enforced_current(second, "cp02"), 9.5f);
    EXPECT_FLOAT_EQ(enforced_current(second, "cp05"), 9.5f);
}

TEST(PhaseImbalanceLoop, PausedConnectorIsSentZero) {
    EnergyManagerImpl impl(make_imbalance_config(), [](const auto&) {});

    // 26 A of L2's difference are elsewhere: nothing is left for the two EVs on L2.
    const auto results = impl.run_optimizer(make_example_tree(124.0f, 16.0f, at_plus(0)), AT);

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 0.0f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp05"), 0.0f);
}

TEST(PhaseImbalanceLoop, NoSiteMeasurementMeansNoCorrection) {
    EnergyManagerImpl impl(make_imbalance_config(), [](const auto&) {});

    auto tree = make_example_tree(90.0f, 16.0f, at_plus(0));
    tree.energy_usage_root.reset();
    // Without the grid meter the site falls back to the EVSE meters, which report current
    // only; the aggregator counts no meter without power. No site measurement, no cut.
    const auto results = impl.run_optimizer(tree, AT);

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 16.0f);
    EXPECT_TRUE(impl.get_phase_imbalance().caps.empty());
}

} // namespace module
