// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <optional>

#include <iso15118/d20/limits.hpp>
#include <iso15118/message/common_types.hpp>

#include "der_setup.hpp"

namespace dt = iso15118::message_20::datatypes;

namespace {

constexpr float NOMINAL_VOLTAGE_V = 230.0f;

bool equal(const dt::RationalNumber& a, const dt::RationalNumber& b) {
    return a.value == b.value and a.exponent == b.exponent;
}

iso15118::d20::AcTransferLimits make_ac_limits(float charge_max, std::optional<float> discharge_max) {
    iso15118::d20::AcTransferLimits ac{};
    ac.charge_power.max = dt::from_float(charge_max);
    ac.charge_power.min = dt::from_float(0.0f);
    ac.nominal_frequency = dt::from_float(50.0f);
    if (discharge_max.has_value()) {
        iso15118::d20::Limit<dt::RationalNumber> discharge{};
        discharge.max = dt::from_float(discharge_max.value());
        discharge.min = dt::from_float(0.0f);
        ac.discharge_power = discharge;
    }
    return ac;
}

} // namespace

TEST(DerSetupTest, nominal_charge_equals_ac_charge_max) {
    const auto ac = make_ac_limits(7400.0f, 11000.0f);

    const auto limits = module::build_iec_der_transfer_limits(ac);

    EXPECT_TRUE(equal(limits.nominal_charge_power, ac.charge_power.max));
}

TEST(DerSetupTest, discharge_present_passes_through_to_both_fields) {
    const auto ac = make_ac_limits(11000.0f, 11000.0f);

    const auto limits = module::build_iec_der_transfer_limits(ac);

    EXPECT_TRUE(equal(limits.nominal_discharge_power, ac.discharge_power->max));
    EXPECT_TRUE(equal(limits.max_discharge_power, ac.discharge_power->max));
}

TEST(DerSetupTest, discharge_absent_yields_zero_discharge) {
    const auto ac = make_ac_limits(11000.0f, std::nullopt);

    const auto limits = module::build_iec_der_transfer_limits(ac);

    EXPECT_TRUE(equal(limits.nominal_charge_power, ac.charge_power.max));
    EXPECT_TRUE(equal(limits.nominal_discharge_power, dt::from_float(0.0f)));
    EXPECT_TRUE(equal(limits.max_discharge_power, dt::from_float(0.0f)));
}

TEST(DerSetupTest, negative_ac_discharge_yields_negative_outputs) {
    const auto ac = make_ac_limits(11000.0f, -11000.0f);

    const auto limits = module::build_iec_der_transfer_limits(ac);

    EXPECT_TRUE(equal(limits.nominal_discharge_power, ac.discharge_power->max));
    EXPECT_TRUE(equal(limits.max_discharge_power, ac.discharge_power->max));
    EXPECT_LT(dt::from_RationalNumber(limits.max_discharge_power), 0.0f);
    // Charge stays positive.
    EXPECT_TRUE(equal(limits.nominal_charge_power, ac.charge_power.max));
    EXPECT_GT(dt::from_RationalNumber(limits.nominal_charge_power), 0.0f);
}

TEST(DerSetupTest, positive_ac_discharge_yields_positive_outputs) {
    const auto ac = make_ac_limits(11000.0f, 11000.0f);

    const auto limits = module::build_iec_der_transfer_limits(ac);

    EXPECT_GT(dt::from_RationalNumber(limits.max_discharge_power), 0.0f);
    EXPECT_GT(dt::from_RationalNumber(limits.nominal_discharge_power), 0.0f);
}

