// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/config.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <iso15118/detail/d20/config_validation.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/message/ac_der_sae_types.hpp>

namespace iso15118::d20 {

namespace dt = message_20::datatypes;
namespace dt_sae = message_20::datatypes::sae;

namespace {

// Trip curves carry the duration on x and the voltage or frequency on y per M.2.2.1.10 and M.2.2.1.11,
// which is the reverse of the IEEE 2030.5 DERCurve axis order.
// Each curve point list carries two points because the schema's minimum list length is two.
sae::VoltageTrip get_default_voltage_trip() {
    sae::VoltageTrip voltage_trip{};

    voltage_trip.over_voltage_must_trip_curve.enable = false;
    voltage_trip.over_voltage_must_trip_curve.priority = std::nullopt;
    voltage_trip.over_voltage_must_trip_curve.x_unit = sae::DERUnit::s;
    voltage_trip.over_voltage_must_trip_curve.y_unit = sae::DERUnit::PercentageV;
    voltage_trip.over_voltage_must_trip_curve.curve_data_points = {{2.0f, 110.0f}, {0.16f, 120.0f}};
    voltage_trip.over_voltage_must_trip_curve.curve_data_points_L2 = std::nullopt;
    voltage_trip.over_voltage_must_trip_curve.curve_data_points_L3 = std::nullopt;

    voltage_trip.under_voltage_must_trip_curve.enable = false;
    voltage_trip.under_voltage_must_trip_curve.priority = std::nullopt;
    voltage_trip.under_voltage_must_trip_curve.x_unit = sae::DERUnit::s;
    voltage_trip.under_voltage_must_trip_curve.y_unit = sae::DERUnit::PercentageV;
    voltage_trip.under_voltage_must_trip_curve.curve_data_points = {{2.0f, 88.0f}, {0.16f, 50.0f}};
    voltage_trip.under_voltage_must_trip_curve.curve_data_points_L2 = std::nullopt;
    voltage_trip.under_voltage_must_trip_curve.curve_data_points_L3 = std::nullopt;

    voltage_trip.over_voltage_momentary_cessation_trip_curve = std::nullopt;
    voltage_trip.under_voltage_momentary_cessation_trip_curve = std::nullopt;
    voltage_trip.over_voltage_may_trip_curve = std::nullopt;
    voltage_trip.under_voltage_may_trip_curve = std::nullopt;

    return voltage_trip;
}

// Each curve point list carries two points because the schema's minimum list length is two.
sae::FrequencyTrip get_default_frequency_trip() {
    sae::FrequencyTrip frequency_trip{};

    frequency_trip.over_frequency_must_trip_curve.enable = false;
    frequency_trip.over_frequency_must_trip_curve.priority = std::nullopt;
    frequency_trip.over_frequency_must_trip_curve.x_unit = sae::DERUnit::s;
    frequency_trip.over_frequency_must_trip_curve.y_unit = sae::DERUnit::Hz;
    frequency_trip.over_frequency_must_trip_curve.curve_data_points = {{300.0f, 51.5f}, {0.16f, 52.0f}};
    frequency_trip.over_frequency_must_trip_curve.curve_data_points_L2 = std::nullopt;
    frequency_trip.over_frequency_must_trip_curve.curve_data_points_L3 = std::nullopt;

    frequency_trip.under_frequency_must_trip_curve.enable = false;
    frequency_trip.under_frequency_must_trip_curve.priority = std::nullopt;
    frequency_trip.under_frequency_must_trip_curve.x_unit = sae::DERUnit::s;
    frequency_trip.under_frequency_must_trip_curve.y_unit = sae::DERUnit::Hz;
    frequency_trip.under_frequency_must_trip_curve.curve_data_points = {{300.0f, 47.5f}, {0.16f, 47.0f}};
    frequency_trip.under_frequency_must_trip_curve.curve_data_points_L2 = std::nullopt;
    frequency_trip.under_frequency_must_trip_curve.curve_data_points_L3 = std::nullopt;

    frequency_trip.over_frequency_may_trip_curve = std::nullopt;
    frequency_trip.under_frequency_may_trip_curve = std::nullopt;

    return frequency_trip;
}

sae::EnterServiceCPDRes get_default_enter_service(float nominal_voltage_v) {
    sae::EnterServiceCPDRes enter_service{};

    enter_service.permit_service = false;
    // AMD1 Table 1: both bands are volts, not percentages.
    enter_service.enter_service_voltage_high = 1.05f * nominal_voltage_v;
    enter_service.enter_service_voltage_low = 0.917f * nominal_voltage_v;
    enter_service.enter_service_frequency_high = 50.1f;
    enter_service.enter_service_frequency_low = 49.5f;
    enter_service.enter_service_delay = std::nullopt;
    // NOTE(mlitre): 0 satisfies [V2G20-3364] and stays inert.
    enter_service.enter_service_randomized_delay = 0.0f;
    enter_service.enter_service_ramp_time = std::nullopt;

    return enter_service;
}

// Each curve point list carries two points because the schema's minimum list length is two.
sae::ReactivePowerSupportCPDRes get_default_reactive_power_support(float nominal_voltage_v) {
    sae::ReactivePowerSupportCPDRes reactive_power_support{};

    reactive_power_support.constant_power_factor.enable = false;
    reactive_power_support.constant_power_factor.priority = std::nullopt;
    reactive_power_support.constant_power_factor.power_factor_value = 1.0f;
    reactive_power_support.constant_power_factor.power_factor_value_L2 = std::nullopt;
    reactive_power_support.constant_power_factor.power_factor_value_L3 = std::nullopt;
    reactive_power_support.constant_power_factor.power_factor_excitation = sae::PowerFactorExcitation::OverExcited;
    reactive_power_support.constant_power_factor.power_factor_excitation_L2 = std::nullopt;
    reactive_power_support.constant_power_factor.power_factor_excitation_L3 = std::nullopt;

    reactive_power_support.volt_var.enable = false;
    reactive_power_support.volt_var.priority = std::nullopt;
    reactive_power_support.volt_var.x_unit = sae::DERUnit::PercentageV;
    reactive_power_support.volt_var.y_unit = sae::DERUnit::PercentageEVMaximumConfiguredReactivePower;
    reactive_power_support.volt_var.curve_data_points = {{100.0f, 0.0f}, {110.0f, 0.0f}};
    reactive_power_support.volt_var.curve_data_points_L2 = std::nullopt;
    reactive_power_support.volt_var.curve_data_points_L3 = std::nullopt;
    reactive_power_support.volt_var.open_loop_response_time = 5.0f;
    reactive_power_support.volt_var.time_constant_pt1 = std::nullopt;
    // AMD1 Table 1: the reference voltage is volts, not a percentage.
    reactive_power_support.volt_var.reference_voltage = nominal_voltage_v;
    reactive_power_support.volt_var.autonomous_reference_voltage_adjustment_enable = false;
    reactive_power_support.volt_var.reference_voltage_adjustment_time_constant = 0;

    reactive_power_support.watt_var.enable = false;
    reactive_power_support.watt_var.priority = std::nullopt;
    reactive_power_support.watt_var.x_unit = sae::DERUnit::PercentageEVMaximumConfiguredActivePower;
    reactive_power_support.watt_var.y_unit = sae::DERUnit::PercentageEVMaximumConfiguredReactivePower;
    reactive_power_support.watt_var.curve_data_points = {{0.0f, 0.0f}, {100.0f, 0.0f}};
    reactive_power_support.watt_var.curve_data_points_L2 = std::nullopt;
    reactive_power_support.watt_var.curve_data_points_L3 = std::nullopt;
    reactive_power_support.watt_var.open_loop_response_time = std::nullopt;
    reactive_power_support.watt_var.time_constant_pt1 = std::nullopt;

    reactive_power_support.constant_var.enable = false;
    reactive_power_support.constant_var.priority = std::nullopt;
    reactive_power_support.constant_var.var_setpoint = 0.0f;
    reactive_power_support.constant_var.var_setpoint_L2 = std::nullopt;
    reactive_power_support.constant_var.var_setpoint_L3 = std::nullopt;
    reactive_power_support.constant_var.unit = sae::DERUnit::PercentageEVMaximumConfiguredReactivePower;

    return reactive_power_support;
}

// Each curve point list carries two points because the schema's minimum list length is two.
sae::ActivePowerSupportCPDRes get_default_active_power_support() {
    sae::ActivePowerSupportCPDRes active_power_support{};

    active_power_support.frequency_droop.enable = false;
    active_power_support.frequency_droop.priority = std::nullopt;
    // Table M.33: an absent OverFrequencyDroop obliges UnderFrequencyDroop. Carrying a zeroed over
    // frequency branch satisfies that unconditionally and stays inert.
    sae::FrequencyDroopSettings over_frequency_droop{};
    over_frequency_droop.db = 0.0f;
    over_frequency_droop.droop_factor = 0.0f;
    over_frequency_droop.power_reference = sae::PowerReference::MaximumActivePower;
    over_frequency_droop.open_loop_response_time = 0.0f;
    active_power_support.frequency_droop.over_frequency_droop = over_frequency_droop;
    active_power_support.frequency_droop.under_frequency_droop = std::nullopt;

    active_power_support.volt_watt.enable = false;
    active_power_support.volt_watt.priority = std::nullopt;
    active_power_support.volt_watt.x_unit = sae::DERUnit::PercentageV;
    active_power_support.volt_watt.y_unit = sae::DERUnit::PercentageEVMaximumConfiguredActivePower;
    active_power_support.volt_watt.curve_data_points = {{100.0f, 100.0f}, {110.0f, 100.0f}};
    active_power_support.volt_watt.curve_data_points_L2 = std::nullopt;
    active_power_support.volt_watt.curve_data_points_L3 = std::nullopt;
    active_power_support.volt_watt.open_loop_response_time = 5.0f;
    active_power_support.volt_watt.time_constant_pt1 = std::nullopt;

    active_power_support.constant_watt.enable = false;
    active_power_support.constant_watt.priority = std::nullopt;
    active_power_support.constant_watt.watt_setpoint = 0.0f;
    active_power_support.constant_watt.watt_setpoint_L2 = std::nullopt;
    active_power_support.constant_watt.watt_setpoint_L3 = std::nullopt;
    active_power_support.constant_watt.unit = sae::DERUnit::PercentageEVMaximumConfiguredActivePower;

    active_power_support.limit_max_discharge_power.enable = false;
    active_power_support.limit_max_discharge_power.priority = std::nullopt;
    active_power_support.limit_max_discharge_power.percentage_value = 100;
    active_power_support.limit_max_discharge_power.percentage_value_L2 = std::nullopt;
    active_power_support.limit_max_discharge_power.percentage_value_L3 = std::nullopt;
    active_power_support.limit_max_discharge_power.open_loop_response_time = std::nullopt;

    return active_power_support;
}

} // namespace

// Every enable is false, so no grid code function is activated. Charge and discharge stay available.
sae::DERControl get_default_sae_der_control(float nominal_voltage_v) {
    sae::DERControl control{};

    control.voltage_trip = get_default_voltage_trip();
    control.frequency_trip = get_default_frequency_trip();
    control.enter_service = get_default_enter_service(nominal_voltage_v);
    control.reactive_power_support = get_default_reactive_power_support(nominal_voltage_v);
    control.active_power_support = get_default_active_power_support();

    return control;
}

DerSaeSetupConfig make_inert_default_sae_setup_config(float nominal_voltage_v) {
    return DerSaeSetupConfig{get_default_sae_der_control(nominal_voltage_v),
                             sae::RequiredDEROperatingMode::GridFollowing, sae::GridConnectionMode::GridConnected};
}

namespace {

/// \brief One curve carrier's three data point lists, so the checks below can loop over heterogeneous carriers.
struct CurveLists {
    const char* name;
    const sae::CurveDataPointsList* points;
    const std::optional<sae::CurveDataPointsList>* points_L2;
    const std::optional<sae::CurveDataPointsList>* points_L3;
};

template <typename Curve> CurveLists curve_lists(const char* name, const Curve& curve) {
    return {name, &curve.curve_data_points, &curve.curve_data_points_L2, &curve.curve_data_points_L3};
}

std::optional<std::string> check_curve_length(const sae::CurveDataPointsList& points, const char* name,
                                              const char* phase) {
    if (points.size() >= static_cast<size_t>(dt_sae::CurveDataPointsMinLength)) {
        return std::nullopt;
    }
    return std::string(name) + phase + " carries fewer than " + std::to_string(dt_sae::CurveDataPointsMinLength) +
           " curve data points";
}

std::optional<std::string> check_curve_lists(const CurveLists& entry) {
    if (auto violation = check_curve_length(*entry.points, entry.name, "")) {
        return violation;
    }
    if (entry.points_L2->has_value()) {
        if (auto violation = check_curve_length(entry.points_L2->value(), entry.name, " L2")) {
            return violation;
        }
    }
    if (entry.points_L3->has_value()) {
        if (auto violation = check_curve_length(entry.points_L3->value(), entry.name, " L3")) {
            return violation;
        }
    }
    return std::nullopt;
}

void add_optional_curve(std::vector<CurveLists>& entries, const char* name, const std::optional<sae::DERCurve>& curve) {
    if (curve.has_value()) {
        entries.push_back(curve_lists(name, curve.value()));
    }
}

/// \brief Checks every curve that ends up on the wire.
///
/// The enable flag is not a gate here: in DERControlCPDRes the trip curves and the volt var, watt var and
/// volt watt blocks are mandatory elements, so their data point lists are sent whether or not the function is
/// enabled. The optional trip curves are checked whenever they are present, for the same reason.
std::optional<std::string> validate_sae_der_curves(const sae::DERControl& control) {
    const auto& voltage_trip = control.voltage_trip;
    const auto& frequency_trip = control.frequency_trip;

    std::vector<CurveLists> entries{
        curve_lists("over voltage must trip curve", voltage_trip.over_voltage_must_trip_curve),
        curve_lists("under voltage must trip curve", voltage_trip.under_voltage_must_trip_curve),
        curve_lists("over frequency must trip curve", frequency_trip.over_frequency_must_trip_curve),
        curve_lists("under frequency must trip curve", frequency_trip.under_frequency_must_trip_curve),
        curve_lists("volt var curve", control.reactive_power_support.volt_var),
        curve_lists("watt var curve", control.reactive_power_support.watt_var),
        curve_lists("volt watt curve", control.active_power_support.volt_watt),
    };

    add_optional_curve(entries, "over voltage momentary cessation trip curve",
                       voltage_trip.over_voltage_momentary_cessation_trip_curve);
    add_optional_curve(entries, "under voltage momentary cessation trip curve",
                       voltage_trip.under_voltage_momentary_cessation_trip_curve);
    add_optional_curve(entries, "over voltage may trip curve", voltage_trip.over_voltage_may_trip_curve);
    add_optional_curve(entries, "under voltage may trip curve", voltage_trip.under_voltage_may_trip_curve);
    add_optional_curve(entries, "over frequency may trip curve", frequency_trip.over_frequency_may_trip_curve);
    add_optional_curve(entries, "under frequency may trip curve", frequency_trip.under_frequency_may_trip_curve);

    for (const auto& entry : entries) {
        if (auto violation = check_curve_lists(entry)) {
            return violation;
        }
    }

    return std::nullopt;
}

std::optional<std::string> validate_sae_enter_service(const sae::EnterServiceCPDRes& enter_service) {
    if (not enter_service.enter_service_delay.has_value() and
        not enter_service.enter_service_randomized_delay.has_value()) {
        return "enter service carries neither enter_service_delay nor enter_service_randomized_delay "
               "[V2G20-3364]";
    }
    if (enter_service.enter_service_delay.has_value() and not enter_service.enter_service_ramp_time.has_value()) {
        return "enter service carries enter_service_delay without enter_service_ramp_time [V2G20-3365], "
               "[V2G20-3367]";
    }
    return std::nullopt;
}

enum class Sign : std::uint8_t {
    NonNegative,
    NonPositive
};

struct SignedLimit {
    const char* name;
    dt::RationalNumber value;
    Sign sign;
};

void add_limit(std::vector<SignedLimit>& limits, const char* name, const dt::RationalNumber& value, Sign sign) {
    limits.push_back({name, value, sign});
}

void add_limit(std::vector<SignedLimit>& limits, const char* name, const std::optional<dt::RationalNumber>& value,
               Sign sign) {
    if (value.has_value()) {
        limits.push_back({name, value.value(), sign});
    }
}

/// \brief Checks the advertised limits against the sign conventions documented on SaeDerTransferLimits and
/// EVSEReactivePowerLimits, which restate ISO 15118-20 AMD1 8.3.5.2.
///
/// The exponent never flips the sign of a RationalNumber, so the mantissa alone decides.
std::optional<std::string> validate_sae_der_limits(const SaeDerTransferLimits& sae_limits) {
    const auto& reactive = sae_limits.reactive_power_limits;

    std::vector<SignedLimit> limits;
    limits.reserve(18);

    add_limit(limits, "maximum_var_absorption_during_charging", reactive.maximum_var_absorption_during_charging,
              Sign::NonNegative);
    add_limit(limits, "maximum_var_absorption_during_charging_L2", reactive.maximum_var_absorption_during_charging_L2,
              Sign::NonNegative);
    add_limit(limits, "maximum_var_absorption_during_charging_L3", reactive.maximum_var_absorption_during_charging_L3,
              Sign::NonNegative);
    add_limit(limits, "maximum_var_injection_during_charging", reactive.maximum_var_injection_during_charging,
              Sign::NonPositive);
    add_limit(limits, "maximum_var_injection_during_charging_L2", reactive.maximum_var_injection_during_charging_L2,
              Sign::NonPositive);
    add_limit(limits, "maximum_var_injection_during_charging_L3", reactive.maximum_var_injection_during_charging_L3,
              Sign::NonPositive);
    add_limit(limits, "maximum_var_absorption_during_discharging", reactive.maximum_var_absorption_during_discharging,
              Sign::NonNegative);
    add_limit(limits, "maximum_var_absorption_during_discharging_L2",
              reactive.maximum_var_absorption_during_discharging_L2, Sign::NonNegative);
    add_limit(limits, "maximum_var_absorption_during_discharging_L3",
              reactive.maximum_var_absorption_during_discharging_L3, Sign::NonNegative);
    add_limit(limits, "maximum_var_injection_during_discharging", reactive.maximum_var_injection_during_discharging,
              Sign::NonPositive);
    add_limit(limits, "maximum_var_injection_during_discharging_L2",
              reactive.maximum_var_injection_during_discharging_L2, Sign::NonPositive);
    add_limit(limits, "maximum_var_injection_during_discharging_L3",
              reactive.maximum_var_injection_during_discharging_L3, Sign::NonPositive);

    add_limit(limits, "nominal_discharge_power", sae_limits.nominal_discharge_power, Sign::NonPositive);
    add_limit(limits, "nominal_discharge_power_L2", sae_limits.nominal_discharge_power_L2, Sign::NonPositive);
    add_limit(limits, "nominal_discharge_power_L3", sae_limits.nominal_discharge_power_L3, Sign::NonPositive);
    add_limit(limits, "max_discharge_power", sae_limits.max_discharge_power, Sign::NonPositive);
    add_limit(limits, "max_discharge_power_L2", sae_limits.max_discharge_power_L2, Sign::NonPositive);
    add_limit(limits, "max_discharge_power_L3", sae_limits.max_discharge_power_L3, Sign::NonPositive);

    for (const auto& limit : limits) {
        if (limit.sign == Sign::NonNegative and limit.value.value < 0) {
            return std::string(limit.name) + " must be non-negative";
        }
        if (limit.sign == Sign::NonPositive and limit.value.value > 0) {
            return std::string(limit.name) + " must be non-positive";
        }
    }

    return std::nullopt;
}

std::optional<dt::RationalNumber> phase_maximum(const std::optional<Limit<dt::RationalNumber>>& limit) {
    if (not limit.has_value()) {
        return std::nullopt;
    }
    return limit.value().max;
}

struct NominalMaximumPair {
    const char* nominal_name;
    const std::optional<dt::RationalNumber>& nominal;
    const char* maximum_name;
    std::optional<dt::RationalNumber> maximum;
    bool use_magnitude;
};

std::optional<std::string> check_nominal_within_maximum(const NominalMaximumPair& pair) {
    if (not pair.nominal.has_value()) {
        return std::nullopt;
    }
    if (not pair.maximum.has_value()) {
        return std::string(pair.nominal_name) + " is configured but " + pair.maximum_name + " is not";
    }

    const auto value_of = [use_magnitude = pair.use_magnitude](const dt::RationalNumber& number) {
        const auto value = dt::from_RationalNumber(number);
        return use_magnitude ? std::fabs(value) : value;
    };

    const auto nominal_value = value_of(pair.nominal.value());
    const auto maximum_value = value_of(pair.maximum.value());

    if (nominal_value > maximum_value) {
        return std::string(pair.nominal_name) + " " + std::to_string(nominal_value) + " exceeds " + pair.maximum_name +
               " " + std::to_string(maximum_value);
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> validate_sae_der_setup(const DerSaeSetupConfig& setup_config,
                                                  const SaeDerTransferLimits& sae_limits,
                                                  const AcTransferLimits& ac_limits) {
    if (auto violation = validate_sae_der_curves(setup_config.der_control)) {
        return violation;
    }
    if (auto violation = validate_sae_enter_service(setup_config.der_control.enter_service)) {
        return violation;
    }
    if (auto violation = validate_sae_der_limits(sae_limits)) {
        return violation;
    }
    return validate_sae_nominals_within_maxima(sae_limits, ac_limits);
}

std::optional<std::string> validate_sae_nominals_within_maxima(const SaeDerTransferLimits& sae_limits,
                                                               const AcTransferLimits& ac_limits) {
    const NominalMaximumPair pairs[] = {
        {"nominal_charge_power", sae_limits.nominal_charge_power, "charge_power.max", ac_limits.charge_power.max,
         false},
        {"nominal_charge_power_L2", sae_limits.nominal_charge_power_L2, "charge_power_L2.max",
         phase_maximum(ac_limits.charge_power_L2), false},
        {"nominal_charge_power_L3", sae_limits.nominal_charge_power_L3, "charge_power_L3.max",
         phase_maximum(ac_limits.charge_power_L3), false},
        {"nominal_discharge_power", sae_limits.nominal_discharge_power, "max_discharge_power",
         sae_limits.max_discharge_power, true},
        {"nominal_discharge_power_L2", sae_limits.nominal_discharge_power_L2, "max_discharge_power_L2",
         sae_limits.max_discharge_power_L2, true},
        {"nominal_discharge_power_L3", sae_limits.nominal_discharge_power_L3, "max_discharge_power_L3",
         sae_limits.max_discharge_power_L3, true},
    };

    for (const auto& pair : pairs) {
        if (auto violation = check_nominal_within_maximum(pair)) {
            return violation;
        }
    }
    return std::nullopt;
}

} // namespace iso15118::d20
