// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "der_setup.hpp"

#include <algorithm>
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

types::iso15118::DERChargingParameters to_der_charging_parameters(const dt::DER_AC_CPDReqEnergyTransferMode& ev) {
    types::iso15118::DERChargingParameters params{};

    // The EV's active charge/discharge power (max/min_charge_power, max/min_discharge_power) has no
    // 1:1 DERChargingParameters counterpart.
    params.ev_session_total_discharge_energy_available =
        charger::convert_from_optional(ev.session_total_discharge_energy_available);

    if (ev.reactive_power_limits.has_value()) {
        const auto& reactive = ev.reactive_power_limits.value();

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
