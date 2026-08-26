// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <utils/date.hpp>

#include "BrokerMeasurementTracking.hpp"
#include "EnergyManagerTestHelpers.hpp"

namespace module {

// ---------------------------------------------------------------- measurement extraction

TEST(MeasurementTrackingHelpers, NoMeasurementReturnsNullopt) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    EXPECT_FALSE(get_measured_power_W(evse).has_value());
}

TEST(MeasurementTrackingHelpers, ReadsLeavesMeasurement) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    test::set_measurement(evse, 4200.0f);

    const auto measured = get_measured_power_W(evse);
    ASSERT_TRUE(measured.has_value());
    EXPECT_FLOAT_EQ(measured.value().total, 4200.0f);
}

TEST(MeasurementTrackingHelpers, PrefersLeavesOverRoot) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);

    types::powermeter::Powermeter root;
    root.timestamp = "2026-08-04T12:00:00.000Z";
    root.energy_Wh_import.total = 0.0f;
    types::units::Power root_power;
    root_power.total = 9000.0f;
    root.power_W = root_power;
    evse.energy_usage_root = root;

    test::set_measurement(evse, 4200.0f);

    const auto measured = get_measured_power_W(evse);
    ASSERT_TRUE(measured.has_value());
    EXPECT_FLOAT_EQ(measured.value().total, 4200.0f);
}

TEST(MeasurementTrackingHelpers, FallsBackToRootMeasurement) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);

    types::powermeter::Powermeter root;
    root.timestamp = "2026-08-04T12:00:00.000Z";
    root.energy_Wh_import.total = 0.0f;
    types::units::Power root_power;
    root_power.total = 9000.0f;
    root.power_W = root_power;
    evse.energy_usage_root = root;

    const auto measured = get_measured_power_W(evse);
    ASSERT_TRUE(measured.has_value());
    EXPECT_FLOAT_EQ(measured.value().total, 9000.0f);
}

TEST(MeasurementTrackingHelpers, PreservesPerPhasePower) {
    // A meter reporting per-phase power must not be collapsed to the total.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    test::set_measurement(evse, 4200.0f);
    evse.energy_usage_leaves.value().power_W.value().L1 = 1400.0f;
    evse.energy_usage_leaves.value().power_W.value().L2 = 1300.0f;
    evse.energy_usage_leaves.value().power_W.value().L3 = 1500.0f;

    const auto measured = get_measured_power_W(evse);
    ASSERT_TRUE(measured.has_value());
    EXPECT_FLOAT_EQ(measured.value().total, 4200.0f);
    ASSERT_TRUE(measured.value().L1.has_value());
    EXPECT_FLOAT_EQ(measured.value().L1.value(), 1400.0f);
    ASSERT_TRUE(measured.value().L2.has_value());
    EXPECT_FLOAT_EQ(measured.value().L2.value(), 1300.0f);
    ASSERT_TRUE(measured.value().L3.has_value());
    EXPECT_FLOAT_EQ(measured.value().L3.value(), 1500.0f);
}

// ---------------------------------------------------------------- per-phase current extraction

// WP1.b preparation: phase imbalance handling needs the measurement per phase (in ampere,
// matching the asymmetry threshold), not just the total power.

TEST(MeasurementTrackingHelpers, NoCurrentMeasurementReturnsAllNullopt) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);

    const auto measured = get_measured_current_A(evse);
    EXPECT_FALSE(measured.L1.has_value());
    EXPECT_FALSE(measured.L2.has_value());
    EXPECT_FALSE(measured.L3.has_value());
}

TEST(MeasurementTrackingHelpers, ReadsLeavesPerPhaseCurrent) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    test::set_measurement_current(evse, 10.0f, 11.0f, 12.0f);

    const auto measured = get_measured_current_A(evse);
    ASSERT_TRUE(measured.L1.has_value());
    ASSERT_TRUE(measured.L2.has_value());
    ASSERT_TRUE(measured.L3.has_value());
    EXPECT_FLOAT_EQ(measured.L1.value(), 10.0f);
    EXPECT_FLOAT_EQ(measured.L2.value(), 11.0f);
    EXPECT_FLOAT_EQ(measured.L3.value(), 12.0f);
}

