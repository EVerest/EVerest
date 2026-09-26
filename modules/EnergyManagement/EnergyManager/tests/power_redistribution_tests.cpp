// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <chrono>

#include <utils/date.hpp>

#include "BrokerPowerRedistribution.hpp"
#include "EnergyManagerTestHelpers.hpp"

namespace module {

namespace {

const auto AT = Everest::Date::from_rfc3339(test::NOW_TS);

// Runs the optimizer over the tree and returns the enforced ampere limit for one connector.
float run_and_get_current(EnergyManagerImpl& impl, const types::energy::EnergyFlowRequest& request,
                          const std::string& uuid, date::utc_clock::time_point at) {
    const auto results = impl.run_optimizer(request, at);
    const auto limit = test::find_limit(results, uuid);
    if (not limit.has_value() or not limit.value().limits_root_side.ac_max_current_A.has_value()) {
        return -1.0f;
    }
    return limit.value().limits_root_side.ac_max_current_A.value().value;
}

ObservedMeasurement make_measurement_with_current(std::optional<float> l1, std::optional<float> l2,
                                                  std::optional<float> l3) {
    ObservedMeasurement m;
    m.current_A.L1 = l1;
    m.current_A.L2 = l2;
    m.current_A.L3 = l3;
    m.measured_at = AT;
    return m;
}

} // namespace

// ---------------------------------------------------------------- per-phase current derivation

TEST(RedistributionCap, PerPhaseCurrentIsUsedDirectly) {
    const auto measured = measured_phase_currents(make_measurement_with_current(10.0f, 11.0f, 12.0f), 230.0f, 3);
    ASSERT_TRUE(measured.L1.has_value());
    ASSERT_TRUE(measured.L2.has_value());
    ASSERT_TRUE(measured.L3.has_value());
    EXPECT_FLOAT_EQ(measured.L1.value(), 10.0f);
    EXPECT_FLOAT_EQ(measured.L2.value(), 11.0f);
    EXPECT_FLOAT_EQ(measured.L3.value(), 12.0f);
}

TEST(RedistributionCap, PartialPhaseCurrentLeavesUnknownPhasesUnconstrained) {
    // A single-phase meter reports L1 only; the other phases must stay unknown, not zero.
    const auto measured =
        measured_phase_currents(make_measurement_with_current(16.0f, std::nullopt, std::nullopt), 230.0f, 3);
    ASSERT_TRUE(measured.L1.has_value());
    EXPECT_FLOAT_EQ(measured.L1.value(), 16.0f);
    EXPECT_FALSE(measured.L2.has_value());
    EXPECT_FALSE(measured.L3.has_value());
}

TEST(RedistributionCap, PerPhasePowerIsConvertedWithNominalVoltage) {
    ObservedMeasurement m;
    types::units::Power power;
    power.total = 5750.0f;
    power.L1 = 2300.0f;
    power.L2 = 1150.0f;
    m.power_W = power;
    m.measured_at = AT;

    const auto measured = measured_phase_currents(m, 230.0f, 3);
    ASSERT_TRUE(measured.L1.has_value());
    ASSERT_TRUE(measured.L2.has_value());
    EXPECT_FLOAT_EQ(measured.L1.value(), 10.0f);
    EXPECT_FLOAT_EQ(measured.L2.value(), 5.0f);
    EXPECT_FALSE(measured.L3.has_value());
}

TEST(RedistributionCap, TotalPowerOnlyIsSpreadOverActivePhases) {
    ObservedMeasurement m;
    types::units::Power power;
    power.total = 4140.0f;
    m.power_W = power;
    m.measured_at = AT;

    const auto measured = measured_phase_currents(m, 230.0f, 3);
    ASSERT_TRUE(measured.L1.has_value());
    ASSERT_TRUE(measured.L2.has_value());
    ASSERT_TRUE(measured.L3.has_value());
    EXPECT_FLOAT_EQ(measured.L1.value(), 6.0f);
    EXPECT_FLOAT_EQ(measured.L2.value(), 6.0f);
    EXPECT_FLOAT_EQ(measured.L3.value(), 6.0f);
}

TEST(RedistributionCap, TotalPowerWithOneActivePhaseGoesToOnePhase) {
    // The caller passes 1 when the EVSE does not report its phase count: all the power on
    // one phase is the highest per-phase current and therefore the least restrictive
    // limit. Assuming 3 would cut a single-phase EV to a third of what it draws.
    ObservedMeasurement m;
    types::units::Power power;
    power.total = 4140.0f;
    m.power_W = power;
    m.measured_at = AT;

    const auto measured = measured_phase_currents(m, 230.0f, 1);
    ASSERT_TRUE(measured.L1.has_value());
    EXPECT_FLOAT_EQ(measured.L1.value(), 18.0f);
    EXPECT_FALSE(measured.L2.has_value());
    EXPECT_FALSE(measured.L3.has_value());
}

TEST(RedistributionCap, PerPhaseCurrentWinsOverPower) {
    auto m = make_measurement_with_current(10.0f, 10.0f, 10.0f);
    types::units::Power power;
    power.total = 20700.0f; // would be 30 A per phase
    m.power_W = power;

    const auto measured = measured_phase_currents(m, 230.0f, 3);
    ASSERT_TRUE(measured.L1.has_value());
    EXPECT_FLOAT_EQ(measured.L1.value(), 10.0f);
}

TEST(RedistributionCap, NoMeasurementYieldsNoCurrents) {
    const auto measured = measured_phase_currents(ObservedMeasurement{}, 230.0f, 3);
    EXPECT_FALSE(measured.L1.has_value());
    EXPECT_FALSE(measured.L2.has_value());
    EXPECT_FALSE(measured.L3.has_value());
}

TEST(RedistributionCap, ZeroNominalVoltageYieldsNoCurrentsFromPower) {
    ObservedMeasurement m;
    types::units::Power power;
    power.total = 4140.0f;
    m.power_W = power;

    const auto measured = measured_phase_currents(m, 0.0f, 3);
    EXPECT_FALSE(measured.L1.has_value());
}

// ---------------------------------------------------------------- scalar collapse

TEST(RedistributionCap, ScalarCapIsTheHighestKnownPhase) {
    // The single ac_max_current_A is applied to every phase, so the lowest phase would
    // starve the phase that legitimately draws most.
    EXPECT_FLOAT_EQ(to_scalar_cap({6.0f, 6.0f, 20.0f}).value(), 20.0f);
    EXPECT_FLOAT_EQ(to_scalar_cap({16.0f, std::nullopt, std::nullopt}).value(), 16.0f);
    EXPECT_FALSE(to_scalar_cap({}).has_value());
}

// ---------------------------------------------------------------- measurement freshness

TEST(RedistributionCap, MeasurementWithoutValueCannotLimit) {
    EXPECT_FALSE(measurement_can_limit(ObservedMeasurement{}, AT, std::chrono::seconds(10)));
}

TEST(RedistributionCap, MeasurementWithoutTimestampCannotLimit) {
    auto m = make_measurement_with_current(10.0f, std::nullopt, std::nullopt);
    m.measured_at = std::nullopt;
    EXPECT_FALSE(measurement_can_limit(m, AT, std::chrono::seconds(10)));
}

TEST(RedistributionCap, FreshMeasurementCanLimit) {
    const auto m = make_measurement_with_current(10.0f, std::nullopt, std::nullopt);
    EXPECT_TRUE(measurement_can_limit(m, AT + std::chrono::seconds(5), std::chrono::seconds(10)));
}

TEST(RedistributionCap, ReadingOlderThanMaxAgeCannotLimit) {
    const auto m = make_measurement_with_current(10.0f, std::nullopt, std::nullopt);
    EXPECT_FALSE(measurement_can_limit(m, AT + std::chrono::seconds(11), std::chrono::seconds(10)));
}

TEST(RedistributionCap, ReadingExactlyAtMaxAgeCannotLimit) {
    // The boundary is where two separately written staleness rules drift apart, so it is
    // pinned here: it must match what is_fresh() tells the aggregator about the same meter.
    const auto m = make_measurement_with_current(10.0f, std::nullopt, std::nullopt);
    const auto now = AT + std::chrono::seconds(10);
    EXPECT_FALSE(measurement_can_limit(m, now, std::chrono::seconds(10)));
    EXPECT_FALSE(is_fresh(m.measured_at, now, std::chrono::seconds(10)));
}

TEST(RedistributionCap, MaxAgeZeroAcceptsAnyAge) {
    const auto m = make_measurement_with_current(10.0f, std::nullopt, std::nullopt);
    EXPECT_TRUE(measurement_can_limit(m, AT + std::chrono::hours(5), std::chrono::seconds(0)));
}

TEST(RedistributionCap, FutureTimestampIsAccepted) {
    // Clock skew is not staleness.
    const auto m = make_measurement_with_current(10.0f, std::nullopt, std::nullopt);
    EXPECT_TRUE(measurement_can_limit(m, AT - std::chrono::seconds(30), std::chrono::seconds(10)));
}

// ---------------------------------------------------------------- per session context

TEST(RedistributionContext, ClearResetsRedistributionState) {
    BrokerContext context;
    context.redistribution_cap_A = PhaseCurrents{12.0f, 12.0f, 12.0f};
    context.redistribution_reduction_pending_since = AT;

    context.clear();

    EXPECT_FALSE(context.redistribution_cap_A.has_value());
    EXPECT_FALSE(context.redistribution_reduction_pending_since.has_value());
}

// ---------------------------------------------------------------- allocation limiting

TEST(PowerRedistributionBroker, AllocationIsLimitedToMeasuredPlusMargin) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});
    test::set_measurement_current(request.children[0], 10.0f, 10.0f, 10.0f, test::NOW_TS);

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 12.0f, 0.01f);
}

