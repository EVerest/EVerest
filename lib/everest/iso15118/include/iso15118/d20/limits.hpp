// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message/common_types.hpp>

namespace iso15118::d20 {

namespace dt = message_20::datatypes;

template <typename T> struct Limit {
    T max;
    T min;
};

struct Limits {
    Limit<message_20::datatypes::RationalNumber> power;
    Limit<message_20::datatypes::RationalNumber> current;
};

struct DcTransferLimits {
    Limits charge_limits;
    std::optional<Limits> discharge_limits;
    Limit<message_20::datatypes::RationalNumber> voltage;
    std::optional<message_20::datatypes::RationalNumber> power_ramp_limit;
};

struct AcTransferLimits {
    Limit<dt::RationalNumber> charge_power;
    std::optional<Limit<dt::RationalNumber>> charge_power_L2;
    std::optional<Limit<dt::RationalNumber>> charge_power_L3;

    dt::RationalNumber nominal_frequency;
    std::optional<dt::RationalNumber> max_power_asymmetry;
    std::optional<dt::RationalNumber> power_ramp_limitation;

    std::optional<Limit<dt::RationalNumber>> discharge_power;
    std::optional<Limit<dt::RationalNumber>> discharge_power_L2;
    std::optional<Limit<dt::RationalNumber>> discharge_power_L3;
};

struct IecDerTransferLimits {
    dt::RationalNumber nominal_charge_power;
    std::optional<dt::RationalNumber> nominal_charge_power_L2;
    std::optional<dt::RationalNumber> nominal_charge_power_L3;
    dt::RationalNumber nominal_discharge_power;
    std::optional<dt::RationalNumber> nominal_discharge_power_L2;
    std::optional<dt::RationalNumber> nominal_discharge_power_L3;
    dt::RationalNumber max_discharge_power;
    std::optional<dt::RationalNumber> max_discharge_power_L2;
    std::optional<dt::RationalNumber> max_discharge_power_L3;
    std::optional<dt::RationalNumber> dso_discharge_power;
    std::optional<dt::RationalNumber> dso_discharge_power_L2;
    std::optional<dt::RationalNumber> dso_discharge_power_L3;
};

/// \brief EVSE reactive power capability advertised for AC DER, in var.
///
/// The four unsuffixed members are mandatory, the _L2 and _L3 members are optional per-phase values.
/// Absorption is non-negative and injection is non-positive: ISO 15118-20 AMD1 8.3.5.2 replaces Table 94,
/// so the sign convention binds both annexes.
struct EVSEReactivePowerLimits {
    dt::RationalNumber maximum_var_absorption_during_charging;
    std::optional<dt::RationalNumber> maximum_var_absorption_during_charging_L2;
    std::optional<dt::RationalNumber> maximum_var_absorption_during_charging_L3;
    dt::RationalNumber maximum_var_injection_during_charging;
    std::optional<dt::RationalNumber> maximum_var_injection_during_charging_L2;
    std::optional<dt::RationalNumber> maximum_var_injection_during_charging_L3;
    dt::RationalNumber maximum_var_absorption_during_discharging;
    std::optional<dt::RationalNumber> maximum_var_absorption_during_discharging_L2;
    std::optional<dt::RationalNumber> maximum_var_absorption_during_discharging_L3;
    dt::RationalNumber maximum_var_injection_during_discharging;
    std::optional<dt::RationalNumber> maximum_var_injection_during_discharging_L2;
    std::optional<dt::RationalNumber> maximum_var_injection_during_discharging_L3;
};

/// \brief Grid parameters the EVSE reports for AC DER: nominal_frequency in Hz, the voltages in V.
///
/// min_frequency and max_frequency are optional, the rest is mandatory. PercentageV is taken against a base
/// voltage that is usually but not always nominal_voltage (AMD1 Table M.45), so nominal_voltage and the
/// PercentageV y values of the must-trip curves have to be consistent.
struct GridLimits {
    dt::RationalNumber nominal_frequency;
    dt::RationalNumber nominal_voltage;
    dt::RationalNumber nominal_voltage_offset;
    std::optional<dt::RationalNumber> min_frequency;
    std::optional<dt::RationalNumber> max_frequency;
    dt::RationalNumber maximum_voltage;
    dt::RationalNumber minimum_voltage;
};

/// \brief SAE AC DER transfer limits advertised at charge parameter discovery.
///
/// nominal_charge_power and nominal_discharge_power are optional, max_discharge_power is mandatory.
/// Nominal is set at most equal to maximum [V2G20-3228], [V2G20-3229]. The discharge powers are non-positive:
/// ISO 15118-20 AMD1 8.3.5.2 replaces Table 94, so the sign convention binds both annexes.
struct SaeDerTransferLimits {
    std::optional<dt::RationalNumber> nominal_charge_power;
    std::optional<dt::RationalNumber> nominal_charge_power_L2;
    std::optional<dt::RationalNumber> nominal_charge_power_L3;
    std::optional<dt::RationalNumber> nominal_discharge_power;
    std::optional<dt::RationalNumber> nominal_discharge_power_L2;
    std::optional<dt::RationalNumber> nominal_discharge_power_L3;
    dt::RationalNumber max_discharge_power;
    std::optional<dt::RationalNumber> max_discharge_power_L2;
    std::optional<dt::RationalNumber> max_discharge_power_L3;

    EVSEReactivePowerLimits reactive_power_limits;
    GridLimits grid_limits;
};

} // namespace iso15118::d20