TEST(MeasurementTrackingHelpers, PartialPhaseCurrentIsPreserved) {
    // A single-phase meter reports only L1; the other phases must stay unknown, not zero.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    test::set_measurement_current(evse, 16.0f, std::nullopt, std::nullopt);

    const auto measured = get_measured_current_A(evse);
    ASSERT_TRUE(measured.L1.has_value());
    EXPECT_FLOAT_EQ(measured.L1.value(), 16.0f);
    EXPECT_FALSE(measured.L2.has_value());
    EXPECT_FALSE(measured.L3.has_value());
}

TEST(MeasurementTrackingHelpers, PrefersLeavesCurrentOverRoot) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);

    types::powermeter::Powermeter root;
    root.timestamp = "2026-08-04T12:00:00.000Z";
    root.energy_Wh_import.total = 0.0f;
    types::units::Current root_current;
    root_current.L1 = 1.0f;
    root_current.L2 = 2.0f;
    root_current.L3 = 3.0f;
    root.current_A = root_current;
    evse.energy_usage_root = root;

    test::set_measurement_current(evse, 10.0f, 11.0f, 12.0f);

    const auto measured = get_measured_current_A(evse);
    ASSERT_TRUE(measured.L1.has_value());
    EXPECT_FLOAT_EQ(measured.L1.value(), 10.0f);
}

TEST(MeasurementTrackingHelpers, FallsBackToRootCurrent) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);

    types::powermeter::Powermeter root;
    root.timestamp = "2026-08-04T12:00:00.000Z";
    root.energy_Wh_import.total = 0.0f;
    types::units::Current root_current;
    root_current.L1 = 1.0f;
    root_current.L2 = 2.0f;
    root_current.L3 = 3.0f;
    root.current_A = root_current;
    evse.energy_usage_root = root;

    const auto measured = get_measured_current_A(evse);
    ASSERT_TRUE(measured.L1.has_value());
    ASSERT_TRUE(measured.L2.has_value());
    ASSERT_TRUE(measured.L3.has_value());
    EXPECT_FLOAT_EQ(measured.L1.value(), 1.0f);
    EXPECT_FLOAT_EQ(measured.L2.value(), 2.0f);
    EXPECT_FLOAT_EQ(measured.L3.value(), 3.0f);
}

// ---------------------------------------------------------------- per session context

TEST(MeasurementTrackingContext, ClearResetsWarnedFlag) {
    BrokerContext context;
    EXPECT_FALSE(context.tracking_warned_no_measurement);

    context.tracking_warned_no_measurement = true;
    context.clear();

    EXPECT_FALSE(context.tracking_warned_no_measurement);
}

TEST(MeasurementTrackingContext, ClearResetsObservedMeasurement) {
    BrokerContext context;
    types::units::Power power;
    power.total = 4200.0f;
    context.last_observed_measurement.power_W = power;
    context.last_observed_measurement.current_A.L1 = 16.0f;
    context.last_observed_measurement.current_A.L2 = 15.0f;
    context.last_observed_measurement.current_A.L3 = 14.0f;

    context.clear();

    EXPECT_FALSE(context.last_observed_measurement.power_W.has_value());
    EXPECT_FALSE(context.last_observed_measurement.current_A.L1.has_value());
    EXPECT_FALSE(context.last_observed_measurement.current_A.L2.has_value());
    EXPECT_FALSE(context.last_observed_measurement.current_A.L3.has_value());
}

// ---------------------------------------------------------------- broker behaviour

namespace {

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

EnergyManagerConfig make_tracking_config() {
    auto config = test::make_default_config();
    config.broker_strategy = "PowerRedistribution";
    return config;
}

const auto AT = Everest::Date::from_rfc3339("2026-08-04T12:30:00.000Z");

} // namespace

