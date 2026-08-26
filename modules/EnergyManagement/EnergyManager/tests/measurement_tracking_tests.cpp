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
    EXPECT_FLOAT_EQ(measured.value(), 4200.0f);
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
    EXPECT_FLOAT_EQ(measured.value(), 4200.0f);
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
    EXPECT_FLOAT_EQ(measured.value(), 9000.0f);
}

// ---------------------------------------------------------------- per session context

TEST(MeasurementTrackingContext, ClearResetsTrackingActive) {
    BrokerContext context;
    EXPECT_FALSE(context.tracking_active);

    context.tracking_active = true;
    context.clear();

    EXPECT_FALSE(context.tracking_active);
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

// enforced_A = cap_W / (phases * voltage)
float expected_current_A(float cap_W, int phases = 3, float voltage = 230.0f) {
    return cap_W / (static_cast<float>(phases) * voltage);
}

EnergyManagerConfig make_tracking_config() {
    auto config = test::make_default_config();
    config.use_power_meter_tracking = true;
    config.power_meter_tracking_initial_current_A = 16.0;
    config.power_meter_tracking_margin_W = 200.0;
    return config;
}

const auto AT = Everest::Date::from_rfc3339("2026-08-04T12:30:00.000Z");

} // namespace

TEST(MeasurementTrackingBroker, FirstRunRequestsInitialCurrent) {
    // Static fuse allows 32A, so the 16A initial current must be the binding constraint.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    // cap = 16A * 3ph * 230V = 11040W -> 16.0A
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 16.0f, 0.01f);
}

TEST(MeasurementTrackingBroker, SecondRunTracksMeasuredPlusMargin) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    // First run primes the context with the initial current.
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 16.0f, 0.01f);

    // The EV draws 10000W. Tracking limit is 10000 + 200 = 10200W, comfortably above the
    // minimum current floor (6A * 690 = 4140W), so the measurement is what binds.
    test::set_measurement(request.children[0], 10000.0f);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), expected_current_A(10200.0f), 0.01f);
}

TEST(MeasurementTrackingBroker, TrackingLimitIsFlooredAtMinimumCurrent) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    run_and_get_current(impl, request, "evse1", AT);

    // measured + margin = 3200W is below the 6A minimum (4140W). The connector must be held
    // at its minimum current, not starved to 0A.
    test::set_measurement(request.children[0], 3000.0f);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 6.0f, 0.01f);
}

TEST(MeasurementTrackingBroker, DisabledFlagLeavesStaticBehaviourUnchanged) {
    auto config = test::make_default_config();
    config.use_power_meter_tracking = false;

    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});
    // A measurement is present but must be ignored entirely.
    test::set_measurement(request.children[0], 3000.0f);

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 32.0f, 0.01f);
}

TEST(MeasurementTrackingBroker, MissingMeasurementFallsBackToStaticLimit) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    // First run: initial current.
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 16.0f, 0.01f);
    // Second run with still no measurement: fall back to the static limit rather than
    // starving the connector.
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 32.0f, 0.01f);
}

TEST(MeasurementTrackingBroker, TracksPerConnectorIndependently) {
    auto evse1 = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto evse2 = test::make_evse_node("evse2", 32.0f, 6.0f);
    // 63A root leaves room for both connectors at once, so neither constrains the other.
    auto request = test::make_root_node("grid", 63.0f, std::nullopt, {evse1, evse2});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    // Prime both connectors.
    impl.run_optimizer(request, AT);

    // Only connector 1 reports a measurement; connector 2 reports none.
    test::set_measurement(request.children[0], 10000.0f);

    const auto results = impl.run_optimizer(request, AT);
    const auto l1 = test::find_limit(results, "evse1");
    const auto l2 = test::find_limit(results, "evse2");
    ASSERT_TRUE(l1.has_value());
    ASSERT_TRUE(l2.has_value());

    EXPECT_NEAR(l1.value().limits_root_side.ac_max_current_A.value().value, expected_current_A(10200.0f), 0.01f);
    EXPECT_NEAR(l2.value().limits_root_side.ac_max_current_A.value().value, 32.0f, 0.01f);
}

