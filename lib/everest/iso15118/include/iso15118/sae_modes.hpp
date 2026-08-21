// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>

namespace iso15118::sae {

// Bits 2, 9, 25 and 27 to 31 are unused by the specification and must be ignored.
inline constexpr std::uint32_t SAE_MODE_BITMAP_MASK = 0x05FFFDFBu;

constexpr std::uint32_t sae_function_bit(DerBitMapFunctions function) {
    return 1U << static_cast<std::uint32_t>(function);
}

struct SaeFunctionName {
    DerBitMapFunctions function{};
    std::string_view name{};
};

// Bits without a matching Enable in DERControlCPDRes: bits 0 and 1 (charge, discharge) are inherent to the
// service and bits 21 and 22 (charge loop target powers) carry no Enable, so neither group is ever enabled by
// the SECC.
inline constexpr std::uint32_t SAE_NOT_ENABLEABLE_MASK =
    sae_function_bit(DerBitMapFunctions::ChargeFunction) | sae_function_bit(DerBitMapFunctions::DischargeFunction) |
    sae_function_bit(DerBitMapFunctions::EVSETargetReactivePowerFunction) |
    sae_function_bit(DerBitMapFunctions::EVSETargetActivePowerFunction);

// Every enumerator: charge, discharge and the charge loop target powers have no Enable in DERControlCPDRes
// but a profile's supported_modes list must be able to name them. Names match the gate_enable call sites in
// ac_der_sae_convert.cpp so table and gates can be audited side by side.
inline constexpr std::array<SaeFunctionName, 24> SAE_FUNCTION_NAMES{{
    {DerBitMapFunctions::ChargeFunction, "charge"},
    {DerBitMapFunctions::DischargeFunction, "discharge"},
    {DerBitMapFunctions::EnterService, "enter service"},
    {DerBitMapFunctions::ConstantPowerFactorUnderExcitedFunction, "constant power factor under excited"},
    {DerBitMapFunctions::ConstantPowerFactorOverExcitedFunction, "constant power factor over excited"},
    {DerBitMapFunctions::ConstantReactivePowerFunction, "constant var"},
    {DerBitMapFunctions::ConstantActivePowerFunction, "constant watt"},
    {DerBitMapFunctions::FrequencyDroopFunction, "frequency droop"},
    {DerBitMapFunctions::HighFrequencyMayTripFunction, "over frequency may trip curve"},
    {DerBitMapFunctions::HighFrequencyMustTripFunction, "over frequency must trip curve"},
    {DerBitMapFunctions::HighVoltageMayTripFunction, "over voltage may trip curve"},
    {DerBitMapFunctions::HighVoltageMomentaryCessationFunction, "over voltage momentary cessation trip curve"},
    {DerBitMapFunctions::HighVoltageMustTripFunction, "over voltage must trip curve"},
    {DerBitMapFunctions::LowFrequencyMayTripFunction, "under frequency may trip curve"},
    {DerBitMapFunctions::LowFrequencyMustTripFunction, "under frequency must trip curve"},
    {DerBitMapFunctions::LowVoltageMayTripFunction, "under voltage may trip curve"},
    {DerBitMapFunctions::LowVoltageMomentaryCessationFunction, "under voltage momentary cessation trip curve"},
    {DerBitMapFunctions::LowVoltageMustTripFunction, "under voltage must trip curve"},
    {DerBitMapFunctions::LimitMaximumActiveDischargePowerFunction, "limit maximum discharge power"},
    {DerBitMapFunctions::EVSETargetReactivePowerFunction, "evse target reactive power"},
    {DerBitMapFunctions::EVSETargetActivePowerFunction, "evse target active power"},
    {DerBitMapFunctions::VoltVarFunction, "volt var"},
    {DerBitMapFunctions::VoltWattFunction, "volt watt"},
    {DerBitMapFunctions::WattVarFunction, "watt var"},
}};

constexpr bool is_sae_function_enableable(DerBitMapFunctions function) {
    return (sae_function_bit(function) & SAE_NOT_ENABLEABLE_MASK) == 0;
}

constexpr std::size_t count_sae_enableable_functions() {
    std::size_t count = 0;
    for (const auto& entry : SAE_FUNCTION_NAMES) {
        if (is_sae_function_enableable(entry.function)) {
            ++count;
        }
    }
    return count;
}

constexpr auto make_sae_enableable_functions() {
    std::array<SaeFunctionName, count_sae_enableable_functions()> functions{};
    std::size_t index = 0;
    for (const auto& entry : SAE_FUNCTION_NAMES) {
        if (is_sae_function_enableable(entry.function)) {
            functions[index++] = entry;
        }
    }
    return functions;
}

// Every bit with a matching Enable in DERControlCPDRes, filtered from the complete table so names cannot
// drift between the two.
inline constexpr auto SAE_ENABLEABLE_FUNCTIONS = make_sae_enableable_functions();

constexpr std::uint32_t make_sae_enabled_mode_mask() {
    std::uint32_t mask = 0;
    for (const auto& entry : SAE_ENABLEABLE_FUNCTIONS) {
        mask |= sae_function_bit(entry.function);
    }
    return mask;
}

// The echo comparison runs over these bits only.
inline constexpr std::uint32_t SAE_ENABLED_MODE_MASK = make_sae_enabled_mode_mask();

static_assert((SAE_ENABLED_MODE_MASK & ~SAE_MODE_BITMAP_MASK) == 0,
              "the enableable bits must be a subset of the bits this document uses");

bool is_function_set(std::uint32_t bitmap, DerBitMapFunctions function);

// The bits the gated response actually enables, and the reference the EV's EnabledModes echo is compared
// against in the charge loop. Both response shapes are read the same way, so identical enables yield
// identical bits.
std::uint32_t derive_enabled_modes(const message_20::datatypes::sae::DERControlCPDRes& res);
std::uint32_t derive_enabled_modes(const message_20::datatypes::sae::DERControlCLRes& res);

// Comma separated function names for the set bits, "none" for an empty bitmap. For log lines only.
std::string sae_function_names(std::uint32_t bitmap);

// Case sensitive lookup in the complete table; std::nullopt for an unknown name.
std::optional<DerBitMapFunctions> parse_sae_function_name(std::string_view name);

} // namespace iso15118::sae