TEST(PowerRedistributionBroker, AllocationRisesByTheMarginEachRun) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});

    test::set_measurement_current(request.children[0], 10.0f, 10.0f, 10.0f, test::NOW_TS);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 12.0f, 0.01f);

    // The EV follows the allocation; the limit keeps one margin ahead of it.
    test::set_measurement_current(request.children[0], 12.0f, 12.0f, 12.0f, test::NOW_TS);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 14.0f, 0.01f);

    test::set_measurement_current(request.children[0], 14.0f, 14.0f, 14.0f, test::NOW_TS);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 16.0f, 0.01f);
}

TEST(PowerRedistributionBroker, AllocationNeverFallsBelowTheEvMinimum) {
    // An EV that momentarily draws nothing while Charging keeps its minimum: cutting it to
    // measured + margin = 2 A would end the charge instead of throttling it.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});
    test::set_measurement_current(request.children[0], 0.0f, 0.0f, 0.0f, test::NOW_TS);

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 6.0f, 0.01f);
}

TEST(PowerRedistributionBroker, MissingMeasurementCapsAtMinimum) {
    // A connector without any usable reading is limited to its minimum plus the margin
    // rather than left uncapped: a dead meter must not hold an allocation open.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    const auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 8.0f, 0.01f);
}

TEST(PowerRedistributionBroker, StaleMeasurementCapsAtMinimum) {
    // The reading's own timestamp is half an hour old: EnergyNode and EvseManager
    // republish the last reading they received, so age is the only thing separating a
    // frozen meter from a live one holding steady.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});
    test::set_measurement_current(request.children[0], 10.0f, 10.0f, 10.0f, "2026-08-04T12:00:00.000Z");

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 8.0f, 0.01f);
}