TEST(MeasurementTrackingBroker, InitialCurrentSurvivesUnpluggedRuns) {
    // The optimizer runs continuously and builds a broker for every EVSE on every run.
    // Runs while the connector is Unplugged must not consume the initial-current flag,
    // or a real session would start at the minimum current instead.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    evse.evse_state = types::energy::EvseState::Unplugged;
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    // Several idle cycles while nothing is plugged in.
    impl.run_optimizer(request, AT);
    impl.run_optimizer(request, AT);

    // An EV plugs in and starts charging; no measurement exists yet.
    request.children[0].evse_state = types::energy::EvseState::Charging;
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 16.0f, 0.01f);
}

TEST(MeasurementTrackingBroker, ReplugRestartsWithInitialCurrent) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    // A full session: initial current, then tracking converges.
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 16.0f, 0.01f);
    test::set_measurement(request.children[0], 10000.0f);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), expected_current_A(10200.0f), 0.01f);

    // The EV unplugs; the meter drops to zero.
    request.children[0].evse_state = types::energy::EvseState::Unplugged;
    test::set_measurement(request.children[0], 0.0f);
    impl.run_optimizer(request, AT);

    // A new session must start from the initial current again, not from measured+margin.
    request.children[0].evse_state = types::energy::EvseState::Charging;
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 16.0f, 0.01f);
}

TEST(MeasurementTrackingBroker, SinglePhaseSessionHonoursInitialCurrent) {
    // A three phase EVSE currently charging a single phase vehicle. The budget is priced
    // the way the trading engine prices purchases (at ac_max_phase_count), so the initial
    // current still comes out as exactly the configured value.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    evse.schedule_import[0].limits_to_root.ac_number_of_active_phases = 1;
    const auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 16.0f, 0.01f);
}

TEST(MeasurementTrackingBroker, SinglePhaseActiveSessionTracksItsRealDraw) {
    // The measurement is physical watts on one phase, but the engine prices purchases at
    // three. Without scaling the measurement up, a single phase EV drawing 16A (3680W)
    // would be given a budget the engine reads as ~5.3A and be tracked down to the
    // minimum current while drawing full power.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    evse.schedule_import[0].limits_to_root.ac_number_of_active_phases = 1;
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    impl.run_optimizer(request, AT);

    // 16A on one phase at 230V.
    test::set_measurement(request.children[0], 3680.0f);

    // budget = 3680 * 3 + 200 = 11240W -> 11240 / 690 = 16.29A: the EV keeps its draw
    // plus the margin, instead of collapsing to the 6A floor.
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 16.29f, 0.05f);
}

TEST(MeasurementTrackingBroker, NegativeMeasurementIsFlooredAtMinimumCurrent) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    impl.run_optimizer(request, AT);

    // A bidirectional meter briefly reporting export must not produce a negative budget.
    test::set_measurement(request.children[0], -500.0f);
    EXPECT_NEAR(run_and_get_current(impl, request, "evse1", AT), 6.0f, 0.01f);
}

