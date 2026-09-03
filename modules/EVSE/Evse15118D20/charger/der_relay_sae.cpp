// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "der_relay_sae.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <everest/logging.hpp>
#include <iso15118/d20/config.hpp>

namespace module {

namespace {

namespace gs = types::grid_support;
namespace sae = iso15118::sae;

/// Annex M elements a directive can write. One writer per slot per call.
enum class Slot {
    OverVoltageMustTrip,
    OverVoltageMayTrip,
    OverVoltageMomentaryCessation,
    UnderVoltageMustTrip,
    UnderVoltageMayTrip,
    UnderVoltageMomentaryCessation,
    OverFrequencyMustTrip,
    OverFrequencyMayTrip,
    UnderFrequencyMustTrip,
    VoltVar,
    WattVar,
    ConstantVar,
    VoltWatt,
    FrequencyDroop,
    LimitMaxDischarge,
    ConstantPowerFactor,
    EnterService,
};

std::optional<Slot> slot_for(gs::DirectiveType type) {
    switch (type) {
    case gs::DirectiveType::HVMustTrip:
        return Slot::OverVoltageMustTrip;
    case gs::DirectiveType::HVMayTrip:
        return Slot::OverVoltageMayTrip;
    case gs::DirectiveType::HVMomCess:
        return Slot::OverVoltageMomentaryCessation;
    case gs::DirectiveType::LVMustTrip:
        return Slot::UnderVoltageMustTrip;
    case gs::DirectiveType::LVMayTrip:
        return Slot::UnderVoltageMayTrip;
    case gs::DirectiveType::LVMomCess:
        return Slot::UnderVoltageMomentaryCessation;
    case gs::DirectiveType::HFMustTrip:
        return Slot::OverFrequencyMustTrip;
    case gs::DirectiveType::HFMayTrip:
        return Slot::OverFrequencyMayTrip;
    case gs::DirectiveType::LFMustTrip:
        return Slot::UnderFrequencyMustTrip;
    case gs::DirectiveType::VoltVar:
        return Slot::VoltVar;
    case gs::DirectiveType::WattVar:
        return Slot::WattVar;
    case gs::DirectiveType::FixedVar:
        return Slot::ConstantVar;
    case gs::DirectiveType::VoltWatt:
        return Slot::VoltWatt;
    case gs::DirectiveType::FreqDroop:
        return Slot::FrequencyDroop;
    case gs::DirectiveType::LimitMaxDischarge:
        return Slot::LimitMaxDischarge;
    case gs::DirectiveType::FixedPFAbsorb:
    case gs::DirectiveType::FixedPFInject:
        return Slot::ConstantPowerFactor;
    case gs::DirectiveType::EnterService:
        return Slot::EnterService;
    default:
        return std::nullopt;
    }
}

/// Which Annex M quantity an element's y axis or setpoint expresses.
enum class UnitFamily {
    ActivePower,
    ReactivePower,
    Voltage,
};

struct MappedUnit {
    sae::DERUnit unit;
    UnitFamily family;
};

std::optional<MappedUnit> map_unit(gs::DERUnit unit) {
    switch (unit) {
    case gs::DERUnit::PctMaxW:
        return MappedUnit{sae::DERUnit::PercentageEVMaximumConfiguredActivePower, UnitFamily::ActivePower};
    case gs::DERUnit::PctWAvail:
        return MappedUnit{sae::DERUnit::PercentageEVMaximumAvailableActivePower, UnitFamily::ActivePower};
    case gs::DERUnit::PctMaxVar:
        return MappedUnit{sae::DERUnit::PercentageEVMaximumConfiguredReactivePower, UnitFamily::ReactivePower};
    case gs::DERUnit::PctVarAvail:
        return MappedUnit{sae::DERUnit::PercentageEVMaximumAvailableReactivePower, UnitFamily::ReactivePower};
    case gs::DERUnit::PctEffectiveV:
        return MappedUnit{sae::DERUnit::PercentageV, UnitFamily::Voltage};
    case gs::DERUnit::Not_Applicable:
        return std::nullopt;
    }
    return std::nullopt;
}

std::string_view type_name(const gs::Directive& d) {
    return gs::directive_type_to_string_view(d.directive_type);
}

void warn_rejected(const gs::Directive& d, std::string_view why) {
    EVLOG_warning << "grid_support directive " << type_name(d) << " (" << d.id << ") rejected: " << why;
}

/// Returns the Annex M unit for a percentage label, or nullopt (after a warning) when the label is missing or
/// belongs to a different quantity than the target element.
std::optional<sae::DERUnit> unit_for(const gs::Directive& d, gs::DERUnit unit, UnitFamily expected) {
    const auto mapped = map_unit(unit);
    if (not mapped.has_value()) {
        warn_rejected(d, "unit is Not_Applicable");
        return std::nullopt;
    }
    if (mapped->family != expected) {
        warn_rejected(d, "unit does not match the Annex M element's quantity");
        return std::nullopt;
    }
    return mapped->unit;
}

/// Annex M Priority is a uint16. An out-of-range grid_support priority is omitted rather than wrapped, so
/// the element carries no fabricated precedence.
std::optional<std::uint16_t> priority_of(const gs::Directive& d) {
    if (d.priority < 0 or d.priority > 65535) {
        // The raw priority still decides slot contention, so such a directive can win its element and then
        // reach the EV with no precedence at all.
        EVLOG_warning << "grid_support directive " << type_name(d) << " (" << d.id << ") priority " << d.priority
                      << " is outside the Annex M uint16 range; the element is written without a priority";
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(d.priority);
}

sae::CurveDataPointsList take_points(const std::vector<gs::DERCurvePoint>& in) {
    sae::CurveDataPointsList points;
    for (const auto& point : in) {
        points.emplace_back(sae::DataTuple{point.x, point.y});
    }
    return points;
}

/// Trip curve per Annex M M.2.2.1.10 and M.2.2.1.11: x is the duration in seconds, y the voltage or frequency
/// threshold, points in strictly ascending duration. grid_support carries the OCPP orientation, where x is the
/// threshold and y the seconds it may be exceeded, so both axes are swapped here. Returns nullopt when two
/// points share a duration.
std::optional<sae::DERCurve> trip_curve(const gs::Directive& d, const gs::DERCurve& in, sae::DERUnit y_unit) {
    auto sorted = in.curve_data;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const gs::DERCurvePoint& a, const gs::DERCurvePoint& b) { return a.y < b.y; });
    const auto duplicate =
        std::adjacent_find(sorted.begin(), sorted.end(),
                           [](const gs::DERCurvePoint& a, const gs::DERCurvePoint& b) { return a.y == b.y; });
    if (duplicate != sorted.end()) {
        warn_rejected(d, "trip curve has two points with the same duration");
        return std::nullopt;
    }

