// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/message/common_types.hpp>

namespace iso15118::d20::ev {

namespace dt = message_20::datatypes;

// DC charge parameters offered by the EV (see flow spec §0/§3, DC branch)
struct DcEvChargeParameters {
    dt::RationalNumber max_charge_power{};
    dt::RationalNumber min_charge_power{};
    dt::RationalNumber max_charge_current{};
    dt::RationalNumber min_charge_current{};
    dt::RationalNumber max_voltage{};
    dt::RationalNumber min_voltage{};
    dt::RationalNumber target_voltage{};
    dt::RationalNumber target_current{};
    dt::RationalNumber energy_capacity{};

    // Dynamic mode energy requests (optional)
    std::optional<dt::RationalNumber> target_energy_request{std::nullopt};
    std::optional<dt::RationalNumber> max_energy_request{std::nullopt};
    std::optional<dt::RationalNumber> min_energy_request{std::nullopt};
};

// DC bidirectional power transfer (BPT) charge parameters, extends the DC set
struct DcEvBptChargeParameters : DcEvChargeParameters {
    dt::RationalNumber max_discharge_power{};
    dt::RationalNumber min_discharge_power{};
    dt::RationalNumber max_discharge_current{};
    dt::RationalNumber min_discharge_current{};

    std::optional<uint8_t> min_soc{std::nullopt};
    std::optional<uint8_t> target_soc{std::nullopt};

    // V2X energy requests (optional, dynamic mode)
    std::optional<dt::RationalNumber> max_v2x_energy_request{std::nullopt};
    std::optional<dt::RationalNumber> min_v2x_energy_request{std::nullopt};
};

// AC charge parameters offered by the EV (see flow spec §3, AC branch)
struct AcEvChargeParameters {
    dt::RationalNumber max_charge_power{};
    dt::RationalNumber min_charge_power{};

    std::optional<dt::RationalNumber> max_charge_power_L2{std::nullopt};
    std::optional<dt::RationalNumber> max_charge_power_L3{std::nullopt};
    std::optional<dt::RationalNumber> min_charge_power_L2{std::nullopt};
    std::optional<dt::RationalNumber> min_charge_power_L3{std::nullopt};

    std::optional<dt::RationalNumber> present_active_power{std::nullopt};
    std::optional<dt::RationalNumber> present_active_power_L2{std::nullopt};
    std::optional<dt::RationalNumber> present_active_power_L3{std::nullopt};

    // BPT discharge limits (optional)
    std::optional<dt::RationalNumber> max_discharge_power{std::nullopt};
    std::optional<dt::RationalNumber> min_discharge_power{std::nullopt};

    // Present reactive power reported in the AC charge loop (optional, SIL default applied when unset)
    std::optional<dt::RationalNumber> present_reactive_power{std::nullopt};
};

// SECC-advertised AC limits received in AC_ChargeParameterDiscoveryRes, stored in EvseInfo. The AC
// counterpart to session::feedback::DcMaximumLimits (there is no AC feedback callback on the EV side).
struct AcMaximumLimits {
    float charge_power{0.0f};
    std::optional<float> charge_power_L2{std::nullopt};
    std::optional<float> charge_power_L3{std::nullopt};
    std::optional<float> discharge_power{std::nullopt};
};

} // namespace iso15118::d20::ev