TEST(PowerRedistributionBroker, SinglePhaseMeterLimitsOnItsOnlyPhase) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});
    test::set_measurement_current(request.children[0], 16.0f, std::nullopt, std::nullopt, test::NOW_TS);

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 18.0f, 0.01f);
}

TEST(PowerRedistributionBroker, TotalPowerOnlyMeterLimitsTheAllocation) {
    // 4140 W over the three active phases at 230 V is 6 A per phase.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4140.0f, test::NOW_TS);

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 8.0f, 0.01f);
}

TEST(PowerRedistributionBroker, LimitSourceNamesTheRedistribution) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});
    test::set_measurement_current(request.children[0], 10.0f, 10.0f, 10.0f, test::NOW_TS);

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto results = impl.run_optimizer(request, AT);

    const auto limit = test::find_limit(results, "evse1");
    ASSERT_TRUE(limit.has_value());
    ASSERT_TRUE(limit.value().limits_root_side.ac_max_current_A.has_value());
    EXPECT_NE(limit.value().limits_root_side.ac_max_current_A.value().source.find("BrokerPowerRedistribution"),
              std::string::npos);
}

TEST(PowerRedistributionBroker, WattOnlyOfferIsNotLimited) {
    // A node offering only a watt limit (DC) is traded exactly like FastCharging trades
    // it: redistribution narrows AC current limits and nothing else.
    auto make_request = []() {
        auto dc = test::make_dc_evse_node("dc1", 11000.0f);
        auto request = test::make_root_node("grid", 40.0f, 22000.0f, {dc});
        test::set_measurement(request.children[0], 5000.0f, test::NOW_TS);
        return request;
    };

    EnergyManagerImpl redistributing(test::make_redistribution_config(),
                                     [](const std::vector<types::energy::EnforcedLimits>&) {});
    EnergyManagerImpl fast_charging(test::make_default_config(),
                                    [](const std::vector<types::energy::EnforcedLimits>&) {});

    const auto redistributed = test::find_limit(redistributing.run_optimizer(make_request(), AT), "dc1");
    const auto fast = test::find_limit(fast_charging.run_optimizer(make_request(), AT), "dc1");

    ASSERT_TRUE(redistributed.has_value());
    ASSERT_TRUE(fast.has_value());
    ASSERT_TRUE(redistributed.value().limits_root_side.total_power_W.has_value());
    ASSERT_TRUE(fast.value().limits_root_side.total_power_W.has_value());
    EXPECT_FLOAT_EQ(redistributed.value().limits_root_side.total_power_W.value().value,
                    fast.value().limits_root_side.total_power_W.value().value);
}