    sae::CurveDataPointsList points;
    for (const auto& point : sorted) {
        points.emplace_back(sae::DataTuple{point.y, point.x});
    }

    sae::DERCurve curve{};
    curve.enable = true;
    curve.priority = priority_of(d);
    curve.x_unit = sae::DERUnit::s;
    curve.y_unit = y_unit;
    curve.curve_data_points = std::move(points);
    curve.curve_data_points_L2 = std::nullopt;
    curve.curve_data_points_L3 = std::nullopt;
    return curve;
}

/// Response curve (VoltVar, WattVar, VoltWatt): points pass through in the given order, percentages untouched.
template <typename Curve>
void response_curve(Curve& out, const gs::Directive& d, const gs::DERCurve& in, sae::DERUnit x_unit,
                    sae::DERUnit y_unit) {
    out.enable = true;
    out.priority = priority_of(d);
    out.x_unit = x_unit;
    out.y_unit = y_unit;
    out.curve_data_points = take_points(in.curve_data);
    out.curve_data_points_L2 = std::nullopt;
    out.curve_data_points_L3 = std::nullopt;
}

void drop_unmodeled_params(const gs::Directive& d, const gs::DERCurve& in) {
    if (in.hysteresis.has_value()) {
        EVLOG_debug << "grid_support directive " << type_name(d) << " (" << d.id
                    << ") hysteresis has no Annex M target; dropped";
    }
    if (in.voltage_params.has_value()) {
        EVLOG_debug << "grid_support directive " << type_name(d) << " (" << d.id
                    << ") voltage_params have no Annex M target; dropped";
    }
}

/// Validates a curve payload; returns a pointer to it, or nullptr after logging the rejection.
const gs::DERCurve* curve_payload(const gs::Directive& d) {
    if (not d.curve.has_value()) {
        warn_rejected(d, "missing curve payload");
        return nullptr;
    }
    if (d.curve->curve_data.size() < 2) {
        warn_rejected(d, "curve needs at least two data points");
        return nullptr;
    }
    if (d.curve->curve_data.size() > sae::CurveDataPointsMaxLength) {
        // Truncating would silently reshape the grid code the operator asked for.
        warn_rejected(d, fmt::format("curve has more than {} points", sae::CurveDataPointsMaxLength));
        return nullptr;
    }
    drop_unmodeled_params(d, *d.curve);
    return &d.curve.value();
}

/// Curve payload plus the y_unit translation only response curves need. Sets \p y_unit on success.
const gs::DERCurve* response_payload(const gs::Directive& d, UnitFamily expected, sae::DERUnit& y_unit) {
    const auto* curve = curve_payload(d);
    if (curve == nullptr) {
        return nullptr;
    }
    const auto unit = unit_for(d, curve->y_unit, expected);
    if (not unit.has_value()) {
        return nullptr;
    }
    y_unit = unit.value();
    return curve;
}

/// Works for the mandatory curves and, through optional's converting assignment, the optional ones.
template <typename Target> bool apply_trip(Target& target, const gs::Directive& d, sae::DERUnit y_unit) {
    const auto* curve = curve_payload(d);
    if (curve == nullptr) {
        return false;
    }
    auto built = trip_curve(d, *curve, y_unit);
    if (not built.has_value()) {
        return false;
    }
    target = std::move(built.value());
    return true;
}

bool apply_volt_var(sae::VoltVar& out, const gs::Directive& d) {
    sae::DERUnit y_unit{};
    const auto* curve = response_payload(d, UnitFamily::ReactivePower, y_unit);
    if (curve == nullptr) {
        return false;
    }
    response_curve(out, d, *curve, sae::DERUnit::PercentageV, y_unit);
    if (curve->response_time.has_value()) {
        out.open_loop_response_time = curve->response_time.value();
    }
    if (curve->reactive_power_params.has_value()) {
        const auto& rp = curve->reactive_power_params.value();
        if (rp.v_ref.has_value()) {
            out.reference_voltage = rp.v_ref.value();
        }
        if (rp.autonomous_v_ref_enable.has_value()) {
            out.autonomous_reference_voltage_adjustment_enable = rp.autonomous_v_ref_enable.value();
        }
        if (rp.autonomous_v_ref_time_constant.has_value()) {
            out.reference_voltage_adjustment_time_constant =
                static_cast<std::uint32_t>(std::lround(std::max(0.0f, rp.autonomous_v_ref_time_constant.value())));
        }
    }
    return true;
}

bool apply_watt_var(sae::WattVar& out, const gs::Directive& d) {
    sae::DERUnit y_unit{};
    const auto* curve = response_payload(d, UnitFamily::ReactivePower, y_unit);
    if (curve == nullptr) {
        return false;
    }
    response_curve(out, d, *curve, sae::DERUnit::PercentageEVMaximumConfiguredActivePower, y_unit);
    out.open_loop_response_time = curve->response_time;
    if (curve->reactive_power_params.has_value()) {
        EVLOG_debug << "grid_support directive " << type_name(d) << " (" << d.id
                    << ") reactive_power_params have no WattVar target; dropped";
    }
    return true;
}

bool apply_volt_watt(sae::VoltWatt& out, const gs::Directive& d) {
    sae::DERUnit y_unit{};
    const auto* curve = response_payload(d, UnitFamily::ActivePower, y_unit);
    if (curve == nullptr) {
        return false;
    }
    response_curve(out, d, *curve, sae::DERUnit::PercentageV, y_unit);
    if (curve->response_time.has_value()) {
        out.open_loop_response_time = curve->response_time.value();
    }
    if (curve->reactive_power_params.has_value()) {
        EVLOG_debug << "grid_support directive " << type_name(d) << " (" << d.id
                    << ") reactive_power_params have no VoltWatt target; dropped";
    }
    return true;
}

/// The Table M.25 VarSetpoint unit set, which spans two quantities and so cannot be expressed as a UnitFamily.
std::optional<sae::DERUnit> constant_var_unit(const gs::Directive& d, gs::DERUnit unit) {
    switch (unit) {
    case gs::DERUnit::PctMaxW:
        return sae::DERUnit::PercentageEVMaximumConfiguredActivePower;
    case gs::DERUnit::PctMaxVar:
        return sae::DERUnit::PercentageEVMaximumConfiguredReactivePower;
    case gs::DERUnit::PctVarAvail:
        return sae::DERUnit::PercentageEVMaximumAvailableReactivePower;
    case gs::DERUnit::PctWAvail:
    case gs::DERUnit::PctEffectiveV:
    case gs::DERUnit::Not_Applicable:
        break;
    }
    warn_rejected(d, "unit is not a Table M.25 VarSetpoint unit");
    return std::nullopt;
}

bool apply_constant_var(sae::ConstantVar& out, const gs::Directive& d) {
    if (not d.fixed_var.has_value()) {
        warn_rejected(d, "missing fixed_var payload");
        return false;
    }
    const auto& fv = d.fixed_var.value();
    const auto unit = constant_var_unit(d, fv.unit);
    if (not unit.has_value()) {
        return false;
    }
    out.enable = true;
    out.priority = priority_of(d);
    // Table M.25: a negative VarSetpoint injects reactive power, a positive one absorbs it. OCPP 2.1
    // FixedVarType.setpoint is documented as "negative = charging, positive = discharging" (2.1 Ed2 part 2,
    // 2.52), and OCPP "charging" is var absorption, so the two conventions are opposed. [V2G20-3183]
    // mandates converting a foreign sign convention.
    out.var_setpoint = -fv.setpoint;
    out.var_setpoint_L2 = std::nullopt;
    out.var_setpoint_L3 = std::nullopt;
    out.unit = unit.value();
    return true;
}

bool apply_frequency_droop(sae::FrequencyDroop& out, const gs::Directive& d, float nominal_frequency_hz) {
    if (not d.freq_droop.has_value()) {
        warn_rejected(d, "missing freq_droop payload");
        return false;
    }
    if (nominal_frequency_hz <= 0.0f) {
        warn_rejected(d, "nominal frequency is not configured");
        return false;
    }
    const auto& fd = d.freq_droop.value();
    if (fd.over_freq < nominal_frequency_hz or fd.under_freq > nominal_frequency_hz) {
        warn_rejected(d, "over_freq must be at or above nominal and under_freq at or below it");
        return false;
    }

    sae::FrequencyDroopSettings over{};
    over.db = fd.over_freq - nominal_frequency_hz;
    over.droop_factor = fd.over_droop;
    over.power_reference = sae::PowerReference::MaximumActivePower;
    over.open_loop_response_time = fd.response_time;

    sae::FrequencyDroopSettings under{};
    under.db = nominal_frequency_hz - fd.under_freq;
    under.droop_factor = fd.under_droop;
    under.power_reference = sae::PowerReference::MaximumActivePower;
    under.open_loop_response_time = fd.response_time;

    out.enable = true;
    out.priority = priority_of(d);
    out.over_frequency_droop = over;
    out.under_frequency_droop = under;
    return true;
}

bool apply_limit_max_discharge(sae::LimitMaxDischargePower& out, const gs::Directive& d) {
    if (not d.limit_max_discharge.has_value() or not d.limit_max_discharge->pct_max_discharge_power.has_value()) {
        warn_rejected(d, "missing pct_max_discharge_power");
        return false;
    }
    const auto raw = d.limit_max_discharge->pct_max_discharge_power.value();
    const auto pct = std::clamp(raw, 0.0f, 100.0f);
    if (pct != raw) {
        EVLOG_warning << "grid_support directive " << type_name(d) << " (" << d.id << ") pct_max_discharge_power "
                      << raw << " clamped to " << pct;
    }
    if (d.limit_max_discharge->power_monitoring_must_trip.has_value()) {
        EVLOG_debug << "grid_support directive " << type_name(d) << " (" << d.id
                    << ") power_monitoring_must_trip has no Annex M target; dropped";
    }
    out.enable = true;
    out.priority = priority_of(d);
    out.percentage_value = static_cast<std::uint16_t>(std::lround(pct));
    out.percentage_value_L2 = std::nullopt;
    out.percentage_value_L3 = std::nullopt;
    return true;
}

bool apply_constant_power_factor(sae::ConstantPowerFactor& out, const gs::Directive& d) {
    if (not d.fixed_pf.has_value()) {
        warn_rejected(d, "missing fixed_pf payload");
        return false;
    }
    const auto& pf = d.fixed_pf.value();
    EVLOG_info << "grid_support directive " << type_name(d) << " (" << d.id
               << ") mapped to Annex M ConstantPowerFactor, which applies in both power directions";
    out.enable = true;
    out.priority = priority_of(d);
    out.power_factor_value = pf.displacement;
    out.power_factor_value_L2 = std::nullopt;
    out.power_factor_value_L3 = std::nullopt;
    out.power_factor_excitation =
        pf.excitation ? sae::PowerFactorExcitation::OverExcited : sae::PowerFactorExcitation::UnderExcited;
    out.power_factor_excitation_L2 = std::nullopt;
    out.power_factor_excitation_L3 = std::nullopt;
    return true;
}

bool apply_enter_service(sae::EnterServiceCPDRes& out, const gs::Directive& d) {
    if (not d.enter_service.has_value()) {
        warn_rejected(d, "missing enter_service payload");
        return false;
    }
    const auto& es = d.enter_service.value();
    if (es.delay.has_value() and not es.ramp_rate.has_value()) {
        warn_rejected(d, "enter_service delay requires ramp_rate");
        return false;
    }
    if (es.ramp_rate.has_value() and not es.delay.has_value()) {
        EVLOG_debug << "grid_support directive " << type_name(d) << " (" << d.id
                    << ") ramp_rate without delay has no Annex M target; dropped";
    }

    out.permit_service = true;
    // AMD1 Table 1 gives both bands in volts, as does grid_support, so no conversion happens here.
    out.enter_service_voltage_high = es.high_voltage;
    out.enter_service_voltage_low = es.low_voltage;
    out.enter_service_frequency_high = es.high_freq;
    out.enter_service_frequency_low = es.low_freq;
    out.enter_service_delay = es.delay;
    out.enter_service_ramp_time = es.delay.has_value() ? es.ramp_rate : std::nullopt;
    if (es.delay.has_value() or es.random_delay.has_value()) {
        out.enter_service_randomized_delay = es.random_delay;
    } else {
        // [V2G20-3364] requires a delay element; zero stays inert.
        out.enter_service_randomized_delay = 0.0f;
    }
    return true;
}

bool apply(sae::DERControl& control, Slot slot, const gs::Directive& d, float nominal_frequency_hz) {
    auto& vt = control.voltage_trip;
    auto& ft = control.frequency_trip;
    auto& rps = control.reactive_power_support;
    auto& aps = control.active_power_support;

    switch (slot) {
    case Slot::OverVoltageMustTrip:
        return apply_trip(vt.over_voltage_must_trip_curve, d, sae::DERUnit::PercentageV);
    case Slot::OverVoltageMayTrip:
        return apply_trip(vt.over_voltage_may_trip_curve, d, sae::DERUnit::PercentageV);
    case Slot::OverVoltageMomentaryCessation:
        return apply_trip(vt.over_voltage_momentary_cessation_trip_curve, d, sae::DERUnit::PercentageV);
    case Slot::UnderVoltageMustTrip:
        return apply_trip(vt.under_voltage_must_trip_curve, d, sae::DERUnit::PercentageV);
    case Slot::UnderVoltageMayTrip:
        return apply_trip(vt.under_voltage_may_trip_curve, d, sae::DERUnit::PercentageV);
    case Slot::UnderVoltageMomentaryCessation:
        return apply_trip(vt.under_voltage_momentary_cessation_trip_curve, d, sae::DERUnit::PercentageV);
    case Slot::OverFrequencyMustTrip:
        return apply_trip(ft.over_frequency_must_trip_curve, d, sae::DERUnit::Hz);
    case Slot::OverFrequencyMayTrip:
        return apply_trip(ft.over_frequency_may_trip_curve, d, sae::DERUnit::Hz);
    case Slot::UnderFrequencyMustTrip:
        return apply_trip(ft.under_frequency_must_trip_curve, d, sae::DERUnit::Hz);
    case Slot::VoltVar:
        return apply_volt_var(rps.volt_var, d);
    case Slot::WattVar:
        return apply_watt_var(rps.watt_var, d);
    case Slot::ConstantVar:
        return apply_constant_var(rps.constant_var, d);
    case Slot::VoltWatt:
        return apply_volt_watt(aps.volt_watt, d);
    case Slot::FrequencyDroop:
        return apply_frequency_droop(aps.frequency_droop, d, nominal_frequency_hz);
    case Slot::LimitMaxDischarge:
        return apply_limit_max_discharge(aps.limit_max_discharge_power, d);
    case Slot::ConstantPowerFactor:
        return apply_constant_power_factor(rps.constant_power_factor, d);
    case Slot::EnterService:
        return apply_enter_service(control.enter_service, d);
    }
    return false;
}

} // namespace