// The load-bearing guarantee: the tracking broker only observes. With or without a
// measurement, allocations must be exactly what BrokerFastCharging produces.

TEST(MeasurementTrackingBroker, MeasurementDoesNotAlterAllocation) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    // The EV draws far less than the static limit; the allocation must stay at the limit.
    test::set_measurement(request.children[0], 3000.0f);

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 32.0f, 0.01f);
    // Repeated runs with a measurement present must not converge anywhere either.
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 32.0f, 0.01f);
}

TEST(MeasurementTrackingBroker, MissingMeasurementDoesNotAlterAllocation) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 32.0f, 0.01f);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 32.0f, 0.01f);
}

TEST(MeasurementTrackingBroker, EquivalentToFastChargingUnderSharedFuse) {
    // Two connectors competing for a fuse that cannot serve both fully: the sharing
    // outcome must be identical whether tracking is enabled or not, measurement present
    // or not.
    auto make_request = []() {
        auto evse1 = test::make_evse_node("evse1", 32.0f, 6.0f);
        auto evse2 = test::make_evse_node("evse2", 32.0f, 6.0f);
        return test::make_root_node("grid", 40.0f, std::nullopt, {evse1, evse2});
    };

    auto tracked_request = make_request();
    test::set_measurement(tracked_request.children[0], 10000.0f);
    auto static_request = make_request();
    test::set_measurement(static_request.children[0], 10000.0f);

    EnergyManagerImpl tracked(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    EnergyManagerImpl statik(test::make_default_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    for (int run = 0; run < 2; run++) {
        const auto tracked_results = tracked.run_optimizer(tracked_request, AT);
        const auto static_results = statik.run_optimizer(static_request, AT);

        for (const auto* uuid : {"evse1", "evse2"}) {
            const auto t = test::find_limit(tracked_results, uuid);
            const auto s = test::find_limit(static_results, uuid);
            ASSERT_TRUE(t.has_value());
            ASSERT_TRUE(s.has_value());
            ASSERT_TRUE(t.value().limits_root_side.ac_max_current_A.has_value());
            ASSERT_TRUE(s.value().limits_root_side.ac_max_current_A.has_value());
            EXPECT_FLOAT_EQ(t.value().limits_root_side.ac_max_current_A.value().value,
                            s.value().limits_root_side.ac_max_current_A.value().value);
        }
    }
}

// ---------------------------------------------------------------- observation

// The observation itself: the broker must actually read the power meter during a run.

TEST(MeasurementTrackingBroker, ObservesMeasuredPowerDuringRun) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4200.0f);

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, AT);

    const auto observed = impl.get_observed_measurement("evse1");
    ASSERT_TRUE(observed.power_W.has_value());
    EXPECT_FLOAT_EQ(observed.power_W.value().total, 4200.0f);
}

TEST(MeasurementTrackingBroker, ObservationFollowsTheMeter) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    test::set_measurement(request.children[0], 4200.0f);
    impl.run_optimizer(request, AT);
    test::set_measurement(request.children[0], 6900.0f);
    impl.run_optimizer(request, AT);

    const auto observed = impl.get_observed_measurement("evse1");
    ASSERT_TRUE(observed.power_W.has_value());
    EXPECT_FLOAT_EQ(observed.power_W.value().total, 6900.0f);
}

TEST(MeasurementTrackingBroker, NoObservationWithoutMeasurement) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, AT);

    EXPECT_FALSE(impl.get_observed_measurement("evse1").power_W.has_value());
}

TEST(MeasurementTrackingBroker, ObservationClearedOnUnplug) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4200.0f);

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, AT);
    ASSERT_TRUE(impl.get_observed_measurement("evse1").power_W.has_value());

    request.children[0].evse_state = types::energy::EvseState::Unplugged;
    impl.run_optimizer(request, AT);

    EXPECT_FALSE(impl.get_observed_measurement("evse1").power_W.has_value());
}

