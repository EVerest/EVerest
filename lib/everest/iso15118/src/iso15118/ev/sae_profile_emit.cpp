// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/sae_profile_emit.hpp>

#include <limits>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/ev/ac_phase_split.hpp>

namespace iso15118::ev {

namespace {

namespace dt = message_20::datatypes;

// AMD1 Table M.5: J3072CertificationDate is in microseconds of SECC time.
constexpr std::uint64_t MICROSECONDS_PER_SECOND = 1'000'000;

std::uint64_t seconds_to_microseconds_saturated(std::uint64_t seconds) {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    return seconds > max / MICROSECONDS_PER_SECOND ? max : seconds * MICROSECONDS_PER_SECOND;
}

template <typename Mode>
void emit_active_power_limits(Mode& mode, const AcChargeParams& params, dt::AcConnector connector) {
    emit_ac_limit(params.max_charge_power, params.phase_count, connector, mode.max_charge_power,
                  mode.max_charge_power_L2, mode.max_charge_power_L3);
    emit_ac_limit(params.min_charge_power, params.phase_count, connector, mode.min_charge_power,
                  mode.min_charge_power_L2, mode.min_charge_power_L3);
    emit_ac_limit(params.max_discharge_power, params.phase_count, connector, mode.maximum_discharge_power,
                  mode.maximum_discharge_power_L2, mode.maximum_discharge_power_L3);
    emit_ac_limit(params.min_discharge_power, params.phase_count, connector, mode.minimum_discharge_power,
                  mode.minimum_discharge_power_L2, mode.minimum_discharge_power_L3);
}

} // namespace

dt::sae::DER_SAE_AC_CPDReqEnergyTransferMode
make_sae_cpd_transfer_mode(const SaeInverterProfile& profile, const AcChargeParams& params, dt::AcConnector connector,
                           dt::Processing processing, std::uint32_t enabled_modes, std::uint64_t update_time,
                           const d20::SeccClock& secc_clock) {
    dt::sae::DER_SAE_AC_CPDReqEnergyTransferMode mode;

    // Every optional not filled from the inputs is set to nullopt explicitly.

    emit_active_power_limits(mode, params, connector);
    mode.session_total_discharge_energy_available = std::nullopt;

    auto& ap = mode.apparent_power_limits;
    emit_ac_limit(profile.max_apparent_power_charging_var_absorption_va, params.phase_count, connector,
                  ap.maximum_apparent_power_during_charging_and_var_absorption,
                  ap.maximum_apparent_power_during_charging_and_var_absorption_L2,
                  ap.maximum_apparent_power_during_charging_and_var_absorption_L3);
    emit_ac_limit(profile.max_apparent_power_charging_var_injection_va, params.phase_count, connector,
                  ap.maximum_apparent_power_during_charging_and_var_injection,
                  ap.maximum_apparent_power_during_charging_and_var_injection_L2,
                  ap.maximum_apparent_power_during_charging_and_var_injection_L3);
    emit_ac_limit(profile.max_apparent_power_discharging_var_absorption_va, params.phase_count, connector,
                  ap.maximum_apparent_power_during_discharging_and_var_absorption,
                  ap.maximum_apparent_power_during_discharging_and_var_absorption_L2,
                  ap.maximum_apparent_power_during_discharging_and_var_absorption_L3);
    emit_ac_limit(profile.max_apparent_power_discharging_var_injection_va, params.phase_count, connector,
                  ap.maximum_apparent_power_during_discharging_and_var_injection,
                  ap.maximum_apparent_power_during_discharging_and_var_injection_L2,
                  ap.maximum_apparent_power_during_discharging_and_var_injection_L3);

    auto& rp = mode.reactive_power_limits;
    emit_ac_limit(profile.max_var_absorption_charging_var, params.phase_count, connector,
                  rp.maximum_var_absorption_during_charging, rp.maximum_var_absorption_during_charging_L2,
                  rp.maximum_var_absorption_during_charging_L3);
    emit_ac_limit(profile.max_var_injection_charging_var, params.phase_count, connector,
                  rp.maximum_var_injection_during_charging, rp.maximum_var_injection_during_charging_L2,
                  rp.maximum_var_injection_during_charging_L3);
    emit_ac_limit(profile.max_var_absorption_discharging_var, params.phase_count, connector,
                  rp.maximum_var_absorption_during_discharging, rp.maximum_var_absorption_during_discharging_L2,
                  rp.maximum_var_absorption_during_discharging_L3);
    emit_ac_limit(profile.max_var_injection_discharging_var, params.phase_count, connector,
                  rp.maximum_var_injection_during_discharging, rp.maximum_var_injection_during_discharging_L2,
                  rp.maximum_var_injection_during_discharging_L3);
    emit_ac_limit(profile.reactive_susceptance_s, params.phase_count, connector, rp.reactive_susceptance,
                  rp.reactive_susceptance_L2, rp.reactive_susceptance_L3);
    rp.minimum_var_absorption_during_charging = std::nullopt;
    rp.minimum_var_absorption_during_charging_L2 = std::nullopt;
    rp.minimum_var_absorption_during_charging_L3 = std::nullopt;
    rp.minimum_var_injection_during_charging = std::nullopt;
    rp.minimum_var_injection_during_charging_L2 = std::nullopt;
    rp.minimum_var_injection_during_charging_L3 = std::nullopt;
    rp.minimum_var_absorption_during_discharging = std::nullopt;
    rp.minimum_var_absorption_during_discharging_L2 = std::nullopt;
    rp.minimum_var_absorption_during_discharging_L3 = std::nullopt;
    rp.minimum_var_injection_during_discharging = std::nullopt;
    rp.minimum_var_injection_during_discharging_L2 = std::nullopt;
    rp.minimum_var_injection_during_discharging_L3 = std::nullopt;

    auto& ex = mode.excitation_limits;
    emit_ac_ratio(profile.over_excited_power_factor, connector, ex.specified_over_excited_power_factor,
                  ex.specified_over_excited_power_factor_L2, ex.specified_over_excited_power_factor_L3);
    emit_ac_limit(profile.over_excited_discharge_power_w, params.phase_count, connector,
                  ex.specified_over_excited_discharge_power, ex.specified_over_excited_discharge_power_L2,
                  ex.specified_over_excited_discharge_power_L3);
    emit_ac_ratio(profile.under_excited_power_factor, connector, ex.specified_under_excited_power_factor,
                  ex.specified_under_excited_power_factor_L2, ex.specified_under_excited_power_factor_L3);
    emit_ac_limit(profile.under_excited_discharge_power_w, params.phase_count, connector,
                  ex.specified_under_excited_discharge_power, ex.specified_under_excited_discharge_power_L2,
                  ex.specified_under_excited_discharge_power_L3);

    mode.inverter_details.inverter_sw_version = profile.inverter_sw_version;
    mode.inverter_details.inverter_hw_version = profile.inverter_hw_version;
    mode.inverter_details.inverter_manufacturer = profile.inverter_manufacturer;
    mode.inverter_details.inverter_model = profile.inverter_model;
    mode.inverter_details.inverter_serial_number = profile.inverter_serial_number;

    mode.ieee1547_normal_category = profile.ieee1547_normal_category;
    mode.ieee1547_abnormal_category = profile.ieee1547_abnormal_category;
    mode.nominal_voltage = dt::from_float(profile.nominal_voltage_v);
    mode.maximum_voltage = dt::from_float(profile.maximum_voltage_v);
    mode.minimum_voltage = dt::from_float(profile.minimum_voltage_v);
    mode.nominal_voltage_offset = dt::from_float(profile.nominal_voltage_offset_v);
    mode.j3072_certified = profile.j3072_certified;
    mode.j3072_certification_date =
        secc_clock.from_unix(seconds_to_microseconds_saturated(profile.j3072_certification_date));
    mode.useable_watt_hours = profile.useable_watt_hours;

    mode.processing = processing;
    mode.supported_modes = profile.supported_modes & sae::SAE_MODE_BITMAP_MASK;
    mode.enabled_modes = enabled_modes;
    mode.update_time = update_time;

    return mode;
}

dt::sae::DER_Dynamic_AC_CLReqControlMode
make_sae_cl_control_mode(const SaeInverterProfile& profile, const AcChargeParams& params, dt::AcConnector connector,
                         float present_voltage, float present_frequency, std::uint32_t der_alarm_status,
                         std::uint32_t enabled_modes, bool permit_service, std::uint64_t update_time) {
    dt::sae::DER_Dynamic_AC_CLReqControlMode mode;

    // Inherited Dynamic_AC_CLReqControlMode fields, filled as in ac_der_iec_charge_loop.cpp.
    mode.departure_time = std::nullopt;
    mode.target_energy_request = {0, 0};
    mode.max_energy_request = {0, 0};
    mode.min_energy_request = {0, 0};
    emit_active_power_limits(mode, params, connector);
    emit_ac_present(params.present_active_power, params.phase_count, connector, mode.present_active_power,
                    mode.present_active_power_L2, mode.present_active_power_L3);
    mode.present_reactive_power = {0, 0};
    mode.present_reactive_power_L2 = std::nullopt;
    mode.present_reactive_power_L3 = std::nullopt;

    mode.present_voltage = dt::from_float(present_voltage);
    mode.present_frequency = dt::from_float(present_frequency);

    mode.session_total_discharge_energy_available = std::nullopt;
    mode.apparent_power = std::nullopt;
    mode.reactive_power = std::nullopt;
    mode.excitation = std::nullopt;
    mode.maximum_v2x_energy_request = std::nullopt;
    mode.minimum_v2x_energy_request = std::nullopt;

    mode.der_operational_state = permit_service ? profile.operational_state : dt::sae::DEROperationalState::Off;
    mode.der_connection_status =
        permit_service ? profile.connection_status : dt::sae::DERConnectionStatus::Disconnected;

    mode.update_time = update_time;
    mode.minimum_charging_duration = profile.minimum_charging_duration_s;
    mode.duration_maximum_charge_rate = profile.duration_maximum_charge_rate_s;
    mode.duration_maximum_discharge_rate = profile.duration_maximum_discharge_rate_s;
    mode.der_alarm_status = der_alarm_status;
    mode.enabled_modes = enabled_modes;

    return mode;
}

} // namespace iso15118::ev
