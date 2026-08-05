// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "der_setup.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/message/common_types.hpp>

#include "conversions.hpp"

namespace module {

namespace {
namespace dt = iso15118::message_20::datatypes;
} // namespace

iso15118::d20::IecDerTransferLimits build_iec_der_transfer_limits(const iso15118::d20::AcTransferLimits& ac_limits) {
    iso15118::d20::IecDerTransferLimits limits{};
    limits.nominal_charge_power = ac_limits.charge_power.max;
    if (ac_limits.discharge_power.has_value()) {
        limits.nominal_discharge_power = ac_limits.discharge_power.value().max;
        limits.max_discharge_power = ac_limits.discharge_power.value().max;
    } else {
        limits.nominal_discharge_power = dt::from_float(0.0f);
        limits.max_discharge_power = dt::from_float(0.0f);
    }
    return limits;
}

iso15118::d20::SaeDerTransferLimits build_sae_der_transfer_limits(const iso15118::d20::AcTransferLimits& ac_limits,
                                                                  std::optional<float> evse_max_reactive_power,
                                                                  std::uint32_t nominal_voltage) {
    iso15118::d20::SaeDerTransferLimits limits{};

    limits.nominal_charge_power = ac_limits.charge_power.max;
    if (ac_limits.discharge_power.has_value()) {
        limits.nominal_discharge_power = ac_limits.discharge_power.value().max;
        limits.max_discharge_power = ac_limits.discharge_power.value().max;
    } else {
        limits.nominal_discharge_power = dt::from_float(0.0f);
        limits.max_discharge_power = dt::from_float(0.0f);
    }

    // All four are mandatory on the wire, so an absent or zero capability yields explicit zeros.
    // Absorption is reported non-negative and injection non-positive, so both signs use the magnitude
    // of the configured capability.
    const auto reactive_magnitude = std::fabs(evse_max_reactive_power.value_or(0.0f));
    const auto absorption = dt::from_float(reactive_magnitude);
    const auto injection = dt::from_float(-reactive_magnitude);
    auto& reactive = limits.reactive_power_limits;
    reactive.maximum_var_absorption_during_charging = absorption;
    reactive.maximum_var_injection_during_charging = injection;
    reactive.maximum_var_absorption_during_discharging = absorption;
    reactive.maximum_var_injection_during_discharging = injection;

    const auto nominal_voltage_f = static_cast<float>(nominal_voltage);
    auto& grid = limits.grid_limits;
    grid.nominal_frequency = ac_limits.nominal_frequency;
    grid.nominal_voltage = dt::from_float(nominal_voltage_f);
    grid.nominal_voltage_offset = dt::from_float(0.0f);
    grid.maximum_voltage = dt::from_float(nominal_voltage_f * OVER_VOLTAGE_TRIP_FRACTION);
    grid.minimum_voltage = dt::from_float(nominal_voltage_f * UNDER_VOLTAGE_TRIP_FRACTION);

    return limits;
}

DerLimitsDerivation derive_der_limits(const std::vector<dt::ServiceCategory>& services,
                                      const iso15118::d20::AcTransferLimits& ac_limits,
                                      std::optional<float> evse_max_reactive_power,
                                      std::optional<std::uint32_t> nominal_voltage) {
    DerLimitsDerivation derivation{};
    derivation.nominal_frequency = dt::from_RationalNumber(ac_limits.nominal_frequency);
    derivation.nominal_voltage = nominal_voltage.value_or(0);

    const auto advertised = [&services](dt::ServiceCategory service) {
        return std::find(services.begin(), services.end(), service) != services.end();
    };

    if (advertised(dt::ServiceCategory::AC_DER_IEC)) {
        derivation.iec_limits = build_iec_der_transfer_limits(ac_limits);
    }

    if (advertised(dt::ServiceCategory::AC_DER_SAE)) {
        if (derivation.nominal_frequency <= 0.0f or derivation.nominal_voltage == 0) {
            derivation.sae_status = SaeDerStatus::GridParametersMissing;
        } else {
            derivation.sae_limits =
                build_sae_der_transfer_limits(ac_limits, evse_max_reactive_power, derivation.nominal_voltage);
            derivation.sae_setup_config.emplace();
            derivation.sae_status = SaeDerStatus::Ready;
        }
    }

    return derivation;
}

DerApplyTransitions apply_derivation(const DerLimitsDerivation& derived, DerAppliedState& current) {
    DerApplyTransitions transitions{};

    if (derived.iec_limits.has_value()) {
        current.iec_limits = derived.iec_limits;
        transitions.iec_assigned = true;
    }

    if (derived.sae_limits.has_value()) {
        current.sae_limits = derived.sae_limits;
        current.sae_setup_config = derived.sae_setup_config;
        transitions.sae = DerSaeApplyTransition::Assigned;
    } else if (current.sae_limits.has_value()) {
        transitions.sae = DerSaeApplyTransition::KeptPrevious;
    } else {
        transitions.sae = DerSaeApplyTransition::NeverDerived;
    }

    return transitions;
}

types::iso15118::DERChargingParameters to_der_charging_parameters(const dt::DER_AC_CPDReqEnergyTransferMode& ev) {
    types::iso15118::DERChargingParameters params{};

    // The EV's active charge/discharge power (max/min_charge_power, max/min_discharge_power) has no
    // 1:1 DERChargingParameters counterpart.
    params.ev_session_total_discharge_energy_available =
        charger::convert_from_optional(ev.session_total_discharge_energy_available);

    if (ev.reactive_power_limits.has_value()) {
        const auto& reactive = ev.reactive_power_limits.value();
        // TODO: clamp EV reactive_power_limits against evse_max_reactive_power (min(EV, EVSE))

        params.max_charge_reactive_power = dt::from_RationalNumber(reactive.max_charge_reactive_power);
        params.max_charge_reactive_power_l2 = charger::convert_from_optional(reactive.max_charge_reactive_power_L2);
        params.max_charge_reactive_power_l3 = charger::convert_from_optional(reactive.max_charge_reactive_power_L3);

        params.min_charge_reactive_power = dt::from_RationalNumber(reactive.min_charge_reactive_power);
        params.min_charge_reactive_power_l2 = charger::convert_from_optional(reactive.min_charge_reactive_power_L2);
        params.min_charge_reactive_power_l3 = charger::convert_from_optional(reactive.min_charge_reactive_power_L3);

        params.max_discharge_reactive_power = dt::from_RationalNumber(reactive.max_discharge_reactive_power);
        params.max_discharge_reactive_power_l2 =
            charger::convert_from_optional(reactive.max_discharge_reactive_power_L2);
        params.max_discharge_reactive_power_l3 =
            charger::convert_from_optional(reactive.max_discharge_reactive_power_L3);

        params.min_discharge_reactive_power = charger::convert_from_optional(reactive.min_discharge_reactive_power);
        params.min_discharge_reactive_power_l2 =
            charger::convert_from_optional(reactive.min_discharge_reactive_power_L2);
        params.min_discharge_reactive_power_l3 =
            charger::convert_from_optional(reactive.min_discharge_reactive_power_L3);
    }

    // supported-DER-control bitmap is in ServiceDetail, not CPDReq; the caller fills
    // ev_supported_dercontrol via map_ev_supported_der_controls, so it is left unset here.

    return params;
}

std::optional<std::vector<types::grid_support::DirectiveType>>
map_ev_supported_der_controls(const std::bitset<12>& selected) {
    using DT = types::grid_support::DirectiveType;
    using iso15118::iec::DERControlName;

    // Bit position (DERControlName) -> grid_support DirectiveType. Over/UnderFrequencyWattMode both
    // surface as FreqWatt; the dedup below keeps the list distinct.
    static constexpr std::pair<DERControlName, DT> table[] = {
        {DERControlName::OverFrequencyWattMode, DT::FreqWatt},
        {DERControlName::UnderFrequencyWattMode, DT::FreqWatt},
        {DERControlName::VoltWattMode, DT::VoltWatt},
        {DERControlName::VoltVarMode, DT::VoltVar},
        {DERControlName::WattVarMode, DT::WattVar},
        {DERControlName::WattCosPhiMode, DT::WattPF},
        {DERControlName::DSOQSetpointProvision, DT::DSOQSetpoint},
        {DERControlName::DSOCosPhiSetpointProvision, DT::DSOCosPhiSetpoint},
        {DERControlName::DCInjectionRestriction, DT::MaximumLevelDCInjection},
        {DERControlName::ZeroCurrentMode, DT::ZeroCurrent},
        {DERControlName::OverVoltageFaultRideThroughMode, DT::OvervoltageFaultRideThrough},
        {DERControlName::UnderVoltageFaultRideThroughMode, DT::UndervoltageFaultRideThrough},
    };

    std::vector<DT> supported;
    for (const auto& [name, directive] : table) {
        if (selected.test(static_cast<size_t>(name)) and
            std::find(supported.begin(), supported.end(), directive) == supported.end()) {
            supported.push_back(directive);
        }
    }

    if (supported.empty()) {
        return std::nullopt;
    }
    return supported;
}

} // namespace module