TEST(DerSetupTest, sae_charge_only_yields_zero_max_discharge) {
    const auto ac = make_ac_limits(7400.0f, std::nullopt);

    const auto limits = module::build_sae_der_transfer_limits(ac, std::nullopt, 230u);

    ASSERT_TRUE(limits.nominal_charge_power.has_value());
    ASSERT_TRUE(limits.nominal_discharge_power.has_value());
    EXPECT_TRUE(equal(limits.nominal_charge_power.value(), ac.charge_power.max));
    EXPECT_TRUE(equal(limits.nominal_discharge_power.value(), dt::from_float(0.0f)));
    EXPECT_TRUE(equal(limits.max_discharge_power, dt::from_float(0.0f)));
}

// Distinct charge and discharge magnitudes, so swapping the two sources fails this test.
TEST(DerSetupTest, sae_discharge_present_matches_nominal_and_max) {
    const auto ac = make_ac_limits(7400.0f, 11000.0f);

    const auto limits = module::build_sae_der_transfer_limits(ac, std::nullopt, 230u);

    ASSERT_TRUE(limits.nominal_charge_power.has_value());
    ASSERT_TRUE(limits.nominal_discharge_power.has_value());
    EXPECT_TRUE(equal(limits.nominal_charge_power.value(), ac.charge_power.max));
    EXPECT_TRUE(equal(limits.nominal_discharge_power.value(), ac.discharge_power->max));
    EXPECT_TRUE(equal(limits.max_discharge_power, ac.discharge_power->max));
    EXPECT_TRUE(equal(limits.nominal_discharge_power.value(), limits.max_discharge_power));
}

TEST(DerSetupTest, sae_negative_discharge_yields_negative_outputs) {
    const auto ac = make_ac_limits(7400.0f, -11000.0f);

    const auto limits = module::build_sae_der_transfer_limits(ac, std::nullopt, 230u);

    ASSERT_TRUE(limits.nominal_discharge_power.has_value());
    EXPECT_TRUE(equal(limits.nominal_discharge_power.value(), ac.discharge_power->max));
    EXPECT_TRUE(equal(limits.max_discharge_power, ac.discharge_power->max));
    EXPECT_LT(dt::from_RationalNumber(limits.max_discharge_power), 0.0f);
    // Charge stays positive.
    EXPECT_GT(dt::from_RationalNumber(limits.nominal_charge_power.value()), 0.0f);
}

// Absorption is non-negative, injection is non-positive.
TEST(DerSetupTest, sae_reactive_power_injection_is_negative_absorption_positive) {
    const auto ac = make_ac_limits(7400.0f, 11000.0f);

    const auto limits = module::build_sae_der_transfer_limits(ac, 5000.0f, 230u);

    const auto& reactive = limits.reactive_power_limits;
    EXPECT_TRUE(equal(reactive.maximum_var_absorption_during_charging, dt::RationalNumber{5000, 0}));
    EXPECT_TRUE(equal(reactive.maximum_var_absorption_during_discharging, dt::RationalNumber{5000, 0}));
    EXPECT_TRUE(equal(reactive.maximum_var_injection_during_charging, dt::RationalNumber{-5000, 0}));
    EXPECT_TRUE(equal(reactive.maximum_var_injection_during_discharging, dt::RationalNumber{-5000, 0}));

    EXPECT_GT(dt::from_RationalNumber(reactive.maximum_var_absorption_during_charging), 0.0f);
    EXPECT_LT(dt::from_RationalNumber(reactive.maximum_var_injection_during_charging), 0.0f);

    EXPECT_FALSE(reactive.maximum_var_absorption_during_charging_L2.has_value());
    EXPECT_FALSE(reactive.maximum_var_absorption_during_charging_L3.has_value());
    EXPECT_FALSE(reactive.maximum_var_injection_during_charging_L2.has_value());
    EXPECT_FALSE(reactive.maximum_var_injection_during_charging_L3.has_value());
    EXPECT_FALSE(reactive.maximum_var_absorption_during_discharging_L2.has_value());
    EXPECT_FALSE(reactive.maximum_var_absorption_during_discharging_L3.has_value());
    EXPECT_FALSE(reactive.maximum_var_injection_during_discharging_L2.has_value());
    EXPECT_FALSE(reactive.maximum_var_injection_during_discharging_L3.has_value());
}

