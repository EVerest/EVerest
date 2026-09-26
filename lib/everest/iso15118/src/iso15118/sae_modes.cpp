// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/sae_modes.hpp>

#include <cstdint>
#include <optional>

#include <iso15118/message/common_types.hpp>

namespace iso15118::sae {

namespace dt_sae = message_20::datatypes::sae;

using DerFn = DerBitMapFunctions;

bool is_function_set(std::uint32_t bitmap, DerBitMapFunctions function) {
    return (bitmap & (1U << message_20::to_underlying_value(function))) != 0U;
}

namespace {

void set_bit_if(std::uint32_t& bitmap, bool enabled, DerFn function) {
    if (enabled) {
        bitmap |= sae_function_bit(function);
    }
}

void set_bit_if(std::uint32_t& bitmap, const std::optional<dt_sae::DERCurve>& curve, DerFn function) {
    set_bit_if(bitmap, curve.has_value() and curve.value().enable, function);
}

// Each Enable maps to one bit. The leaves are shared by DERControlCPDRes and DERControlCLRes.
void add_enabled_modes(std::uint32_t& modes, const dt_sae::VoltageTrip& voltage) {
    set_bit_if(modes, voltage.over_voltage_must_trip_curve.enable, DerFn::HighVoltageMustTripFunction);
    set_bit_if(modes, voltage.under_voltage_must_trip_curve.enable, DerFn::LowVoltageMustTripFunction);
    set_bit_if(modes, voltage.over_voltage_momentary_cessation_trip_curve,
               DerFn::HighVoltageMomentaryCessationFunction);
    set_bit_if(modes, voltage.under_voltage_momentary_cessation_trip_curve,
               DerFn::LowVoltageMomentaryCessationFunction);
    set_bit_if(modes, voltage.over_voltage_may_trip_curve, DerFn::HighVoltageMayTripFunction);
    set_bit_if(modes, voltage.under_voltage_may_trip_curve, DerFn::LowVoltageMayTripFunction);
}

void add_enabled_modes(std::uint32_t& modes, const dt_sae::FrequencyTrip& frequency) {
    set_bit_if(modes, frequency.over_frequency_must_trip_curve.enable, DerFn::HighFrequencyMustTripFunction);
    set_bit_if(modes, frequency.under_frequency_must_trip_curve.enable, DerFn::LowFrequencyMustTripFunction);
    set_bit_if(modes, frequency.over_frequency_may_trip_curve, DerFn::HighFrequencyMayTripFunction);
    set_bit_if(modes, frequency.under_frequency_may_trip_curve, DerFn::LowFrequencyMayTripFunction);
}

void add_enabled_modes(std::uint32_t& modes, const dt_sae::ConstantPowerFactor& constant_power_factor) {
    // One bit per excitation direction; only the one sent is set.
    const auto over_excited =
        constant_power_factor.power_factor_excitation == dt_sae::PowerFactorExcitation::OverExcited;
    set_bit_if(modes, constant_power_factor.enable and over_excited, DerFn::ConstantPowerFactorOverExcitedFunction);
    set_bit_if(modes, constant_power_factor.enable and not over_excited,
               DerFn::ConstantPowerFactorUnderExcitedFunction);
}

void add_enabled_modes(std::uint32_t& modes, const dt_sae::VoltVar& volt_var) {
    set_bit_if(modes, volt_var.enable, DerFn::VoltVarFunction);
}

void add_enabled_modes(std::uint32_t& modes, const dt_sae::WattVar& watt_var) {
    set_bit_if(modes, watt_var.enable, DerFn::WattVarFunction);
}

void add_enabled_modes(std::uint32_t& modes, const dt_sae::ConstantVar& constant_var) {
    set_bit_if(modes, constant_var.enable, DerFn::ConstantReactivePowerFunction);
}

void add_enabled_modes(std::uint32_t& modes, const dt_sae::FrequencyDroop& frequency_droop) {
    set_bit_if(modes, frequency_droop.enable, DerFn::FrequencyDroopFunction);
}

void add_enabled_modes(std::uint32_t& modes, const dt_sae::VoltWatt& volt_watt) {
    set_bit_if(modes, volt_watt.enable, DerFn::VoltWattFunction);
}

void add_enabled_modes(std::uint32_t& modes, const dt_sae::ConstantWatt& constant_watt) {
    set_bit_if(modes, constant_watt.enable, DerFn::ConstantActivePowerFunction);
}

void add_enabled_modes(std::uint32_t& modes, const dt_sae::LimitMaxDischargePower& limit_max_discharge_power) {
    set_bit_if(modes, limit_max_discharge_power.enable, DerFn::LimitMaximumActiveDischargePowerFunction);
}

// Declared after the leaf overloads so lookup finds them. ADL would not: the leaves live in another namespace.
template <typename Leaf> void add_enabled_modes(std::uint32_t& modes, const std::optional<Leaf>& leaf) {
    if (leaf.has_value()) {
        add_enabled_modes(modes, leaf.value());
    }
}

} // namespace

std::uint32_t derive_enabled_modes(const dt_sae::DERControlCPDRes& res) {
    std::uint32_t modes = 0;

    add_enabled_modes(modes, res.voltage_trip);
    add_enabled_modes(modes, res.frequency_trip);

    set_bit_if(modes, res.enter_service_cpd_res.permit_service, DerFn::EnterService);

    const auto& reactive = res.reactive_power_support_cpd_res;
    add_enabled_modes(modes, reactive.constant_power_factor);
    add_enabled_modes(modes, reactive.volt_var);
    add_enabled_modes(modes, reactive.watt_var);
    add_enabled_modes(modes, reactive.constant_var);

    const auto& active = res.active_power_support_cpd_res;
    add_enabled_modes(modes, active.frequency_droop);
    add_enabled_modes(modes, active.volt_watt);
    add_enabled_modes(modes, active.constant_watt);
    add_enabled_modes(modes, active.limit_max_discharge_power);

    return modes;
}

// Same leaves behind optionals; an absent block leaves its bits unset.
std::uint32_t derive_enabled_modes(const dt_sae::DERControlCLRes& res) {
    std::uint32_t modes = 0;

    add_enabled_modes(modes, res.voltage_trip);
    add_enabled_modes(modes, res.frequency_trip);

    set_bit_if(modes, res.enter_service_cl_res.permit_service, DerFn::EnterService);

    if (res.reactive_power_support_cl_res.has_value()) {
        const auto& reactive = res.reactive_power_support_cl_res.value();
        add_enabled_modes(modes, reactive.constant_power_factor);
        add_enabled_modes(modes, reactive.volt_var);
        add_enabled_modes(modes, reactive.watt_var);
        add_enabled_modes(modes, reactive.constant_var);
    }

    if (res.active_power_support_cl_res.has_value()) {
        const auto& active = res.active_power_support_cl_res.value();
        add_enabled_modes(modes, active.frequency_droop);
        add_enabled_modes(modes, active.volt_watt);
        add_enabled_modes(modes, active.constant_watt);
        add_enabled_modes(modes, active.limit_max_discharge_power);
    }

    return modes;
}

std::string sae_function_names(std::uint32_t bitmap) {
    std::string names;
    for_each_sae_function([&names, bitmap](DerFn function) {
        if ((bitmap & sae_function_bit(function)) == 0) {
            return;
        }
        if (not names.empty()) {
            names += ", ";
        }
        names += sae_function_name(function);
    });
    return names.empty() ? std::string{"none"} : names;
}

std::optional<DerBitMapFunctions> parse_sae_function_name(std::string_view name) {
    return enum_from_name<DerFn, SAE_BITMAP_BITS>(name, sae_function_name);
}

} // namespace iso15118::sae
