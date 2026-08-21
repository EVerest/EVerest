// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/config.hpp>

#include <algorithm>
#include <cstdint>
#include <string>

#include <iso15118/detail/d20/config_validation.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/message/ac_der_sae_types.hpp>

namespace iso15118::d20 {

namespace dt = message_20::datatypes;
namespace dt_sae = message_20::datatypes::sae;

namespace {

auto get_mobility_needs_mode(const ControlMobilityNeedsModes& mode) {
    using namespace dt;

    if (mode.control_mode == ControlMode::Scheduled and mode.mobility_mode == MobilityNeedsMode::ProvidedBySecc) {
        logf_info("Setting the mobility needs mode to ProvidedByEvcc. In scheduled mode only ProvidedByEvcc is "
                  "supported.");
        return MobilityNeedsMode::ProvidedByEvcc;
    }

    return mode.mobility_mode;
}

auto get_default_ac_parameter_list(const std::vector<ControlMobilityNeedsModes>& control_mobility_modes,
                                   const AcSetupConfig& ac_setup_config) {
    using namespace dt;

    std::vector<AcParameterList> param_list;

    for (const auto& mode : control_mobility_modes) {
        for (const auto& connector : ac_setup_config.connectors) {
            param_list.push_back({
                connector,
                mode.control_mode,
                get_mobility_needs_mode(mode),
                ac_setup_config.voltage,
                Pricing::NoPricing,
            });
        }
    }

    return param_list;
}

auto get_default_ac_bpt_parameter_list(const std::vector<ControlMobilityNeedsModes>& control_mobility_modes,
                                       const AcSetupConfig& ac_setup_config, const BptSetupConfig& bpt_setup_config) {
    using namespace dt;

    std::vector<AcBptParameterList> param_list;

    for (const auto& mode : control_mobility_modes) {
        for (const auto& connector : ac_setup_config.connectors) {
            param_list.push_back(
                {{
                     connector,
                     mode.control_mode,
                     get_mobility_needs_mode(mode),
                     ac_setup_config.voltage,
                     Pricing::NoPricing,
                 },
                 bpt_setup_config.bpt_channel,
                 bpt_setup_config.generator_mode,
                 bpt_setup_config.grid_code_detection_method.value_or(dt::GridCodeIslandingDetectionMethod::Passive)});
        }
    }

    return param_list;
}

auto get_default_ac_der_iec_parameter_list(const std::vector<ControlMobilityNeedsModes>& control_mobility_modes,
                                           const AcSetupConfig& ac_setup_config,
                                           const DerIecSetupConfig& der_setup_config) {
    using namespace dt;

    std::vector<AcDerParameterList> param_list;

    constexpr auto MAX_IEC_CONTROL_FUNCTIONS = 12;
    std::bitset<MAX_IEC_CONTROL_FUNCTIONS> control_functions{};

    static_assert(MAX_IEC_CONTROL_FUNCTIONS ==
                      message_20::to_underlying_value(iec::DERControlName::UnderVoltageFaultRideThroughMode) + 1,
                  "MAX_IEC_CONTROL_FUNCTIONS should be in sync with the DERControlName enum definition");

    for (const auto& function : der_setup_config.supported_der_control_functions) {
        control_functions.set(static_cast<size_t>(function.first), true);
    }

    for (const auto& mode : control_mobility_modes) {
        for (const auto& connector : ac_setup_config.connectors) {
            param_list.push_back({{
                                      connector,
                                      mode.control_mode,
                                      get_mobility_needs_mode(mode),
                                      ac_setup_config.voltage,
                                      Pricing::NoPricing,
                                  },
                                  control_functions});
        }
    }

    return param_list;
}

auto get_default_dc_parameter_list(const std::vector<ControlMobilityNeedsModes>& control_mobility_modes) {
    using namespace dt;

    // TODO(sl): Add check if a control mode is more than one in that vector

    std::vector<DcParameterList> param_list;

    for (const auto& mode : control_mobility_modes) {
        param_list.push_back({
            DcConnector::Extended,
            mode.control_mode,
            get_mobility_needs_mode(mode),
            Pricing::NoPricing,
        });
    }

    return param_list;
}

auto get_default_dc_bpt_parameter_list(const std::vector<ControlMobilityNeedsModes>& control_mobility_modes,
                                       const BptSetupConfig& bpt_setup_config) {
    using namespace dt;

    // TODO(sl): Add check if a control mode is more than one in that vector

    std::vector<DcBptParameterList> param_list;

    for (const auto& mode : control_mobility_modes) {
        param_list.push_back({{
                                  DcConnector::Extended,
                                  mode.control_mode,
                                  get_mobility_needs_mode(mode),
                                  Pricing::NoPricing,
                              },
                              bpt_setup_config.bpt_channel,
                              bpt_setup_config.generator_mode});
    }

    return param_list;
}

auto get_default_mcs_parameter_list(const std::vector<ControlMobilityNeedsModes>& control_mobility_modes) {
    using namespace dt;

    // TODO(sl): Add check if a control mode is more than one in that vector
    std::vector<McsParameterList> param_list;

    for (const auto& mode : control_mobility_modes) {
        param_list.push_back({
            McsConnector::Mcs,
            mode.control_mode,
            get_mobility_needs_mode(mode),
            Pricing::NoPricing,
        });
    }

    return param_list;
}

auto get_default_mcs_bpt_parameter_list(const std::vector<ControlMobilityNeedsModes>& control_mobility_modes,
                                        const BptSetupConfig& bpt_setup_config) {
    using namespace dt;

    // TODO(sl): Add check if a control mode is more than one in that vector
    std::vector<McsBptParameterList> param_list;

    for (const auto& mode : control_mobility_modes) {
        param_list.push_back({{
                                  McsConnector::Mcs,
                                  mode.control_mode,
                                  get_mobility_needs_mode(mode),
                                  Pricing::NoPricing,
                              },
                              bpt_setup_config.bpt_channel,
                              bpt_setup_config.generator_mode});
    }

    return param_list;
}

// Trip curves carry the duration on x and the voltage or frequency on y per M.2.2.1.10 and M.2.2.1.11,
// which is the reverse of the IEEE 2030.5 DERCurve axis order. Points are ordered by ascending
// duration; an EV validating monotonic x values rejects the reverse order.
// Each curve point list carries two points because the schema's minimum list length is two.
sae::VoltageTrip get_default_voltage_trip() {
    sae::VoltageTrip voltage_trip{};

    voltage_trip.over_voltage_must_trip_curve.enable = false;
    voltage_trip.over_voltage_must_trip_curve.priority = std::nullopt;
    voltage_trip.over_voltage_must_trip_curve.x_unit = sae::DERUnit::s;
    voltage_trip.over_voltage_must_trip_curve.y_unit = sae::DERUnit::PercentageV;
    voltage_trip.over_voltage_must_trip_curve.curve_data_points = {{0.16f, 120.0f}, {2.0f, 110.0f}};
    voltage_trip.over_voltage_must_trip_curve.curve_data_points_L2 = std::nullopt;
    voltage_trip.over_voltage_must_trip_curve.curve_data_points_L3 = std::nullopt;

    voltage_trip.under_voltage_must_trip_curve.enable = false;
    voltage_trip.under_voltage_must_trip_curve.priority = std::nullopt;
    voltage_trip.under_voltage_must_trip_curve.x_unit = sae::DERUnit::s;
    voltage_trip.under_voltage_must_trip_curve.y_unit = sae::DERUnit::PercentageV;
    voltage_trip.under_voltage_must_trip_curve.curve_data_points = {{0.16f, 50.0f}, {2.0f, 88.0f}};
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
    frequency_trip.over_frequency_must_trip_curve.curve_data_points = {{0.16f, 52.0f}, {300.0f, 51.5f}};
    frequency_trip.over_frequency_must_trip_curve.curve_data_points_L2 = std::nullopt;
    frequency_trip.over_frequency_must_trip_curve.curve_data_points_L3 = std::nullopt;

    frequency_trip.under_frequency_must_trip_curve.enable = false;
    frequency_trip.under_frequency_must_trip_curve.priority = std::nullopt;
    frequency_trip.under_frequency_must_trip_curve.x_unit = sae::DERUnit::s;
    frequency_trip.under_frequency_must_trip_curve.y_unit = sae::DERUnit::Hz;
    frequency_trip.under_frequency_must_trip_curve.curve_data_points = {{0.16f, 47.0f}, {300.0f, 47.5f}};
    frequency_trip.under_frequency_must_trip_curve.curve_data_points_L2 = std::nullopt;
    frequency_trip.under_frequency_must_trip_curve.curve_data_points_L3 = std::nullopt;

    frequency_trip.over_frequency_may_trip_curve = std::nullopt;
    frequency_trip.under_frequency_may_trip_curve = std::nullopt;

    return frequency_trip;
}

sae::EnterServiceCPDRes get_default_enter_service() {
    sae::EnterServiceCPDRes enter_service{};

    enter_service.permit_service = false;
    enter_service.enter_service_voltage_high = 105.0f;
    enter_service.enter_service_voltage_low = 91.7f;
    enter_service.enter_service_frequency_high = 50.1f;
    enter_service.enter_service_frequency_low = 49.5f;
    enter_service.enter_service_delay = std::nullopt;
    // NOTE(mlitre): 0 satisfies [V2G20-3364] and stays inert.
    enter_service.enter_service_randomized_delay = 0.0f;
    enter_service.enter_service_ramp_time = std::nullopt;

    return enter_service;
}

// Each curve point list carries two points because the schema's minimum list length is two.
sae::ReactivePowerSupportCPDRes get_default_reactive_power_support() {
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
    reactive_power_support.volt_var.reference_voltage = 100.0f;
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
    reactive_power_support.constant_var.unit = sae::DERUnit::var;

    return reactive_power_support;
}

// Each curve point list carries two points because the schema's minimum list length is two.
sae::ActivePowerSupportCPDRes get_default_active_power_support() {
    sae::ActivePowerSupportCPDRes active_power_support{};

    active_power_support.frequency_droop.enable = false;
    active_power_support.frequency_droop.priority = std::nullopt;
    active_power_support.frequency_droop.over_frequency_droop = std::nullopt;
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
sae::DERControl get_default_sae_der_control() {
    sae::DERControl control{};

    control.voltage_trip = get_default_voltage_trip();
    control.frequency_trip = get_default_frequency_trip();
    control.enter_service = get_default_enter_service();
    control.reactive_power_support = get_default_reactive_power_support();
    control.active_power_support = get_default_active_power_support();

    return control;
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

/// \brief Applies the AC_DER_SAE offer rules to an offered service list.
///
/// Fills in the inert default setup config when none was given, and removes AC_DER_SAE from the offer when
/// the limits are missing or the setup config is not conformant. This is the single decision procedure: it
/// runs on construction and again on every replacement of the offered services.
void apply_ac_der_sae_offer_rules(std::vector<dt::ServiceCategory>& services,
                                  std::optional<DerSaeSetupConfig>& setup_config,
                                  const std::optional<SaeDerTransferLimits>& sae_limits) {
    const auto ac_der_sae_found =
        std::find(services.begin(), services.end(), dt::ServiceCategory::AC_DER_SAE) != services.end();

    if (not ac_der_sae_found) {
        return;
    }

    if (not setup_config.has_value()) {
        logf_warning("The supported energy services contain AC_DER_SAE, but no sae der setup config was defined. "
                     "Falling back to the inert default grid code configuration.");
        setup_config.emplace();
    }

    const auto strip_ac_der_sae = [&services]() {
        services.erase(std::remove(services.begin(), services.end(), dt::ServiceCategory::AC_DER_SAE), services.end());
    };

    if (not sae_limits.has_value()) {
        strip_ac_der_sae();
        logf_error("The supported energy services contain AC_DER_SAE, but there are no sae der limits defined. "
                   "Removing AC_DER_SAE from the supported_energy_transfer list!");
        return;
    }

    // setup_config is filled in above when it was missing, so both inputs are present here.
    const auto violation = validate_sae_der_setup(setup_config.value(), sae_limits.value());
    if (violation.has_value()) {
        strip_ac_der_sae();
        logf_error("The sae der configuration is not conformant: %s. Removing AC_DER_SAE from the "
                   "supported_energy_transfer list!",
                   violation.value().c_str());
    }
}

} // namespace

std::optional<std::string> validate_sae_der_setup(const DerSaeSetupConfig& setup_config,
                                                  const SaeDerTransferLimits& sae_limits) {
    if (auto violation = validate_sae_der_curves(setup_config.der_control)) {
        return violation;
    }
    if (auto violation = validate_sae_enter_service(setup_config.der_control.enter_service)) {
        return violation;
    }
    return validate_sae_der_limits(sae_limits);
}

void SessionConfig::set_supported_energy_transfer_services(std::vector<dt::ServiceCategory> services) {
    supported_energy_transfer_services = std::move(services);
    apply_ac_der_sae_offer_rules(supported_energy_transfer_services, der_sae_setup_config, der_sae_limits);
}

SessionConfig::SessionConfig(EvseSetupConfig config) :
    evse_id(std::move(config.evse_id)),
    cert_install_service(config.enable_certificate_install_service),
    authorization_services(std::move(config.authorization_services)),
    supported_vas_services(std::move(config.supported_vas_services)),
    dc_limits(config.dc_limits),
    ac_limits(config.ac_limits),
    der_iec_limits(config.der_iec_limits),
    der_sae_setup_config(config.der_sae_setup_config),
    der_sae_limits(config.der_sae_limits),
    powersupply_limits(config.powersupply_limits),
    supported_control_mobility_modes(std::move(config.control_mobility_modes)),
    custom_protocol(std::move(config.custom_protocol)),
    selecting_sap_based_on_energy_service(config.selecting_sap_based_on_energy_service) {

    set_supported_energy_transfer_services(std::move(config.supported_energy_services));

    // TODO(SL): How to handle this probaly
    const auto is_dc_bpt_service = [](dt::ServiceCategory service) {
        return service == dt::ServiceCategory::DC_BPT or service == dt::ServiceCategory::MCS_BPT;
    };
    const auto dc_bpt_found = std::any_of(supported_energy_transfer_services.begin(),
                                          supported_energy_transfer_services.end(), is_dc_bpt_service);

    if (dc_bpt_found and not dc_limits.discharge_limits.has_value()) {
        logf_warning("The supported energy services contain DC_BPT or MCS_BPT, but dc limits does not contain BPT "
                     "limits. This can lead to session shutdowns.");
    }

    const auto is_ac_bpt_service = [](dt::ServiceCategory service) { return service == dt::ServiceCategory::AC_BPT; };
    const auto ac_bpt_found = std::any_of(supported_energy_transfer_services.begin(),
                                          supported_energy_transfer_services.end(), is_ac_bpt_service);

    if (ac_bpt_found and not ac_limits.discharge_power.has_value()) {
        logf_warning("The supported energy services contain AC_BPT, but ac limits does not contain BPT limits. This "
                     "can lead to session shutdowns.");
    }

    const auto is_ac_der_iec_service = [](dt::ServiceCategory service) {
        return service == dt::ServiceCategory::AC_DER_IEC;
    };
    const auto ac_der_iec_found = std::any_of(supported_energy_transfer_services.begin(),
                                              supported_energy_transfer_services.end(), is_ac_der_iec_service);
    if (ac_der_iec_found and not der_iec_limits.has_value()) {
        logf_warning("The supported energy services contain AC_DER_IEC, but there is no der limits defined. This "
                     "can lead to session shutdowns.");
    }

    if (supported_control_mobility_modes.empty()) {
        logf_warning("No control modes were provided, set to scheduled mode");
        supported_control_mobility_modes = {{dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};
    }

    const auto ac_setup_config = config.ac_setup_config.value_or(AcSetupConfig({230, {dt::AcConnector::SinglePhase}}));
    const auto ac_bpt_setup_config = config.bpt_setup_config.value_or(BptSetupConfig(
        {dt::BptChannel::Unified, dt::GeneratorMode::GridFollowing, dt::GridCodeIslandingDetectionMethod::Passive}));
    const auto dc_bpt_setup_config = config.bpt_setup_config.value_or(
        BptSetupConfig({dt::BptChannel::Unified, dt::GeneratorMode::GridFollowing, std::nullopt}));
    der_iec_setup_config = config.der_iec_setup_config.value_or(
        DerIecSetupConfig({{}, iec::OperatingMode::GridFollowing, iec::GridConnectionMode::GridConnected}));

    ac_parameter_list = get_default_ac_parameter_list(supported_control_mobility_modes, ac_setup_config);
    ac_bpt_parameter_list =
        get_default_ac_bpt_parameter_list(supported_control_mobility_modes, ac_setup_config, ac_bpt_setup_config);
    ac_der_iec_parameter_list =
        get_default_ac_der_iec_parameter_list(supported_control_mobility_modes, ac_setup_config, der_iec_setup_config);

    dc_parameter_list = get_default_dc_parameter_list(supported_control_mobility_modes);
    dc_bpt_parameter_list = get_default_dc_bpt_parameter_list(supported_control_mobility_modes, dc_bpt_setup_config);

    mcs_parameter_list = get_default_mcs_parameter_list(supported_control_mobility_modes);
    mcs_bpt_parameter_list = get_default_mcs_bpt_parameter_list(supported_control_mobility_modes, dc_bpt_setup_config);
}

} // namespace iso15118::d20