// A negative configured capability is still absorption-positive, injection-negative.
TEST(DerSetupTest, sae_negative_reactive_power_capability_uses_magnitude) {
    const auto ac = make_ac_limits(7400.0f, 11000.0f);

    const auto limits = module::build_sae_der_transfer_limits(ac, -5000.0f, 230u);

    const auto& reactive = limits.reactive_power_limits;
    EXPECT_TRUE(equal(reactive.maximum_var_absorption_during_charging, dt::RationalNumber{5000, 0}));
    EXPECT_TRUE(equal(reactive.maximum_var_injection_during_charging, dt::RationalNumber{-5000, 0}));
}

TEST(DerSetupTest, sae_absent_reactive_power_yields_explicit_zeros) {
    const auto ac = make_ac_limits(7400.0f, 11000.0f);

    const auto limits = module::build_sae_der_transfer_limits(ac, std::nullopt, 230u);

    const auto& reactive = limits.reactive_power_limits;
    EXPECT_TRUE(equal(reactive.maximum_var_absorption_during_charging, dt::RationalNumber{0, 0}));
    EXPECT_TRUE(equal(reactive.maximum_var_injection_during_charging, dt::RationalNumber{0, 0}));
    EXPECT_TRUE(equal(reactive.maximum_var_absorption_during_discharging, dt::RationalNumber{0, 0}));
    EXPECT_TRUE(equal(reactive.maximum_var_injection_during_discharging, dt::RationalNumber{0, 0}));
}

TEST(DerSetupTest, sae_zero_reactive_power_matches_absent) {
    const auto ac = make_ac_limits(7400.0f, 11000.0f);

    const auto zero = module::build_sae_der_transfer_limits(ac, 0.0f, 230u);
    const auto absent = module::build_sae_der_transfer_limits(ac, std::nullopt, 230u);

    EXPECT_TRUE(equal(zero.reactive_power_limits.maximum_var_absorption_during_charging,
                      absent.reactive_power_limits.maximum_var_absorption_during_charging));
    EXPECT_TRUE(equal(zero.reactive_power_limits.maximum_var_injection_during_charging,
                      absent.reactive_power_limits.maximum_var_injection_during_charging));
    EXPECT_TRUE(equal(zero.reactive_power_limits.maximum_var_absorption_during_discharging,
                      absent.reactive_power_limits.maximum_var_absorption_during_discharging));
    EXPECT_TRUE(equal(zero.reactive_power_limits.maximum_var_injection_during_discharging,
                      absent.reactive_power_limits.maximum_var_injection_during_discharging));
    EXPECT_TRUE(equal(zero.reactive_power_limits.maximum_var_injection_during_charging, dt::RationalNumber{0, 0}));
}

// from_float keeps 4 significant digits and truncates, so 230 * 0.88 encodes as 202.3 and not 202.4.
TEST(DerSetupTest, sae_grid_limits_derive_from_nominal_voltage) {
    const auto ac = make_ac_limits(7400.0f, 11000.0f);

    const auto limits = module::build_sae_der_transfer_limits(ac, std::nullopt, 230u);

    const auto& grid = limits.grid_limits;
    EXPECT_TRUE(equal(grid.nominal_frequency, dt::from_float(50.0f)));
    EXPECT_TRUE(equal(grid.nominal_voltage, dt::from_float(230.0f)));
    EXPECT_TRUE(equal(grid.nominal_voltage_offset, dt::RationalNumber{0, 0}));
    EXPECT_TRUE(equal(grid.maximum_voltage, dt::RationalNumber{2530, -1}));
    EXPECT_TRUE(equal(grid.minimum_voltage, dt::RationalNumber{2023, -1}));
    EXPECT_FALSE(grid.min_frequency.has_value());
    EXPECT_FALSE(grid.max_frequency.has_value());
}