SaeRelayResult map_active_directives_to_sae_der_control(const gs::ActiveDirectiveSet& directives,
                                                        float nominal_voltage_v, float nominal_frequency_hz) {
    SaeRelayResult result{};
    result.der_control = iso15118::d20::get_default_sae_der_control(nominal_voltage_v);

    if (nominal_voltage_v <= 0.0f) {
        // The default is derived from the nominal, so without it there is no baseline to map onto.
        EVLOG_warning << "grid_support DER directives cannot be mapped to AC_DER_SAE: nominal voltage not configured";
        return result;
    }

    const auto& in = directives.directives;
    std::vector<std::size_t> order(in.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&in](std::size_t a, std::size_t b) { return in[a].priority < in[b].priority; });

    std::set<Slot> written;
    std::set<gs::DirectiveType> unmapped_logged;

    for (const auto idx : order) {
        const auto& d = in[idx];
        const auto slot = slot_for(d.directive_type);
        if (not slot.has_value()) {
            if (unmapped_logged.insert(d.directive_type).second) {
                EVLOG_warning << "grid_support directive " << type_name(d) << " has no AC_DER_SAE target; skipped";
                result.unmapped.push_back(d.directive_type);
            }
            continue;
        }
        if (written.count(slot.value()) != 0) {
            EVLOG_warning << "grid_support directive " << type_name(d) << " (" << d.id
                          << ") shadowed by a higher-precedence directive on the same Annex M element";
            result.shadowed_ids.push_back(d.id);
            continue;
        }
        if (apply(result.der_control, slot.value(), d, nominal_frequency_hz)) {
            written.insert(slot.value());
        }
    }

    return result;
}