TEST(MeasurementTrackingBroker, WattOnlyNodeIsNotStarved) {
    // A node declaring only a watt limit (typically DC) has no meaningful amps-to-watts
    // conversion; tracking must leave it on its static limits rather than capping it with
    // an AC-derived budget or starving it to zero.
    types::energy::EnergyFlowRequest evse;
    evse.uuid = "dc1";
    evse.node_type = types::energy::NodeType::Evse;
    evse.evse_state = types::energy::EvseState::Charging;
    types::energy::ScheduleReqEntry entry;
    entry.timestamp = "2026-08-04T12:00:00.000Z";
    entry.limits_to_root.total_power_W = {11000.0f, "TEST_dc"};
    evse.schedule_import = {entry};
    types::energy::ScheduleReqEntry none = entry;
    none.limits_to_root.total_power_W = {0.0f, "TEST_dc"};
    evse.schedule_export = {none};

    auto request = test::make_root_node("grid", 63.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    // Two runs: neither the initial-current logic nor the measured branch may apply.
    impl.run_optimizer(request, AT);
    const auto results = impl.run_optimizer(request, AT);
    const auto limit = test::find_limit(results, "dc1");
    ASSERT_TRUE(limit.has_value());
    ASSERT_TRUE(limit.value().limits_root_side.total_power_W.has_value());
    EXPECT_NEAR(limit.value().limits_root_side.total_power_W.value().value, 11000.0f, 1.0f);
}

TEST(MeasurementTrackingBroker, MultiSlotScheduleCapsEverySlot) {
    auto config = make_tracking_config();
    // Two 30 minute slots inside the 1h forecast.
    config.schedule_interval_duration = 30;

    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(config, [](const std::vector<types::energy::EnforcedLimits>&) {});

    impl.run_optimizer(request, AT);
    test::set_measurement(request.children[0], 10000.0f);

    const auto results = impl.run_optimizer(request, AT);
    const auto limit = test::find_limit(results, "evse1");
    ASSERT_TRUE(limit.has_value());
    // The slot grid holds the 30 minute intervals plus the request's own timestamps, so
    // only a lower bound is asserted; what matters is that every slot is capped.
    ASSERT_GE(limit.value().schedule.size(), 2U);

    // Each slot is an independent time interval with the full per-interval budget.
    for (const auto& slot : limit.value().schedule) {
        ASSERT_TRUE(slot.limits_to_root.total_power_W.has_value());
        EXPECT_NEAR(slot.limits_to_root.total_power_W.value().value, 10200.0f, 1.0f);
    }
}

TEST(MeasurementTrackingBroker, TighterStaticWattLimitWins) {
    // The node itself declares a 5000W limit; the tracking budget (10200W) must not
    // widen it.
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f, 5000.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    impl.run_optimizer(request, AT);
    test::set_measurement(request.children[0], 10000.0f);

    const auto results = impl.run_optimizer(request, AT);
    const auto limit = test::find_limit(results, "evse1");
    ASSERT_TRUE(limit.has_value());
    ASSERT_TRUE(limit.value().limits_root_side.total_power_W.has_value());
    EXPECT_NEAR(limit.value().limits_root_side.total_power_W.value().value, 5000.0f, 1.0f);
}

TEST(MeasurementTrackingBroker, LooserStaticWattLimitDoesNotBind) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f, 20000.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    impl.run_optimizer(request, AT);
    test::set_measurement(request.children[0], 10000.0f);

    const auto results = impl.run_optimizer(request, AT);
    const auto limit = test::find_limit(results, "evse1");
    ASSERT_TRUE(limit.has_value());
    EXPECT_NEAR(limit.value().limits_root_side.total_power_W.value().value, 10200.0f, 1.0f);
}

TEST(MeasurementTrackingBroker, TotalPurchaseEqualsTheTrackingLimit) {
    auto evse = test::make_evse_node("evse1", 32.0f, 6.0f);
    auto request = test::make_root_node("grid", 32.0f, std::nullopt, {evse});

    EnergyManagerImpl impl(make_tracking_config(), [](const std::vector<types::energy::EnforcedLimits>&) {});

    impl.run_optimizer(request, AT);
    test::set_measurement(request.children[0], 10000.0f);

    const auto results = impl.run_optimizer(request, AT);
    const auto limit = test::find_limit(results, "evse1");
    ASSERT_TRUE(limit.has_value());
    ASSERT_TRUE(limit.value().limits_root_side.total_power_W.has_value());

    // The remaining-budget mechanism must converge on exactly the cap across trading rounds,
    // neither stopping short of it nor overshooting it.
    EXPECT_NEAR(limit.value().limits_root_side.total_power_W.value().value, 10200.0f, 1.0f);
}

} // namespace module