// 400 V is exact on both breakpoints: 440.0 and 352.0.
TEST(DerSetupTest, sae_grid_limits_at_400_volt) {
    const auto ac = make_ac_limits(7400.0f, 11000.0f);

    const auto limits = module::build_sae_der_transfer_limits(ac, std::nullopt, 400u);

    const auto& grid = limits.grid_limits;
    EXPECT_TRUE(equal(grid.nominal_voltage, dt::from_float(400.0f)));
    EXPECT_TRUE(equal(grid.maximum_voltage, dt::RationalNumber{4400, -1}));
    EXPECT_TRUE(equal(grid.minimum_voltage, dt::RationalNumber{3520, -1}));
}

TEST(DerSetupTest, derive_without_der_service_yields_nothing) {
    const auto ac = make_ac_limits(11000.0f, 11000.0f);

    const auto derived = module::derive_der_limits({dt::ServiceCategory::AC_BPT}, ac, std::nullopt, 230u);

    EXPECT_FALSE(derived.iec_limits.has_value());
    EXPECT_FALSE(derived.sae_limits.has_value());
    EXPECT_FALSE(derived.sae_setup_config.has_value());
    EXPECT_EQ(derived.sae_status, module::SaeDerStatus::NotRequested);
}

TEST(DerSetupTest, derive_iec_service_yields_iec_limits_only) {
    const auto ac = make_ac_limits(11000.0f, 11000.0f);

    const auto derived = module::derive_der_limits({dt::ServiceCategory::AC_DER_IEC}, ac, std::nullopt, 230u);

    ASSERT_TRUE(derived.iec_limits.has_value());
    EXPECT_TRUE(equal(derived.iec_limits->nominal_charge_power, ac.charge_power.max));
    EXPECT_FALSE(derived.sae_limits.has_value());
    EXPECT_EQ(derived.sae_status, module::SaeDerStatus::NotRequested);
}

// The defect this guards: the services list is empty at boot and only gains AC_DER_SAE later, so the
// derivation has to produce limits on the later call as well.
TEST(DerSetupTest, derive_after_services_gain_sae_yields_sae_limits) {
    const auto ac = make_ac_limits(11000.0f, 11000.0f);

    const auto at_boot = module::derive_der_limits({}, ac, 5000.0f, 400u);
    ASSERT_FALSE(at_boot.sae_limits.has_value());

    const auto after_der_enabled = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, 5000.0f, 400u);

    ASSERT_TRUE(after_der_enabled.sae_limits.has_value());
    EXPECT_EQ(after_der_enabled.sae_status, module::SaeDerStatus::Ready);
    EXPECT_TRUE(after_der_enabled.sae_setup_config.has_value());
    ASSERT_TRUE(after_der_enabled.sae_limits->nominal_charge_power.has_value());
    EXPECT_TRUE(equal(after_der_enabled.sae_limits->nominal_charge_power.value(), ac.charge_power.max));
    EXPECT_TRUE(equal(after_der_enabled.sae_limits->grid_limits.nominal_voltage, dt::from_float(400.0f)));
    EXPECT_TRUE(equal(after_der_enabled.sae_limits->reactive_power_limits.maximum_var_absorption_during_charging,
                      dt::from_float(5000.0f)));
    EXPECT_EQ(after_der_enabled.nominal_voltage, 400u);
}

TEST(DerSetupTest, derive_sae_without_nominal_frequency_withholds_limits) {
    auto ac = make_ac_limits(11000.0f, 11000.0f);
    ac.nominal_frequency = dt::from_float(0.0f);

    const auto derived = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, std::nullopt, 230u);

    EXPECT_FALSE(derived.sae_limits.has_value());
    EXPECT_FALSE(derived.sae_setup_config.has_value());
    EXPECT_EQ(derived.sae_status, module::SaeDerStatus::GridParametersMissing);
    EXPECT_FLOAT_EQ(derived.nominal_frequency, 0.0f);
}

