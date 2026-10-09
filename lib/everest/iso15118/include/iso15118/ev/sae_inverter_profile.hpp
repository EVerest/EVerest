// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <iso15118/message/ac_der_sae_types.hpp>
#include <iso15118/sae_modes.hpp>

namespace iso15118::ev {

// Static inverter description for AC_DER_SAE. The durations and DER state belong to the
// charge loop request, most other fields to DER_SAE_AC_CPDReqEnergyTransferMode. The CPD
// request carries no frequency.
//
// Power, var, VA and siemens values are totals across AcChargeParams::phase_count lines.
// Power factors are ratios that apply to each line. The defaults form a valid profile.
struct SaeInverterProfile {
    std::string inverter_sw_version{"0.0.1"};
    std::optional<std::string> inverter_hw_version{};
    std::string inverter_manufacturer{"EVerest"};
    std::string inverter_model{"SIL-EV"};
    std::string inverter_serial_number{"SIL-000001"};

    // SupportedModes bitmap, AMD1 Table M.6.
    std::uint32_t supported_modes{sae::sae_function_bit(sae::DerBitMapFunctions::ChargeFunction) |
                                  sae::sae_function_bit(sae::DerBitMapFunctions::DischargeFunction)};

    float max_apparent_power_charging_var_absorption_va{11040.0f};
    float max_apparent_power_charging_var_injection_va{11040.0f};
    float max_apparent_power_discharging_var_absorption_va{11040.0f};
    float max_apparent_power_discharging_var_injection_va{11040.0f};

    float max_var_absorption_charging_var{5000.0f};
    float max_var_injection_charging_var{5000.0f};
    float max_var_absorption_discharging_var{5000.0f};
    float max_var_injection_discharging_var{5000.0f};
    float reactive_susceptance_s{0.0f}; // siemens

    float over_excited_power_factor{0.9f};
    float over_excited_discharge_power_w{9936.0f};
    float under_excited_power_factor{0.9f};
    float under_excited_discharge_power_w{9936.0f};

    float nominal_voltage_v{230.0f};
    float maximum_voltage_v{253.0f};
    float minimum_voltage_v{207.0f};
    float nominal_voltage_offset_v{0.0f};
    float nominal_frequency_hz{50.0f};

    message_20::datatypes::sae::IEEE1547NormalCategory ieee1547_normal_category{
        message_20::datatypes::sae::IEEE1547NormalCategory::CategoryB};
    message_20::datatypes::sae::IEEE1547AbnormalCategory ieee1547_abnormal_category{
        message_20::datatypes::sae::IEEE1547AbnormalCategory::CategoryII};
    bool j3072_certified{false};
    // Seconds since the Unix epoch.
    std::uint64_t j3072_certification_date{0};

    std::uint32_t useable_watt_hours{60000};
    std::uint32_t minimum_charging_duration_s{0};
    std::uint32_t duration_maximum_charge_rate_s{0};
    std::uint32_t duration_maximum_discharge_rate_s{0};

    message_20::datatypes::sae::DEROperationalState operational_state{
        message_20::datatypes::sae::DEROperationalState::On};
    message_20::datatypes::sae::DERConnectionStatus connection_status{
        message_20::datatypes::sae::DERConnectionStatus::Connected};
};

} // namespace iso15118::ev