TEST(PowerRedistributionBroker, FastChargingStrategyIsNotLimitedByMeasurements) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});
    test::set_measurement_current(request.children[0], 10.0f, 10.0f, 10.0f, test::NOW_TS);

    EnergyManagerImpl impl(test::make_default_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 32.0f, 0.01f);
}

TEST(PowerRedistributionBroker, FutureSlotsAreNotLimited) {
    // The measurement describes now, so it limits the slot covering now; the later slots
    // stay at the full request - they are forecast the market still plans with.
    auto config = test::make_redistribution_config();
    config.schedule_interval_duration = 30;

    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});
    test::set_measurement_current(request.children[0], 10.0f, 10.0f, 10.0f, "2026-08-04T12:40:00.000Z");

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto at = Everest::Date::from_rfc3339("2026-08-04T12:40:00.000Z");
    const auto results = impl.run_optimizer(request, at);

    const auto limit = test::find_limit(results, "evse1");
    ASSERT_TRUE(limit.has_value());
    ASSERT_TRUE(limit.value().limits_root_side.ac_max_current_A.has_value());
    EXPECT_NEAR(limit.value().limits_root_side.ac_max_current_A.value().value, 12.0f, 0.01f);

    // The schedule ends with a slot after the active one; it must carry the full limit.
    const auto& schedule = limit.value().schedule;
    ASSERT_GT(schedule.size(), 1u);
    const auto& last = schedule.back();
    ASSERT_TRUE(last.limits_to_root.ac_max_current_A.has_value());
    EXPECT_NEAR(last.limits_to_root.ac_max_current_A.value().value, 32.0f, 0.01f);
}

// ---------------------------------------------------------------- session lifecycle

TEST(PowerRedistributionSession, StartLowerCapsBeforeAnyMeasurement) {
    // With start_with_lower_limit a session begins at min current + margin even before the
    // meter reports anything, and ramps from there.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    const auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 8.0f, 0.01f);
}

TEST(PowerRedistributionSession, StartUpperGivesTheFullAllocationUntilTheHoldExpires) {
    auto config = test::make_redistribution_config();
    config.redistribution_start_with_lower_limit = false;
    config.redistribution_reduction_hold_s = 30;

    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    const auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});

    // No measurement: the reduction to min + margin is pending from the first run but the
    // hold keeps the start value up while the EV could still be ramping.
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 32.0f, 0.01f);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT + std::chrono::seconds(29)), 32.0f, 0.01f);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT + std::chrono::seconds(31)), 8.0f, 0.01f);
}

