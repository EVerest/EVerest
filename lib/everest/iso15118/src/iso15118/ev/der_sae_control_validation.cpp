// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/der_sae_control_validation.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

// Diagnostics only: no Annex M requirement makes the EV reject a session on these.

namespace iso15118::ev {

namespace dt = message_20::datatypes;

namespace {

using dt::sae::CurveDataPointsList;
using dt::sae::CurveDataPointsMinLength;
using dt::sae::DERUnit;

const char* unit_name(DERUnit unit) {
    switch (unit) {
    case DERUnit::V:
        return "V";
    case DERUnit::Hz:
        return "Hz";
    case DERUnit::W:
        return "W";
    case DERUnit::s:
        return "s";
    case DERUnit::var:
        return "var";
    case DERUnit::PercentageEVMaximumConfiguredActivePower:
        return "PercentageEVMaximumConfiguredActivePower";
    case DERUnit::PercentageEVMaximumConfiguredReactivePower:
        return "PercentageEVMaximumConfiguredReactivePower";
    case DERUnit::PercentageEVMaximumConfiguredApparentPower:
        return "PercentageEVMaximumConfiguredApparentPower";
    case DERUnit::PercentageEVMaximumAvailableActivePower:
        return "PercentageEVMaximumAvailableActivePower";
    case DERUnit::PercentageEVMaximumAvailableReactivePower:
        return "PercentageEVMaximumAvailableReactivePower";
    case DERUnit::PercentageV:
        return "PercentageV";
    }
    return "unknown";
}

std::string format_value(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
    return buffer;
}

struct UnitSet {
    std::vector<DERUnit> units;

    bool contains(DERUnit unit) const {
        for (const auto accepted : units) {
            if (accepted == unit) {
                return true;
            }
        }
        return false;
    }