// A negative frequency is as unusable as a missing one, and only a <= 0 guard rejects it.
TEST(DerSetupTest, derive_sae_with_negative_nominal_frequency_withholds_limits) {
    auto ac = make_ac_limits(11000.0f, 11000.0f);
    ac.nominal_frequency = dt::from_float(-50.0f);

    const auto derived = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, std::nullopt, 230u);

    EXPECT_FALSE(derived.sae_limits.has_value());
    EXPECT_FALSE(derived.sae_setup_config.has_value());
    EXPECT_EQ(derived.sae_status, module::SaeDerStatus::GridParametersMissing);
}

// The voltage has to fail closed like the frequency does: it is the usual base for the EV's percentage
// voltage curves, so a guessed base advertises a grid code that does not match the site.
TEST(DerSetupTest, derive_sae_without_nominal_voltage_withholds_limits) {
    const auto ac = make_ac_limits(11000.0f, std::nullopt);

    const auto derived = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, std::nullopt, std::nullopt);

    EXPECT_FALSE(derived.sae_limits.has_value());
    EXPECT_FALSE(derived.sae_setup_config.has_value());
    EXPECT_EQ(derived.sae_status, module::SaeDerStatus::GridParametersMissing);
    EXPECT_EQ(derived.nominal_voltage, 0u);
}

TEST(DerSetupTest, derive_sae_with_zero_nominal_voltage_withholds_limits) {
    const auto ac = make_ac_limits(11000.0f, std::nullopt);

    const auto derived = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, std::nullopt, 0u);

    EXPECT_FALSE(derived.sae_limits.has_value());
    EXPECT_FALSE(derived.sae_setup_config.has_value());
    EXPECT_EQ(derived.sae_status, module::SaeDerStatus::GridParametersMissing);
}

// The grid values are passed through, not assumed: a 60 Hz site must advertise 60 Hz.
TEST(DerSetupTest, derive_sae_passes_through_a_60_hz_nominal_frequency) {
    auto ac = make_ac_limits(11000.0f, 11000.0f);
    ac.nominal_frequency = dt::from_float(60.0f);

    const auto derived = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, std::nullopt, 277u);

    ASSERT_TRUE(derived.sae_limits.has_value());
    EXPECT_EQ(derived.sae_status, module::SaeDerStatus::Ready);
    EXPECT_TRUE(equal(derived.sae_limits->grid_limits.nominal_frequency, dt::from_float(60.0f)));
    EXPECT_FLOAT_EQ(derived.nominal_frequency, 60.0f);
}

// Drift guard. The advertised voltage window and the library's default must-trip curves are two
// independent declarations of the same thresholds. The trip curves carry duration on x and PercentageV on
// y, and the EV denormalizes the percentage against the base voltage for the function, so if either side
// moves alone the EV's trip thresholds silently stop matching the window the module advertises. Read from
// the library rather than copied, so this test cannot drift with the thing it pins.
TEST(DerSetupTest, trip_fractions_match_the_library_default_must_trip_curves) {
    const auto der_control = iso15118::d20::get_default_sae_der_control(NOMINAL_VOLTAGE_V);

    const auto& over = der_control.voltage_trip.over_voltage_must_trip_curve;
    const auto& under = der_control.voltage_trip.under_voltage_must_trip_curve;

    ASSERT_EQ(over.y_unit, iso15118::sae::DERUnit::PercentageV);
    ASSERT_EQ(under.y_unit, iso15118::sae::DERUnit::PercentageV);
    ASSERT_FALSE(over.curve_data_points.empty());
    ASSERT_FALSE(under.curve_data_points.empty());

    // Curve points are ordered by ascending duration, so the continuous-operation threshold that the
    // advertised window mirrors is the longest-duration point.
    EXPECT_FLOAT_EQ(module::OVER_VOLTAGE_TRIP_FRACTION * 100.0f, over.curve_data_points.back().y_value);
    EXPECT_FLOAT_EQ(module::UNDER_VOLTAGE_TRIP_FRACTION * 100.0f, under.curve_data_points.back().y_value);
}

