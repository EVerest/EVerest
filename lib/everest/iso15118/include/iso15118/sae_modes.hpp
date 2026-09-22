// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/enum_names.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>

namespace iso15118::sae {

constexpr std::uint32_t sae_function_bit(DerBitMapFunctions function) {
    return 1U << static_cast<std::uint32_t>(function);
}

// Bits with no Enable in DERControlCPDRes: charge, discharge and the two charge loop target powers.
inline constexpr std::uint32_t SAE_NOT_ENABLEABLE_MASK =
    sae_function_bit(DerBitMapFunctions::ChargeFunction) | sae_function_bit(DerBitMapFunctions::DischargeFunction) |
    sae_function_bit(DerBitMapFunctions::EVSETargetReactivePowerFunction) |
    sae_function_bit(DerBitMapFunctions::EVSETargetActivePowerFunction);

inline constexpr std::size_t SAE_BITMAP_BITS = 32;

constexpr std::string_view sae_function_name(DerBitMapFunctions function) {
    switch (function) {
    case DerBitMapFunctions::ChargeFunction:
        return "ChargeFunction";
    case DerBitMapFunctions::DischargeFunction:
        return "DischargeFunction";
    case DerBitMapFunctions::EnterService:
        return "EnterService";
    case DerBitMapFunctions::ConstantPowerFactorUnderExcitedFunction:
        return "ConstantPowerFactorUnderExcitedFunction";
    case DerBitMapFunctions::ConstantPowerFactorOverExcitedFunction:
        return "ConstantPowerFactorOverExcitedFunction";
    case DerBitMapFunctions::ConstantReactivePowerFunction:
        return "ConstantReactivePowerFunction";
    case DerBitMapFunctions::ConstantActivePowerFunction:
        return "ConstantActivePowerFunction";
    case DerBitMapFunctions::FrequencyDroopFunction:
        return "FrequencyDroopFunction";
    case DerBitMapFunctions::HighFrequencyMayTripFunction:
        return "HighFrequencyMayTripFunction";
    case DerBitMapFunctions::HighFrequencyMustTripFunction:
        return "HighFrequencyMustTripFunction";
    case DerBitMapFunctions::HighVoltageMayTripFunction:
        return "HighVoltageMayTripFunction";
    case DerBitMapFunctions::HighVoltageMomentaryCessationFunction:
        return "HighVoltageMomentaryCessationFunction";
    case DerBitMapFunctions::HighVoltageMustTripFunction:
        return "HighVoltageMustTripFunction";
    case DerBitMapFunctions::LowFrequencyMayTripFunction:
        return "LowFrequencyMayTripFunction";
    case DerBitMapFunctions::LowFrequencyMustTripFunction:
        return "LowFrequencyMustTripFunction";
    case DerBitMapFunctions::LowVoltageMayTripFunction:
        return "LowVoltageMayTripFunction";
    case DerBitMapFunctions::LowVoltageMomentaryCessationFunction:
        return "LowVoltageMomentaryCessationFunction";
    case DerBitMapFunctions::LowVoltageMustTripFunction:
        return "LowVoltageMustTripFunction";
    case DerBitMapFunctions::LimitMaximumActiveDischargePowerFunction:
        return "LimitMaximumActiveDischargePowerFunction";
    case DerBitMapFunctions::EVSETargetReactivePowerFunction:
        return "EVSETargetReactivePowerFunction";
    case DerBitMapFunctions::EVSETargetActivePowerFunction:
        return "EVSETargetActivePowerFunction";
    case DerBitMapFunctions::VoltVarFunction:
        return "VoltVarFunction";
    case DerBitMapFunctions::VoltWattFunction:
        return "VoltWattFunction";
    case DerBitMapFunctions::WattVarFunction:
        return "WattVarFunction";
    }
    return "";
}

template <typename Visit> constexpr void for_each_sae_function(Visit visit) {
    for_each_enum_value<DerBitMapFunctions, SAE_BITMAP_BITS>(sae_function_name, visit);
}

constexpr bool is_sae_function_enableable(DerBitMapFunctions function) {
    return (sae_function_bit(function) & SAE_NOT_ENABLEABLE_MASK) == 0;
}

constexpr std::uint32_t make_sae_enabled_mode_mask() {
    std::uint32_t mask = 0;
    for_each_sae_function([&mask](DerBitMapFunctions function) {
        if (is_sae_function_enableable(function)) {
            mask |= sae_function_bit(function);
        }
    });
    return mask;
}

// The bits the EnabledModes echo is compared over.
inline constexpr std::uint32_t SAE_ENABLED_MODE_MASK = make_sae_enabled_mode_mask();

constexpr std::size_t count_set_bits(std::uint32_t bits) {
    std::size_t count = 0;
    for (; bits != 0; bits &= bits - 1) {
        ++count;
    }
    return count;
}

constexpr auto make_sae_enableable_functions() {
    std::array<DerBitMapFunctions, count_set_bits(SAE_ENABLED_MODE_MASK)> functions{};
    std::size_t index = 0;
    for_each_sae_function([&functions, &index](DerBitMapFunctions function) {
        if (is_sae_function_enableable(function)) {
            functions[index++] = function;
        }
    });
    return functions;
}

// The functions with an Enable in DERControlCPDRes, in bit order.
inline constexpr auto SAE_ENABLEABLE_FUNCTIONS = make_sae_enableable_functions();

static_assert((SAE_ENABLED_MODE_MASK & ~SAE_MODE_BITMAP_MASK) == 0,
              "the enableable bits must be a subset of the bits this document uses");

bool is_function_set(std::uint32_t bitmap, DerBitMapFunctions function);

// The bits a response enables; both shapes map identical enables to identical bits.
std::uint32_t derive_enabled_modes(const message_20::datatypes::sae::DERControlCPDRes& res);
std::uint32_t derive_enabled_modes(const message_20::datatypes::sae::DERControlCLRes& res);

// Comma separated names of the set bits, "none" if empty. For logging.
std::string sae_function_names(std::uint32_t bitmap);

// Case sensitive; std::nullopt for an unknown name.
std::optional<DerBitMapFunctions> parse_sae_function_name(std::string_view name);

} // namespace iso15118::sae