bool SaeRelayInput::operator==(const SaeRelayInput& other) const {
    return directives == other.directives and nominal_voltage_v == other.nominal_voltage_v and
           nominal_frequency_hz == other.nominal_frequency_hz;
}

SaeRelayInput sae_relay_input(const gs::ActiveDirectiveSet& directives, float nominal_voltage_v,
                              float nominal_frequency_hz) {
    SaeRelayInput input{};
    input.directives.reserve(directives.directives.size());
    for (const auto& d : directives.directives) {
        input.directives.emplace_back(d.id, d.received_at);
    }
    std::sort(input.directives.begin(), input.directives.end());
    input.nominal_voltage_v = nominal_voltage_v;
    input.nominal_frequency_hz = nominal_frequency_hz;
    return input;
}

std::vector<std::string> inert_sae_der_functions(const sae::DERControl& control) {
    std::vector<std::string> inert;
    const auto check = [&inert](const char* name, bool enabled) {
        if (not enabled) {
            inert.emplace_back(name);
        }
    };
    const auto optional_enabled = [](const std::optional<sae::DERCurve>& curve) {
        return curve.has_value() and curve->enable;
    };

    const auto& vt = control.voltage_trip;
    check("OverVoltageMustTrip", vt.over_voltage_must_trip_curve.enable);
    check("UnderVoltageMustTrip", vt.under_voltage_must_trip_curve.enable);
    check("OverVoltageMomentaryCessation", optional_enabled(vt.over_voltage_momentary_cessation_trip_curve));
    check("UnderVoltageMomentaryCessation", optional_enabled(vt.under_voltage_momentary_cessation_trip_curve));
    check("OverVoltageMayTrip", optional_enabled(vt.over_voltage_may_trip_curve));
    check("UnderVoltageMayTrip", optional_enabled(vt.under_voltage_may_trip_curve));

    const auto& ft = control.frequency_trip;
    check("OverFrequencyMustTrip", ft.over_frequency_must_trip_curve.enable);
    check("UnderFrequencyMustTrip", ft.under_frequency_must_trip_curve.enable);
    check("OverFrequencyMayTrip", optional_enabled(ft.over_frequency_may_trip_curve));
    check("UnderFrequencyMayTrip", optional_enabled(ft.under_frequency_may_trip_curve));

    check("EnterService", control.enter_service.permit_service);

    const auto& rps = control.reactive_power_support;
    check("ConstantPowerFactor", rps.constant_power_factor.enable);
    check("VoltVar", rps.volt_var.enable);
    check("WattVar", rps.watt_var.enable);
    check("ConstantVar", rps.constant_var.enable);

    const auto& aps = control.active_power_support;
    check("FrequencyDroop", aps.frequency_droop.enable);
    check("VoltWatt", aps.volt_watt.enable);
    check("ConstantWatt", aps.constant_watt.enable);
    check("LimitMaxDischargePower", aps.limit_max_discharge_power.enable);

    return inert;
}

} // namespace module