TEST(DerSetupTest, derive_both_der_services_yields_both_limits) {
    const auto ac = make_ac_limits(11000.0f, 11000.0f);

    const auto derived = module::derive_der_limits({dt::ServiceCategory::AC_DER_IEC, dt::ServiceCategory::AC_DER_SAE},
                                                   ac, 5000.0f, 230u);

    EXPECT_TRUE(derived.iec_limits.has_value());
    EXPECT_TRUE(derived.sae_limits.has_value());
    EXPECT_EQ(derived.sae_status, module::SaeDerStatus::Ready);
}

TEST(DerSetupTest, apply_missing_after_ready_keeps_the_previous_sae_set) {
    auto ac = make_ac_limits(11000.0f, 11000.0f);
    module::DerAppliedState state{};

    const auto ready = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, 5000.0f, 400u);
    const auto first = module::apply_derivation(ready, state);
    ASSERT_EQ(first.sae, module::DerSaeApplyTransition::Assigned);
    ASSERT_TRUE(state.sae_limits.has_value());
    ASSERT_TRUE(state.sae_setup_config.has_value());

    ac.nominal_frequency = dt::from_float(0.0f);
    const auto missing = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, 5000.0f, 400u);
    ASSERT_EQ(missing.sae_status, module::SaeDerStatus::GridParametersMissing);

    const auto second = module::apply_derivation(missing, state);

    EXPECT_EQ(second.sae, module::DerSaeApplyTransition::KeptPrevious);
    ASSERT_TRUE(state.sae_limits.has_value());
    EXPECT_TRUE(state.sae_setup_config.has_value());
    EXPECT_TRUE(equal(state.sae_limits->grid_limits.nominal_frequency, dt::from_float(50.0f)));
    EXPECT_TRUE(equal(state.sae_limits->grid_limits.nominal_voltage, dt::from_float(400.0f)));
}

TEST(DerSetupTest, apply_missing_without_a_prior_derivation_reports_never_derived) {
    auto ac = make_ac_limits(11000.0f, 11000.0f);
    ac.nominal_frequency = dt::from_float(0.0f);
    module::DerAppliedState state{};

    const auto missing = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, 5000.0f, 400u);
    const auto transitions = module::apply_derivation(missing, state);

    EXPECT_EQ(transitions.sae, module::DerSaeApplyTransition::NeverDerived);
    EXPECT_FALSE(state.sae_limits.has_value());
    EXPECT_FALSE(state.sae_setup_config.has_value());
    EXPECT_FALSE(transitions.iec_assigned);
}

TEST(DerSetupTest, apply_assigns_iec_independently_of_sae) {
    auto ac = make_ac_limits(11000.0f, 11000.0f);
    ac.nominal_frequency = dt::from_float(0.0f);
    module::DerAppliedState state{};

    const auto derived = module::derive_der_limits({dt::ServiceCategory::AC_DER_IEC, dt::ServiceCategory::AC_DER_SAE},
                                                   ac, 5000.0f, 400u);
    const auto transitions = module::apply_derivation(derived, state);

    EXPECT_TRUE(transitions.iec_assigned);
    ASSERT_TRUE(state.iec_limits.has_value());
    EXPECT_TRUE(equal(state.iec_limits->nominal_charge_power, ac.charge_power.max));
    EXPECT_EQ(transitions.sae, module::DerSaeApplyTransition::NeverDerived);
    EXPECT_FALSE(state.sae_limits.has_value());
}

