// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/der_functions.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>

namespace iso15118::d20::state {

namespace dt_sae = message_20::datatypes::sae;

void convert(dt_sae::CurveDataPointsList& out, const sae::CurveDataPointsList& in);
void convert(dt_sae::DERCurve& out, const sae::DERCurve& in);
void convert(dt_sae::VoltageTrip& out, const sae::VoltageTrip& in);
void convert(dt_sae::FrequencyTrip& out, const sae::FrequencyTrip& in);
void convert(dt_sae::EnterServiceCPDRes& out, const sae::EnterServiceCPDRes& in);

void convert(dt_sae::ConstantPowerFactor& out, const sae::ConstantPowerFactor& in);
void convert(dt_sae::VoltVar& out, const sae::VoltVar& in);
void convert(dt_sae::WattVar& out, const sae::WattVar& in);
void convert(dt_sae::ConstantVar& out, const sae::ConstantVar& in);
void convert(dt_sae::ReactivePowerSupportCPDRes& out, const sae::ReactivePowerSupportCPDRes& in);

void convert(dt_sae::FrequencyDroopSettings& out, const sae::FrequencyDroopSettings& in);
void convert(dt_sae::FrequencyDroop& out, const sae::FrequencyDroop& in);
void convert(dt_sae::VoltWatt& out, const sae::VoltWatt& in);
void convert(dt_sae::ConstantWatt& out, const sae::ConstantWatt& in);
void convert(dt_sae::LimitMaxDischargePower& out, const sae::LimitMaxDischargePower& in);
void convert(dt_sae::ActivePowerSupportCPDRes& out, const sae::ActivePowerSupportCPDRes& in);

void convert(dt_sae::DERControlCPDRes& out, const sae::DERControl& in);

void convert(dt_sae::EnterServiceCLRes& out, const sae::EnterServiceCPDRes& in);
void convert(dt_sae::ReactivePowerSupportCLRes& out, const sae::ReactivePowerSupportCPDRes& in);
void convert(dt_sae::ActivePowerSupportCLRes& out, const sae::ActivePowerSupportCPDRes& in);

// Bits 2, 9, 25 and 27 to 31 are unused by the specification and must be ignored.
constexpr std::uint32_t SAE_MODE_BITMAP_MASK = 0x05FFFDFBu;

constexpr std::uint32_t sae_function_bit(sae::DerBitMapFunctions function) {
    return 1U << static_cast<std::uint32_t>(function);
}

struct SaeFunctionName {
    sae::DerBitMapFunctions function{};
    std::string_view name{};
};

// Every bit with a matching Enable in DERControlCPDRes. Bits 0 and 1 (charge, discharge) are inherent to the
// service and bits 21 and 22 (charge loop target powers) carry no Enable, so neither group is ever enabled by
// the SECC. Names match the gate_enable call sites in ac_der_sae_convert.cpp so both tables can be audited
// side by side.
constexpr std::array<SaeFunctionName, 20> SAE_ENABLEABLE_FUNCTIONS{{
    {sae::DerBitMapFunctions::EnterService, "enter service"},
    {sae::DerBitMapFunctions::ConstantPowerFactorUnderExcitedFunction, "constant power factor under excited"},
    {sae::DerBitMapFunctions::ConstantPowerFactorOverExcitedFunction, "constant power factor over excited"},
    {sae::DerBitMapFunctions::ConstantReactivePowerFunction, "constant var"},
    {sae::DerBitMapFunctions::ConstantActivePowerFunction, "constant watt"},
    {sae::DerBitMapFunctions::FrequencyDroopFunction, "frequency droop"},
    {sae::DerBitMapFunctions::HighFrequencyMayTripFunction, "over frequency may trip curve"},
    {sae::DerBitMapFunctions::HighFrequencyMustTripFunction, "over frequency must trip curve"},
    {sae::DerBitMapFunctions::HighVoltageMayTripFunction, "over voltage may trip curve"},
    {sae::DerBitMapFunctions::HighVoltageMomentaryCessationFunction, "over voltage momentary cessation trip curve"},
    {sae::DerBitMapFunctions::HighVoltageMustTripFunction, "over voltage must trip curve"},
    {sae::DerBitMapFunctions::LowFrequencyMayTripFunction, "under frequency may trip curve"},
    {sae::DerBitMapFunctions::LowFrequencyMustTripFunction, "under frequency must trip curve"},
    {sae::DerBitMapFunctions::LowVoltageMayTripFunction, "under voltage may trip curve"},
    {sae::DerBitMapFunctions::LowVoltageMomentaryCessationFunction, "under voltage momentary cessation trip curve"},
    {sae::DerBitMapFunctions::LowVoltageMustTripFunction, "under voltage must trip curve"},
    {sae::DerBitMapFunctions::LimitMaximumActiveDischargePowerFunction, "limit maximum discharge power"},
    {sae::DerBitMapFunctions::VoltVarFunction, "volt var"},
    {sae::DerBitMapFunctions::VoltWattFunction, "volt watt"},
    {sae::DerBitMapFunctions::WattVarFunction, "watt var"},
}};

constexpr std::uint32_t make_sae_enabled_mode_mask() {
    std::uint32_t mask = 0;
    for (const auto& entry : SAE_ENABLEABLE_FUNCTIONS) {
        mask |= sae_function_bit(entry.function);
    }
    return mask;
}

// The echo comparison runs over these bits only.
constexpr std::uint32_t SAE_ENABLED_MODE_MASK = make_sae_enabled_mode_mask();

static_assert((SAE_ENABLED_MODE_MASK & ~SAE_MODE_BITMAP_MASK) == 0,
              "the enableable bits must be a subset of the bits this document uses");

bool is_function_set(std::uint32_t bitmap, sae::DerBitMapFunctions function);
void gate_enable(bool& enable, bool supported, const char* function_name);
void gate_optional_curve(std::optional<dt_sae::DERCurve>& curve, bool supported, const char* function_name);

void gate_enables_by_supported_modes(dt_sae::DERControlCPDRes& out, std::uint32_t supported_modes);

// The bits the gated response actually enables, and the reference the EV's EnabledModes echo is compared
// against in the charge loop. Both response shapes are read the same way, so identical enables yield
// identical bits.
std::uint32_t derive_enabled_modes(const dt_sae::DERControlCPDRes& res);
std::uint32_t derive_enabled_modes(const dt_sae::DERControlCLRes& res);

// Comma separated function names for the set bits, "none" for an empty bitmap. For log lines only.
std::string sae_function_names(std::uint32_t bitmap);

// DERControlCLRes is the channel for updating parameters already sent in DERControlCPDRes, so the optional
// blocks are emitted only when those parameters actually change. Not purely additive: on the no change path
// the optional blocks and the optional enter service members are cleared from out.
void build_der_control_cl_res(dt_sae::DERControlCLRes& out, const DerSaeSetupConfig& config, bool changed_since_cpd,
                              std::uint32_t ev_supported_modes);

} // namespace iso15118::d20::state
