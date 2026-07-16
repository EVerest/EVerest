// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "der_relay.hpp"

#include <cmath>

#include <everest/logging.hpp>

namespace module {

namespace {

namespace gs = types::grid_support;
namespace iec = iso15118::iec;

struct ModeMapping {
    iec::DERControlName name;
    iec::CurveDataPointsUnit x_unit;
    gs::DERUnit x_norm_unit;
};

std::optional<ModeMapping> mode_for(gs::DirectiveType type) {
    switch (type) {
    case gs::DirectiveType::VoltVar:
        return ModeMapping{iec::DERControlName::VoltVarMode, iec::CurveDataPointsUnit::V, gs::DERUnit::PctEffectiveV};
    case gs::DirectiveType::WattVar:
        return ModeMapping{iec::DERControlName::WattVarMode, iec::CurveDataPointsUnit::W, gs::DERUnit::PctMaxW};
    case gs::DirectiveType::WattPF:
        return ModeMapping{iec::DERControlName::WattCosPhiMode, iec::CurveDataPointsUnit::W, gs::DERUnit::PctMaxW};
    default:
        return std::nullopt;
    }
}

std::optional<float> denormalize(float pct, gs::DERUnit unit, float volt_base, float watt_base,
                                 std::optional<float> var_base) {
    switch (unit) {
    case gs::DERUnit::PctEffectiveV:
        if (volt_base <= 0.0f) {
            return std::nullopt;
        }
        return pct / 100.0f * volt_base;
    case gs::DERUnit::PctMaxW:
    case gs::DERUnit::PctWAvail:
        if (watt_base <= 0.0f) {
            return std::nullopt;
        }
        return pct / 100.0f * watt_base;
    case gs::DERUnit::PctMaxVar:
    case gs::DERUnit::PctVarAvail:
        if (not var_base.has_value()) {
            return std::nullopt;
        }
        return pct / 100.0f * var_base.value();
    case gs::DERUnit::Not_Applicable:
        return pct;
    }
    return std::nullopt;
}

} // namespace

std::map<iec::DERControlName, iec::DERControlFunction>
map_active_directives_to_der_functions(const gs::ActiveDirectiveSet& directives, float volt_base, float watt_base,
                                       std::optional<float> var_base) {
    std::map<iec::DERControlName, iec::DERControlFunction> result;

    for (const auto& d : directives.directives) {
        const auto mapping = mode_for(d.directive_type);
        if (not mapping.has_value()) {
            EVLOG_warning << "grid_support directive " << gs::directive_type_to_string_view(d.directive_type)
                          << " is not relayable to AC_DER_IEC; skipped";
            continue;
        }

        const auto name = gs::directive_type_to_string_view(d.directive_type);

        if (not d.curve.has_value()) {
            EVLOG_warning << name << " directive missing curve payload; skipped";
            continue;
        }
        const auto& src = d.curve.value();

        if (src.hysteresis.has_value() or src.reactive_power_params.has_value() or src.voltage_params.has_value()) {
            EVLOG_warning << name
                          << " curve carries hysteresis/reactive/voltage params not modeled in AC_DER_IEC; dropped";
        }

        const bool is_power_factor = d.directive_type == gs::DirectiveType::WattPF;

        iec::CurveDataPointsList points;
        bool skip_curve = false;
        const auto point_count = src.curve_data.size();
        const auto kept = std::min<std::size_t>(point_count, iec::CurveDataPointsMaxLength);

        for (std::size_t i = 0; i < kept; ++i) {
            const auto& point = src.curve_data[i];

            const auto x = denormalize(point.x, mapping->x_norm_unit, volt_base, watt_base, var_base);
            if (not x.has_value()) {
                EVLOG_warning << name << " curve x-axis needs an EVSE base that is not configured; skipped";
                skip_curve = true;
                break;
            }

            iec::SetpointExcitation y_value{};
            if (is_power_factor) {
                y_value.setpoint_value = std::fabs(point.y);
                y_value.excitation = point.y >= 0.0f ? iec::PowerFactorExcitation::OverExcited
                                                     : iec::PowerFactorExcitation::UnderExcited;
            } else {
                const auto y = denormalize(point.y, src.y_unit, volt_base, watt_base, var_base);
                if (not y.has_value()) {
                    EVLOG_warning << name << " curve needs EVSE reactive-power base; none configured; skipped";
                    skip_curve = true;
                    break;
                }
                y_value.setpoint_value = y.value();
                y_value.excitation = std::nullopt;
            }

            points.push_back(iec::DataTuple{x.value(), y_value});
        }

        if (skip_curve) {
            continue;
        }

        if (points.empty()) {
            EVLOG_warning << name << " curve has no usable data points; skipped";
            continue;
        }

        if (point_count > iec::CurveDataPointsMaxLength) {
            EVLOG_warning << name << " curve has " << point_count << " points; truncated to "
                          << iec::CurveDataPointsMaxLength << " (AC_DER_IEC wire cap)";
        }

        iec::DERCurve curve{};
        curve.x_unit = mapping->x_unit;
        // power factor rides on SetpointExcitation; unit stays var for all three modes
        curve.y_unit = iec::CurveDataPointsUnit::var;
        curve.curve_data_points = std::move(points);
        curve.min_cos_phi = std::nullopt;
        curve.lock_value_unit = std::nullopt;
        curve.lock_in_value = std::nullopt;
        curve.lock_out_value = std::nullopt;
        curve.pt1_response_reactive_power = false;
        curve.step_response_time_constant_reactive_power = src.response_time.value_or(0.0f);
        curve.intentional_delay = std::nullopt;

        if (result.count(mapping->name) != 0) {
            EVLOG_warning << "grid_support directive " << name
                          << " appears more than once in the active set; last wins";
        }
        result[mapping->name] = std::move(curve);
    }

    return result;
}

} // namespace module