    std::string names() const {
        std::string out = "{";
        for (const auto unit : units) {
            if (out.size() > 1) {
                out += ", ";
            }
            out += unit_name(unit);
        }
        return out + "}";
    }
};

struct CurveUnitRule {
    UnitSet x;
    UnitSet y;
    // AMD1 M.2.2.1.10 and M.2.2.1.11: trip curve x is a duration in s.
    bool x_is_duration{false};
};

const CurveUnitRule voltage_trip_rule{{{DERUnit::s}}, {{DERUnit::V, DERUnit::PercentageV}}, true};
const CurveUnitRule frequency_trip_rule{{{DERUnit::s}}, {{DERUnit::Hz}}, true};
// Physical sanity check beyond AMD1 Tables M.28 and M.29, which only say derUnitType.
const CurveUnitRule volt_var_rule{{{DERUnit::V, DERUnit::PercentageV}},
                                  {{DERUnit::var, DERUnit::PercentageEVMaximumConfiguredReactivePower,
                                    DERUnit::PercentageEVMaximumAvailableReactivePower}}};
const CurveUnitRule watt_var_rule{
    {{DERUnit::W, DERUnit::PercentageEVMaximumConfiguredActivePower, DERUnit::PercentageEVMaximumAvailableActivePower}},
    {{DERUnit::var, DERUnit::PercentageEVMaximumConfiguredReactivePower,
      DERUnit::PercentageEVMaximumAvailableReactivePower}}};
const CurveUnitRule volt_watt_rule{{{DERUnit::V, DERUnit::PercentageV}},
                                   {{DERUnit::W, DERUnit::PercentageEVMaximumConfiguredActivePower,
                                     DERUnit::PercentageEVMaximumAvailableActivePower}}};
// AMD1 Table M.26.
const UnitSet constant_watt_units{{DERUnit::PercentageEVMaximumConfiguredActivePower,
                                   DERUnit::PercentageEVMaximumConfiguredApparentPower,
                                   DERUnit::PercentageEVMaximumAvailableActivePower}};
// AMD1 Table M.25.
const UnitSet constant_var_units{{DERUnit::PercentageEVMaximumConfiguredActivePower,
                                  DERUnit::PercentageEVMaximumConfiguredReactivePower,
                                  DERUnit::PercentageEVMaximumAvailableReactivePower}};

void check_points(const std::string& site, const CurveDataPointsList& points, DerControlProblems& problems) {
    // The type caps the maximum. The decoder enforces the minimum on received messages;
    // this check covers messages built in code.
    if (points.size() < CurveDataPointsMinLength) {
        problems.push_back(site + ": " + std::to_string(points.size()) + " point(s), minimum is " +
                           std::to_string(CurveDataPointsMinLength));
        return;
    }

    auto previous = dt::from_RationalNumber(points[0].x_value);
    for (std::size_t i = 1; i < points.size(); ++i) {
        const auto current = dt::from_RationalNumber(points[i].x_value);
        if (current < previous) {
            problems.push_back(site + ": x decreases at point[" + std::to_string(i) + "] (" + format_value(current) +
                               " after " + format_value(previous) + ")");
            return;
        }
        previous = current;
    }
}

template <typename CurveT>
void check_curve(const std::string& site, const CurveT& curve, const CurveUnitRule& rule,
                 DerControlProblems& problems) {
    if (not curve.enable) {
        return;
    }
    if (not rule.x.contains(curve.x_unit)) {
        std::string message = site + ": x_unit " + unit_name(curve.x_unit) + " not in " + rule.x.names();
        if (rule.x_is_duration) {
            message += " (trip curves carry the duration on x)";
        }
        problems.push_back(std::move(message));
    }
    if (not rule.y.contains(curve.y_unit)) {
        problems.push_back(site + ": y_unit " + unit_name(curve.y_unit) + " not in " + rule.y.names());
    }

    check_points(site, curve.curve_data_points, problems);
    if (curve.curve_data_points_L2) {
        check_points(site + "(L2)", *curve.curve_data_points_L2, problems);
    }
    if (curve.curve_data_points_L3) {
        check_points(site + "(L3)", *curve.curve_data_points_L3, problems);
    }
}

template <typename CurveT>
void check_optional_curve(const std::string& site, const std::optional<CurveT>& curve, const CurveUnitRule& rule,
                          DerControlProblems& problems) {
    if (curve) {
        check_curve(site, *curve, rule, problems);
    }
}

template <typename SetpointT>
void check_unit(const std::string& site, const SetpointT& setpoint, const UnitSet& accepted,
                DerControlProblems& problems) {
    if (setpoint.enable and not accepted.contains(setpoint.unit)) {
        problems.push_back(site + ": unit " + unit_name(setpoint.unit) + " not in " + accepted.names());
    }
}

void check_power_factor_value(const std::string& site, const dt::RationalNumber& value, DerControlProblems& problems) {
    const auto factor = dt::from_RationalNumber(value);
    if (not std::isfinite(factor) or factor <= 0.0f or factor > 1.0f) {
        problems.push_back(site + ": power_factor_value " + format_value(factor) + " not in (0, 1]");
    }
}

void check_power_factor(const dt::sae::ConstantPowerFactor& setpoint, DerControlProblems& problems) {
    if (not setpoint.enable) {
        return;
    }
    const std::string site = "ReactivePowerSupport.constant_power_factor";
    check_power_factor_value(site, setpoint.power_factor_value, problems);
    if (setpoint.power_factor_value_L2) {
        check_power_factor_value(site + "(L2)", *setpoint.power_factor_value_L2, problems);
    }
    if (setpoint.power_factor_value_L3) {
        check_power_factor_value(site + "(L3)", *setpoint.power_factor_value_L3, problems);
    }
}

// AMD1 Table M.35.
constexpr std::uint16_t max_discharge_percentage = 100;

void check_discharge_percentage(const std::string& site, std::uint16_t value, DerControlProblems& problems) {
    if (value > max_discharge_percentage) {
        problems.push_back(site + ": percentage_value " + std::to_string(value) + " not in [0, " +
                           std::to_string(max_discharge_percentage) + "]");
    }
}

void check_limit_max_discharge_power(const dt::sae::LimitMaxDischargePower& limit, DerControlProblems& problems) {
    if (not limit.enable) {
        return;
    }
    const std::string site = "ActivePowerSupport.limit_max_discharge_power";
    check_discharge_percentage(site, limit.percentage_value, problems);
    if (limit.percentage_value_L2) {
        check_discharge_percentage(site + "(L2)", *limit.percentage_value_L2, problems);
    }
    if (limit.percentage_value_L3) {
        check_discharge_percentage(site + "(L3)", *limit.percentage_value_L3, problems);
    }
}

void check_voltage_trip(const dt::sae::VoltageTrip& trip, DerControlProblems& problems) {
    const auto& rule = voltage_trip_rule;
    check_curve("VoltageTrip.over_voltage_must_trip_curve", trip.over_voltage_must_trip_curve, rule, problems);
    check_curve("VoltageTrip.under_voltage_must_trip_curve", trip.under_voltage_must_trip_curve, rule, problems);
    check_optional_curve("VoltageTrip.over_voltage_momentary_cessation_trip_curve",
                         trip.over_voltage_momentary_cessation_trip_curve, rule, problems);
    check_optional_curve("VoltageTrip.under_voltage_momentary_cessation_trip_curve",
                         trip.under_voltage_momentary_cessation_trip_curve, rule, problems);
    check_optional_curve("VoltageTrip.over_voltage_may_trip_curve", trip.over_voltage_may_trip_curve, rule, problems);
    check_optional_curve("VoltageTrip.under_voltage_may_trip_curve", trip.under_voltage_may_trip_curve, rule, problems);
}

void check_frequency_trip(const dt::sae::FrequencyTrip& trip, DerControlProblems& problems) {
    const auto& rule = frequency_trip_rule;
    check_curve("FrequencyTrip.over_frequency_must_trip_curve", trip.over_frequency_must_trip_curve, rule, problems);
    check_curve("FrequencyTrip.under_frequency_must_trip_curve", trip.under_frequency_must_trip_curve, rule, problems);
    check_optional_curve("FrequencyTrip.over_frequency_may_trip_curve", trip.over_frequency_may_trip_curve, rule,
                         problems);
    check_optional_curve("FrequencyTrip.under_frequency_may_trip_curve", trip.under_frequency_may_trip_curve, rule,
                         problems);
}

} // namespace

DerControlProblems validate_der_control(const dt::sae::DERControlCPDRes& control) {
    DerControlProblems problems;

    check_voltage_trip(control.voltage_trip, problems);
    check_frequency_trip(control.frequency_trip, problems);

    const auto& reactive = control.reactive_power_support_cpd_res;
    check_power_factor(reactive.constant_power_factor, problems);
    check_curve("ReactivePowerSupport.volt_var", reactive.volt_var, volt_var_rule, problems);
    check_curve("ReactivePowerSupport.watt_var", reactive.watt_var, watt_var_rule, problems);
    check_unit("ReactivePowerSupport.constant_var", reactive.constant_var, constant_var_units, problems);

    const auto& active = control.active_power_support_cpd_res;
    check_curve("ActivePowerSupport.volt_watt", active.volt_watt, volt_watt_rule, problems);
    check_unit("ActivePowerSupport.constant_watt", active.constant_watt, constant_watt_units, problems);
    check_limit_max_discharge_power(active.limit_max_discharge_power, problems);

    return problems;
}

DerControlProblems validate_der_control(const dt::sae::DERControlCLRes& control) {
    DerControlProblems problems;

    if (control.voltage_trip) {
        check_voltage_trip(*control.voltage_trip, problems);
    }
    if (control.frequency_trip) {
        check_frequency_trip(*control.frequency_trip, problems);
    }

    if (control.reactive_power_support_cl_res) {
        const auto& reactive = *control.reactive_power_support_cl_res;
        if (reactive.constant_power_factor) {
            check_power_factor(*reactive.constant_power_factor, problems);
        }
        check_optional_curve("ReactivePowerSupport.volt_var", reactive.volt_var, volt_var_rule, problems);
        check_optional_curve("ReactivePowerSupport.watt_var", reactive.watt_var, watt_var_rule, problems);
        if (reactive.constant_var) {
            check_unit("ReactivePowerSupport.constant_var", *reactive.constant_var, constant_var_units, problems);
        }
    }

    if (control.active_power_support_cl_res) {
        const auto& active = *control.active_power_support_cl_res;
        check_optional_curve("ActivePowerSupport.volt_watt", active.volt_watt, volt_watt_rule, problems);
        if (active.constant_watt) {
            check_unit("ActivePowerSupport.constant_watt", *active.constant_watt, constant_watt_units, problems);
        }
        if (active.limit_max_discharge_power) {
            check_limit_max_discharge_power(*active.limit_max_discharge_power, problems);
        }
    }

    return problems;
}

} // namespace iso15118::ev