TEST(PowerRedistributionSession, ReductionWaitsOutTheHoldAndRecoveryResetsIt) {
    auto config = test::make_redistribution_config();
    config.redistribution_reduction_hold_s = 30;

    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});

    test::set_measurement_current(request.children[0], 10.0f, 10.0f, 10.0f, test::NOW_TS);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 12.0f, 0.01f);

    // A dip below the limit does not reduce it while the hold runs.
    test::set_measurement_current(request.children[0], 5.0f, 5.0f, 5.0f, "2026-08-04T12:30:05.000Z");
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT + std::chrono::seconds(5)), 12.0f, 0.01f);

    // Consumption recovers: the pending reduction is dropped...
    test::set_measurement_current(request.children[0], 10.0f, 10.0f, 10.0f, "2026-08-04T12:30:10.000Z");
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT + std::chrono::seconds(10)), 12.0f, 0.01f);

    // ...so a later dip starts a fresh hold instead of inheriting the first one's age.
    test::set_measurement_current(request.children[0], 5.0f, 5.0f, 5.0f, "2026-08-04T12:30:40.000Z");
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT + std::chrono::seconds(40)), 12.0f, 0.01f);
}

TEST(PowerRedistributionSession, HoldZeroFollowsTheMeasurementDownImmediately) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});

    test::set_measurement_current(request.children[0], 10.0f, 10.0f, 10.0f, test::NOW_TS);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 12.0f, 0.01f);

    test::set_measurement_current(request.children[0], 5.0f, 5.0f, 5.0f, test::NOW_TS);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 7.0f, 0.01f);
}

TEST(PowerRedistributionSession, PauseLiftsTheCapAndAResumeStartsOver) {
    auto config = test::make_redistribution_config();
    config.redistribution_reduction_hold_s = 30;

    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});

    test::set_measurement_current(request.children[0], 20.0f, 20.0f, 20.0f, test::NOW_TS);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 22.0f, 0.01f);

    // A pause measures zero for a reason that says nothing about demand: no cap at all.
    request.children[0].evse_state = types::energy::EvseState::PausedEV;
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT + std::chrono::seconds(5)), 32.0f, 0.01f);

    // The resume is a fresh start: back to the start value immediately, not to the 22 A of
    // the earlier drawing phase held up by the reduction hold.
    request.children[0].evse_state = types::energy::EvseState::Charging;
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT + std::chrono::seconds(20)), 8.0f, 0.01f);
}

TEST(PowerRedistributionSession, NonChargingSessionIsNotLimited) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse});
    request.children[0].evse_state = types::energy::EvseState::WaitForAuth;

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 32.0f, 0.01f);
}

// ---------------------------------------------------------------- fairness under a shared fuse

TEST(PowerRedistributionFairness, SaturatedFuseIsSplitEqually) {
    // Both EVs draw everything they get; the caps sit above the fair share and must not
    // move it.
    auto evse1 = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto evse2 = test::make_evse_node("evse2", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse1, evse2});
    test::set_measurement_current(request.children[0], 20.0f, 20.0f, 20.0f, test::NOW_TS);
    test::set_measurement_current(request.children[1], 20.0f, 20.0f, 20.0f, test::NOW_TS);

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto results = impl.run_optimizer(request, AT);

    for (const auto* uuid : {"evse1", "evse2"}) {
        const auto limit = test::find_limit(results, uuid);
        ASSERT_TRUE(limit.has_value());
        ASSERT_TRUE(limit.value().limits_root_side.ac_max_current_A.has_value());
        EXPECT_NEAR(limit.value().limits_root_side.ac_max_current_A.value().value, 20.0f, 0.01f);
    }
}

