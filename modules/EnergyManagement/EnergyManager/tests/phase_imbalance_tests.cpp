// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <utils/date.hpp>

#include "EnergyManagerTestHelpers.hpp"
#include "PhaseImbalance.hpp"

namespace module {

namespace {

constexpr float LIMIT_A = 20.0f;
constexpr float VOLTAGE = 230.0f;

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
    for (const auto& cut : result.cuts) {
        if (cut.uuid == uuid) {
            return cut.new_cap_A;
        }
    }
    return std::nullopt;
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

TEST(PhaseImbalance, ExampleOnePhaseInvolved) {
    // L1 66 A, L2 90 A, L3 66 A: 24 A on L2 against a 20 A limit. CP02 and CP05 are
    // single-phase on L2, CP06 single-phase on L1: the 4 A come off 02 and 05, 2 A each.
    const PhaseCurrents site{66.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L2}, 16.0f),
        make_connector("cp05", {Phase::L2}, 16.0f),
        make_connector("cp06", {Phase::L1}, 16.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    ASSERT_EQ(result.cuts.size(), 2u);
    EXPECT_FLOAT_EQ(cap_of(result, "cp02").value(), 14.0f);
    EXPECT_FLOAT_EQ(cap_of(result, "cp05").value(), 14.0f);
    EXPECT_FALSE(cap_of(result, "cp06").has_value());
    EXPECT_EQ(result.reference, Phase::L1);
    EXPECT_FLOAT_EQ(result.L2.imbalance_A, 24.0f);
    EXPECT_FLOAT_EQ(result.L2.overshoot_A, 4.0f);
    EXPECT_FLOAT_EQ(result.L2.corrected_A, 4.0f);
    EXPECT_FLOAT_EQ(result.L2.residual_A, 0.0f);
    EXPECT_FLOAT_EQ(result.L3.overshoot_A, 0.0f);
}

TEST(PhaseImbalance, ExampleTwoPhasesInvolved) {
    // L1 90 A, L2 90 A, L3 66 A: 24 A on both L1 and L2. 02 and 05 on L1, 06 and 09 on L2:
    // every one of them 2 A.
    const PhaseCurrents site{90.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L1}, 16.0f),
        make_connector("cp05", {Phase::L1}, 16.0f),
        make_connector("cp06", {Phase::L2}, 16.0f),
        make_connector("cp09", {Phase::L2}, 16.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    ASSERT_EQ(result.cuts.size(), 4u);
    for (const auto* uuid : {"cp02", "cp05", "cp06", "cp09"}) {
        EXPECT_FLOAT_EQ(cap_of(result, uuid).value(), 14.0f) << uuid;
    }
    EXPECT_EQ(result.reference, Phase::L3);
    EXPECT_FLOAT_EQ(result.L1.overshoot_A, 4.0f);
    EXPECT_FLOAT_EQ(result.L2.overshoot_A, 4.0f);
    EXPECT_FLOAT_EQ(result.L1.residual_A, 0.0f);
    EXPECT_FLOAT_EQ(result.L2.residual_A, 0.0f);
}

// ---------------------------------------------------------------- involvement

TEST(PhaseImbalance, ThreePhaseLoadsAreNeverCut) {
    // Cutting a three-phase load lowers every phase alike and leaves the difference as it
    // is; the overshoot is a residual, not a cut.
    const PhaseCurrents site{66.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp01", {Phase::L1, Phase::L2, Phase::L3}, 16.0f),
        make_connector("cp02", {Phase::L1, Phase::L2, Phase::L3}, 16.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_TRUE(result.cuts.empty());
    EXPECT_FLOAT_EQ(result.L2.residual_A, 4.0f);
    EXPECT_FLOAT_EQ(result.L2.corrected_A, 0.0f);
}

TEST(PhaseImbalance, TwoPhaseLoadTouchingTheReferenceIsNotInvolved) {
    // L1 is the reference. A load on L1+L2 cannot change L2 - L1; only cp05 can.
    const PhaseCurrents site{66.0f, 90.0f, 70.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L1, Phase::L2}, 16.0f),
        make_connector("cp05", {Phase::L2}, 16.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    ASSERT_EQ(result.cuts.size(), 1u);
    EXPECT_FLOAT_EQ(cap_of(result, "cp05").value(), 12.0f);
}

TEST(PhaseImbalance, TwoPhaseLoadOnTwoOffendingPhasesTakesTheLargerShare) {
    // L3 is the reference. cp02 draws on L1 and L2 and is involved on both; L1's share
    // is 4 A (alone), L2's is 2 A (shared with cp06). One cut lowers both its phases, so
    // it takes the larger share once, not the sum.
    const PhaseCurrents site{90.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L1, Phase::L2}, 16.0f),
        make_connector("cp06", {Phase::L2}, 16.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    ASSERT_EQ(result.cuts.size(), 2u);
    EXPECT_FLOAT_EQ(cap_of(result, "cp02").value(), 12.0f);
    EXPECT_FLOAT_EQ(cap_of(result, "cp06").value(), 14.0f);
    EXPECT_FLOAT_EQ(result.L1.corrected_A, 4.0f);
    // L2 is over-corrected by cp02's extra 2 A: the safe direction, not compensated.
    EXPECT_FLOAT_EQ(result.L2.corrected_A, 6.0f);
    EXPECT_FLOAT_EQ(result.L2.residual_A, 0.0f);
}

// ---------------------------------------------------------------- the cut

TEST(PhaseImbalance, CutComesOffTheMeasurementWhenBelowThePreviousCap) {
    // A connector capped at 20 A but drawing 12 A does not respond to a cap of 18 A.
    const PhaseCurrents site{66.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L2}, 12.0f, 6.0f, 20.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    ASSERT_EQ(result.cuts.size(), 1u);
    EXPECT_FLOAT_EQ(result.cuts[0].new_cap_A, 8.0f);
    EXPECT_FLOAT_EQ(result.cuts[0].cut_A, 4.0f);
}

TEST(PhaseImbalance, CutNeverRaisesAnExistingCap) {
    // Drawing 16 A against a cap of 10 A (the EV has not followed yet): the new cap comes
    // off the 10 A, not the 16 A.
    const PhaseCurrents site{66.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L2}, 16.0f, 6.0f, 10.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    ASSERT_EQ(result.cuts.size(), 1u);
    EXPECT_FLOAT_EQ(result.cuts[0].new_cap_A, 6.0f);
}

TEST(PhaseImbalance, FlooredConnectorLeavesAResidual) {
    // cp02 can give 1 A before its 6 A minimum, cp05 gives its full 2 A: 1 A of the
    // overshoot remains, reported and not spread further within this run.
    const PhaseCurrents site{66.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L2}, 7.0f),
        make_connector("cp05", {Phase::L2}, 16.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    ASSERT_EQ(result.cuts.size(), 2u);
    EXPECT_FLOAT_EQ(cap_of(result, "cp02").value(), 6.0f);
    EXPECT_FLOAT_EQ(cap_of(result, "cp05").value(), 14.0f);
    EXPECT_FLOAT_EQ(result.L2.corrected_A, 3.0f);
    EXPECT_FLOAT_EQ(result.L2.residual_A, 1.0f);
}

TEST(PhaseImbalance, ConnectorAlreadyAtItsMinimumGetsNoCut) {
    const PhaseCurrents site{66.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{
        make_connector("cp02", {Phase::L2}, 6.0f, 6.0f, 6.0f),
    };

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_TRUE(result.cuts.empty());
    EXPECT_FLOAT_EQ(result.L2.residual_A, 4.0f);
}

// ---------------------------------------------------------------- nothing to do

TEST(PhaseImbalance, WithinTheLimitNothingIsCut) {
    const PhaseCurrents site{70.0f, 90.0f, 75.0f};
    const std::vector<ImbalanceConnector> connectors{make_connector("cp02", {Phase::L2}, 16.0f)};

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_TRUE(result.cuts.empty());
    EXPECT_FLOAT_EQ(result.L2.imbalance_A, 20.0f);
    EXPECT_FLOAT_EQ(result.L2.overshoot_A, 0.0f);
}

TEST(PhaseImbalance, UnknownPhaseIsNeitherReferenceNorCandidate) {
    // L3 unknown: the imbalance is judged between L1 and L2 only, never against a zero
    // nobody measured.
    const PhaseCurrents site{66.0f, 90.0f, std::nullopt};
    const std::vector<ImbalanceConnector> connectors{make_connector("cp02", {Phase::L2}, 16.0f)};

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_EQ(result.reference, Phase::L1);
    ASSERT_EQ(result.cuts.size(), 1u);
    EXPECT_FLOAT_EQ(result.cuts[0].new_cap_A, 12.0f);
    EXPECT_FLOAT_EQ(result.L3.overshoot_A, 0.0f);
}

TEST(PhaseImbalance, FewerThanTwoKnownPhasesMeansNoImbalance) {
    const PhaseCurrents site{std::nullopt, 90.0f, std::nullopt};
    const std::vector<ImbalanceConnector> connectors{make_connector("cp02", {Phase::L2}, 16.0f)};

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_FALSE(result.reference.has_value());
    EXPECT_TRUE(result.cuts.empty());
    EXPECT_FLOAT_EQ(result.L2.overshoot_A, 0.0f);
}

TEST(PhaseImbalance, NoConnectorInvolvedIsAResidual) {
    const PhaseCurrents site{66.0f, 90.0f, 66.0f};
    const std::vector<ImbalanceConnector> connectors{make_connector("cp06", {Phase::L1}, 16.0f)};

    const auto result = correct_phase_imbalance(site, connectors, LIMIT_A);

    EXPECT_TRUE(result.cuts.empty());
    EXPECT_FLOAT_EQ(result.L2.residual_A, 4.0f);
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
// all drawing \p evse_A, the site at (66, \p l2, 66) A on the grid meter. The schedules
// keep the helpers' default timestamp, half an hour before the run: a slot is only in
// force once the run is past its start. \p timestamp is the measurements' time.
types::energy::EnergyFlowRequest make_example_tree(float l2, float evse_A, const std::string& timestamp) {
    auto cp02 = test::make_evse_node("cp02", 16.0f, 6.0f);
    auto cp05 = test::make_evse_node("cp05", 16.0f, 6.0f);
    auto cp06 = test::make_evse_node("cp06", 16.0f, 6.0f);
    test::set_measurement_current(cp02, std::nullopt, evse_A, std::nullopt, timestamp);
    test::set_measurement_current(cp05, std::nullopt, evse_A, std::nullopt, timestamp);
    test::set_measurement_current(cp06, evse_A, std::nullopt, std::nullopt, timestamp);
    auto root = test::make_root_node("grid", 100.0f, std::nullopt, {cp02, cp05, cp06});
    set_root_current(root, 66.0f, l2, 66.0f, timestamp);
    return root;
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

TEST(PhaseImbalanceLoop, CutBindsInTheRunThatComputedIt) {
    EnergyManagerImpl impl(make_imbalance_config(), [](const auto&) {});

    const auto results = impl.run_optimizer(make_example_tree(90.0f, 16.0f, at_plus(0)), AT);

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 14.0f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp05"), 14.0f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp06"), 16.0f);
    const auto imbalance = impl.get_phase_imbalance();
    EXPECT_EQ(imbalance.cuts.size(), 2u);
    EXPECT_FLOAT_EQ(imbalance.L2.overshoot_A, 4.0f);
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

TEST(PhaseImbalanceLoop, HoldBlocksASecondCutOnTheSameReading) {
    EnergyManagerImpl impl(make_imbalance_config(), [](const auto&) {});
    impl.run_optimizer(make_example_tree(90.0f, 16.0f, at_plus(0)), AT);

    // 5 s later the EVs have not followed yet and the meter still shows the overshoot.
    // Within the hold that is not a reason for another 2 A.
    const auto results = impl.run_optimizer(make_example_tree(90.0f, 16.0f, at_plus(5)), AT + std::chrono::seconds(5));

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 14.0f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp05"), 14.0f);
    EXPECT_TRUE(impl.get_phase_imbalance().cuts.empty());
}

TEST(PhaseImbalanceLoop, AfterTheHoldARemainingOvershootIsCutAgain) {
    EnergyManagerImpl impl(make_imbalance_config(), [](const auto&) {});
    impl.run_optimizer(make_example_tree(90.0f, 16.0f, at_plus(0)), AT);

    // The EVs followed to 14 A, but a load elsewhere kept L2 at 88 A: 2 A still to go.
    const auto results =
        impl.run_optimizer(make_example_tree(88.0f, 14.0f, at_plus(10)), AT + std::chrono::seconds(10));

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 13.0f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp05"), 13.0f);
}

TEST(PhaseImbalanceLoop, CapIsKeptWhileReleasingItWouldRetrigger) {
    EnergyManagerImpl impl(make_imbalance_config(), [](const auto&) {});
    impl.run_optimizer(make_example_tree(90.0f, 16.0f, at_plus(0)), AT);

    // The cut worked: L2 is at 86 A, exactly the limit. Handing the 2 A back would put it
    // over again, so the cap stays.
    const auto results =
        impl.run_optimizer(make_example_tree(86.0f, 14.0f, at_plus(10)), AT + std::chrono::seconds(10));

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 14.0f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp05"), 14.0f);
}

TEST(PhaseImbalanceLoop, CapIsReleasedOnceThePhaseHasRoomForIt) {
    EnergyManagerImpl impl(make_imbalance_config(), [](const auto&) {});
    impl.run_optimizer(make_example_tree(90.0f, 16.0f, at_plus(0)), AT);

    // A car on L2 left: 70 A, 4 A above the reference. 4 + 2 given back stays under
    // 20 - 1, so the cap goes and the redistribution cap (measured + margin) takes over.
    const auto results =
        impl.run_optimizer(make_example_tree(70.0f, 14.0f, at_plus(10)), AT + std::chrono::seconds(10));

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 16.0f);
    EXPECT_FLOAT_EQ(enforced_current(results, "cp05"), 16.0f);
}

TEST(PhaseImbalanceLoop, NoSiteMeasurementMeansNoCorrection) {
    EnergyManagerImpl impl(make_imbalance_config(), [](const auto&) {});

    auto tree = make_example_tree(90.0f, 16.0f, at_plus(0));
    tree.energy_usage_root.reset();
    // Without the grid meter the site falls back to the EVSE meters, which report current
    // only; the aggregator counts no meter without power. No site measurement, no cut.
    const auto results = impl.run_optimizer(tree, AT);

    EXPECT_FLOAT_EQ(enforced_current(results, "cp02"), 16.0f);
    EXPECT_TRUE(impl.get_phase_imbalance().cuts.empty());
}

} // namespace module