TEST(MeasurementTrackingBroker, ObservesPerPhaseCurrentDuringRun) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement_current(request.children[0], 10.0f, 11.0f, 12.0f);

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, AT);

    const auto observed = impl.get_observed_measurement("evse1").current_A;
    ASSERT_TRUE(observed.L1.has_value());
    ASSERT_TRUE(observed.L2.has_value());
    ASSERT_TRUE(observed.L3.has_value());
    EXPECT_FLOAT_EQ(observed.L1.value(), 10.0f);
    EXPECT_FLOAT_EQ(observed.L2.value(), 11.0f);
    EXPECT_FLOAT_EQ(observed.L3.value(), 12.0f);
}

TEST(MeasurementTrackingBroker, PerPhaseObservationFollowsTheMeter) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    test::set_measurement_current(request.children[0], 10.0f, 11.0f, 12.0f);
    impl.run_optimizer(request, AT);
    test::set_measurement_current(request.children[0], 6.0f, std::nullopt, std::nullopt);
    impl.run_optimizer(request, AT);

    const auto observed = impl.get_observed_measurement("evse1").current_A;
    ASSERT_TRUE(observed.L1.has_value());
    EXPECT_FLOAT_EQ(observed.L1.value(), 6.0f);
    // Stale phases must not survive from the previous run.
    EXPECT_FALSE(observed.L2.has_value());
    EXPECT_FALSE(observed.L3.has_value());
}

TEST(MeasurementTrackingBroker, PerPhaseObservationClearedOnUnplug) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement_current(request.children[0], 10.0f, 11.0f, 12.0f);

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, AT);
    ASSERT_TRUE(impl.get_observed_measurement("evse1").current_A.L1.has_value());

    request.children[0].evse_state = types::energy::EvseState::Unplugged;
    impl.run_optimizer(request, AT);

    const auto observed = impl.get_observed_measurement("evse1").current_A;
    EXPECT_FALSE(observed.L1.has_value());
    EXPECT_FALSE(observed.L2.has_value());
    EXPECT_FALSE(observed.L3.has_value());
}

TEST(MeasurementTrackingBroker, NoPerPhaseObservationWhenTrackingDisabled) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement_current(request.children[0], 10.0f, 11.0f, 12.0f);

    EnergyManagerImpl impl(test::make_default_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, AT);

    const auto observed = impl.get_observed_measurement("evse1").current_A;
    EXPECT_FALSE(observed.L1.has_value());
    EXPECT_FALSE(observed.L2.has_value());
    EXPECT_FALSE(observed.L3.has_value());
}

TEST(MeasurementTrackingBroker, PerPhaseCurrentDoesNotAlterAllocation) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    // The EV draws far less per phase than the static limit; allocation must stay at the limit.
    test::set_measurement_current(request.children[0], 6.0f, 6.0f, 6.0f);

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 32.0f, 0.01f);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 32.0f, 0.01f);
}

TEST(MeasurementTrackingBroker, NoObservationWhenTrackingDisabled) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4200.0f);

    EnergyManagerImpl impl(test::make_default_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});
    impl.run_optimizer(request, AT);

    EXPECT_FALSE(impl.get_observed_measurement("evse1").power_W.has_value());
}

TEST(MeasurementTrackingBroker, FastChargingStrategyLeavesStaticBehaviourUnchanged) {
    auto config = test::make_default_config();
    config.broker_strategy = "FastCharging";

    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 3000.0f);

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 32.0f, 0.01f);
}

// ---------------------------------------------------------------- strategy selection

TEST(MeasurementTrackingBroker, UnknownStrategyFallsBackToFastCharging) {
    // A misspelled or future strategy value must not break energy distribution: it behaves
    // like the default FastCharging broker and observes nothing.
    auto config = test::make_default_config();
    config.broker_strategy = "NoSuchStrategy";

    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    test::set_measurement(request.children[0], 4200.0f);

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 32.0f, 0.01f);
    EXPECT_FALSE(impl.get_observed_measurement("evse1").power_W.has_value());
}

} // namespace module