TEST(PowerRedistributionFairness, JoiningSessionMakesTheOthersReduceEqually) {
    // Two sessions saturate the fuse. A third one plugging in gets its start allocation
    // in the very next run, and the two running sessions give up the difference in equal
    // parts - allocations are re-traded from zero every run, so nobody keeps a grandfathered
    // share. As the newcomer's consumption rises, all three converge on the equal share.
    auto evse1 = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto evse2 = test::make_evse_node("evse2", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 60.0f, std::nullopt, {evse1, evse2});
    test::set_measurement_current(request.children[0], 30.0f, 30.0f, 30.0f, test::NOW_TS);
    test::set_measurement_current(request.children[1], 30.0f, 30.0f, 30.0f, test::NOW_TS);

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});

    auto current_of = [&](const std::vector<types::energy::EnforcedLimits>& results, const char* uuid) {
        const auto limit = test::find_limit(results, uuid);
        if (not limit.has_value() or not limit.value().limits_root_side.ac_max_current_A.has_value()) {
            return -1.0f;
        }
        return limit.value().limits_root_side.ac_max_current_A.value().value;
    };

    // The fuse is saturated by the two running sessions.
    auto results = impl.run_optimizer(request, AT);
    EXPECT_NEAR(current_of(results, "evse1"), 30.0f, 0.01f);
    EXPECT_NEAR(current_of(results, "evse2"), 30.0f, 0.01f);

    // The third session starts (no measurement yet, start value min + margin = 8 A): the
    // running sessions drop to (60 - 8) / 2 = 26 A each, in the same run.
    request.children.push_back(test::make_evse_node("evse3", 32.0f, 6.0f));
    results = impl.run_optimizer(request, AT);
    EXPECT_NEAR(current_of(results, "evse1"), 26.0f, 0.01f);
    EXPECT_NEAR(current_of(results, "evse2"), 26.0f, 0.01f);
    EXPECT_NEAR(current_of(results, "evse3"), 8.0f, 0.01f);

    // The newcomer ramps up and the others' consumption follows their lowered limits:
    // everyone meets at the equal share of 20 A.
    test::set_measurement_current(request.children[0], 26.0f, 26.0f, 26.0f, test::NOW_TS);
    test::set_measurement_current(request.children[1], 26.0f, 26.0f, 26.0f, test::NOW_TS);
    test::set_measurement_current(request.children[2], 18.0f, 18.0f, 18.0f, test::NOW_TS);
    results = impl.run_optimizer(request, AT);
    EXPECT_NEAR(current_of(results, "evse1"), 20.0f, 0.01f);
    EXPECT_NEAR(current_of(results, "evse2"), 20.0f, 0.01f);
    EXPECT_NEAR(current_of(results, "evse3"), 20.0f, 0.01f);
}

TEST(PowerRedistributionFairness, UnderConsumingSessionFreesBudgetForTheOthers) {
    // This is the point of the strategy: without it both sessions would sit at 20 A and
    // the second EV could not use what the first one leaves idle.
    auto evse1 = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto evse2 = test::make_evse_node("evse2", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 40.0f, std::nullopt, {evse1, evse2});
    test::set_measurement_current(request.children[0], 8.0f, 8.0f, 8.0f, test::NOW_TS);
    test::set_measurement_current(request.children[1], 30.0f, 30.0f, 30.0f, test::NOW_TS);

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto results = impl.run_optimizer(request, AT);

    const auto limited = test::find_limit(results, "evse1");
    const auto distributed = test::find_limit(results, "evse2");
    ASSERT_TRUE(limited.has_value());
    ASSERT_TRUE(distributed.has_value());
    EXPECT_NEAR(limited.value().limits_root_side.ac_max_current_A.value().value, 10.0f, 0.01f);
    EXPECT_NEAR(distributed.value().limits_root_side.ac_max_current_A.value().value, 30.0f, 0.01f);
}

TEST(PowerRedistributionFairness, EveryConnectorKeepsItsMinimumWhenTheFuseIsTight) {
    auto evse1 = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto evse2 = test::make_evse_node("evse2", 32.0f, 6.0f);
    auto evse3 = test::make_evse_node("evse3", 32.0f, 6.0f);
    const auto request = test::make_root_node("grid", 18.0f, std::nullopt, {evse1, evse2, evse3});

    EnergyManagerImpl impl(test::make_redistribution_config(),
                           [](const std::vector<types::energy::EnforcedLimits>&) {});
    const auto results = impl.run_optimizer(request, AT);

    for (const auto* uuid : {"evse1", "evse2", "evse3"}) {
        const auto limit = test::find_limit(results, uuid);
        ASSERT_TRUE(limit.has_value());
        ASSERT_TRUE(limit.value().limits_root_side.ac_max_current_A.has_value());
        EXPECT_NEAR(limit.value().limits_root_side.ac_max_current_A.value().value, 6.0f, 0.01f);
    }
}

} // namespace module
