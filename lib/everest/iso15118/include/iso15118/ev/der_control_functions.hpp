// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <bitset>
#include <cstddef>

#include <iso15118/d20/der_functions.hpp>

namespace iso15118::ev {

// One bit per iec::DERControlName enumerator; the enum tail pins the count.
constexpr std::size_t DER_CONTROL_FUNCTION_COUNT = 12;

// EV-declared IEC DER control functions, negotiated via the AC_DER_IEC
// ServiceDetail exchange. Each flag maps to one iec::DERControlName; to_bitset
// packs them at the enum's underlying bit positions so the mask lines up with the
// SECC-encoded DERControlFunctions bitmask.
struct DerControlFunctions {
    bool over_frequency_watt_mode{false};
    bool under_frequency_watt_mode{false};
    bool volt_watt_mode{false};
    bool volt_var_mode{false};
    bool watt_var_mode{false};
    bool watt_cos_phi_mode{false};
    bool dso_q_setpoint_provision{false};
    bool dso_cos_phi_setpoint_provision{false};
    bool dc_injection_restriction{false};
    bool zero_current_mode{false};
    bool over_voltage_fault_ride_through_mode{false};
    bool under_voltage_fault_ride_through_mode{false};

    std::bitset<DER_CONTROL_FUNCTION_COUNT> to_bitset() const {
        std::bitset<DER_CONTROL_FUNCTION_COUNT> bits;
        using iso15118::iec::DERControlName;
        bits.set(static_cast<std::size_t>(DERControlName::OverFrequencyWattMode), over_frequency_watt_mode);
        bits.set(static_cast<std::size_t>(DERControlName::UnderFrequencyWattMode), under_frequency_watt_mode);
        bits.set(static_cast<std::size_t>(DERControlName::VoltWattMode), volt_watt_mode);
        bits.set(static_cast<std::size_t>(DERControlName::VoltVarMode), volt_var_mode);
        bits.set(static_cast<std::size_t>(DERControlName::WattVarMode), watt_var_mode);
        bits.set(static_cast<std::size_t>(DERControlName::WattCosPhiMode), watt_cos_phi_mode);
        bits.set(static_cast<std::size_t>(DERControlName::DSOQSetpointProvision), dso_q_setpoint_provision);
        bits.set(static_cast<std::size_t>(DERControlName::DSOCosPhiSetpointProvision), dso_cos_phi_setpoint_provision);
        bits.set(static_cast<std::size_t>(DERControlName::DCInjectionRestriction), dc_injection_restriction);
        bits.set(static_cast<std::size_t>(DERControlName::ZeroCurrentMode), zero_current_mode);
        bits.set(static_cast<std::size_t>(DERControlName::OverVoltageFaultRideThroughMode),
                 over_voltage_fault_ride_through_mode);
        bits.set(static_cast<std::size_t>(DERControlName::UnderVoltageFaultRideThroughMode),
                 under_voltage_fault_ride_through_mode);
        return bits;
    }
};

static_assert(DER_CONTROL_FUNCTION_COUNT ==
                  static_cast<std::size_t>(iso15118::iec::DERControlName::UnderVoltageFaultRideThroughMode) + 1,
              "DER_CONTROL_FUNCTION_COUNT must stay in sync with the iec::DERControlName enum tail");

} // namespace iso15118::ev