TEST(DerSetupTest, apply_leaves_stale_iec_limits_when_the_service_is_withdrawn) {
    const auto ac = make_ac_limits(11000.0f, 11000.0f);
    module::DerAppliedState state{};

    const auto with_iec = module::derive_der_limits({dt::ServiceCategory::AC_DER_IEC}, ac, 5000.0f, 400u);
    ASSERT_TRUE(module::apply_derivation(with_iec, state).iec_assigned);

    const auto without_iec = module::derive_der_limits({dt::ServiceCategory::AC_BPT}, ac, 5000.0f, 400u);
    const auto transitions = module::apply_derivation(without_iec, state);

    EXPECT_FALSE(transitions.iec_assigned);
    EXPECT_TRUE(state.iec_limits.has_value());
}

TEST(DerSetupTest, apply_keeps_a_relayed_sae_setup_config_on_re_derivation) {
    const auto ac = make_ac_limits(11000.0f, 11000.0f);
    module::DerAppliedState state{};

    const auto first = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, 5000.0f, 400u);
    ASSERT_EQ(module::apply_derivation(first, state).sae, module::DerSaeApplyTransition::Assigned);
    ASSERT_TRUE(state.sae_setup_config.has_value());

    // A relayed grid code lands in the applied state; a later AC-limits re-derivation must not erase it.
    state.sae_setup_config->revision = 7;
    state.sae_setup_config->der_control.reactive_power_support.volt_var.enable = true;

    const auto second = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, 6000.0f, 400u);
    ASSERT_TRUE(second.sae_setup_config.has_value());
    const auto transitions = module::apply_derivation(second, state);

    EXPECT_EQ(transitions.sae, module::DerSaeApplyTransition::Assigned);
    ASSERT_TRUE(state.sae_setup_config.has_value());
    EXPECT_EQ(state.sae_setup_config->revision, 7u);
    EXPECT_TRUE(state.sae_setup_config->der_control.reactive_power_support.volt_var.enable);
}

TEST(DerSetupTest, apply_reseeds_a_seed_config_on_re_derivation) {
    const auto ac = make_ac_limits(11000.0f, 11000.0f);
    module::DerAppliedState state{};

    const auto first = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, 5000.0f, 400u);
    ASSERT_EQ(module::apply_derivation(first, state).sae, module::DerSaeApplyTransition::Assigned);
    ASSERT_TRUE(state.sae_setup_config.has_value());
    ASSERT_EQ(state.sae_setup_config->revision, 0u);

    // Revision 0 is the seed nobody dictated, so a re-derivation replaces it instead of preserving a stale
    // nominal-derived default.
    state.sae_setup_config->der_control.enter_service.enter_service_voltage_high = 999.0f;

    const auto second = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, 6000.0f, 400u);
    ASSERT_TRUE(second.sae_setup_config.has_value());
    const auto transitions = module::apply_derivation(second, state);

    EXPECT_EQ(transitions.sae, module::DerSaeApplyTransition::Assigned);
    ASSERT_TRUE(state.sae_setup_config.has_value());
    EXPECT_FLOAT_EQ(state.sae_setup_config->der_control.enter_service.enter_service_voltage_high,
                    second.sae_setup_config->der_control.enter_service.enter_service_voltage_high);
}

TEST(DerSetupTest, apply_fills_an_absent_sae_setup_config_from_the_derivation) {
    const auto ac = make_ac_limits(11000.0f, 11000.0f);
    module::DerAppliedState state{};

    const auto derived = module::derive_der_limits({dt::ServiceCategory::AC_DER_SAE}, ac, 5000.0f, 400u);
    const auto transitions = module::apply_derivation(derived, state);

    EXPECT_EQ(transitions.sae, module::DerSaeApplyTransition::Assigned);
    ASSERT_TRUE(state.sae_setup_config.has_value());
    EXPECT_EQ(state.sae_setup_config->revision, 0u);
    EXPECT_FALSE(state.sae_setup_config->der_control.reactive_power_support.volt_var.enable);
}
