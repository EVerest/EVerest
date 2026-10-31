// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "der_publish_names.hpp"

#include <cstddef>
#include <stdexcept>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/ev/d20/der/iec_functions.hpp>
#include <iso15118/sae_modes.hpp>

namespace module {

namespace {

using iso15118::iec::DERControlName;
using iso15118::sae::DerBitMapFunctions;
using types::iso15118::DerIecFunction;
using types::iso15118::DerSaeFunction;

// sae_der_functions walks the mask, so every bit in it must be an enumerator.
constexpr std::uint32_t sae_enumerator_bits() {
    std::uint32_t bits = 0;
    for (const auto& entry : iso15118::sae::SAE_FUNCTION_NAMES) {
        bits |= iso15118::sae::sae_function_bit(entry.function);
    }
    return bits;
}
static_assert(sae_enumerator_bits() == iso15118::sae::SAE_MODE_BITMAP_MASK,
              "SAE_MODE_BITMAP_MASK must hold exactly the DerBitMapFunctions bits");

DerIecFunction to_der_iec_function(DERControlName function) {
    switch (function) {
    case DERControlName::OverFrequencyWattMode:
        return DerIecFunction::OverFrequencyWattMode;
    case DERControlName::UnderFrequencyWattMode:
        return DerIecFunction::UnderFrequencyWattMode;
    case DERControlName::VoltWattMode:
        return DerIecFunction::VoltWattMode;
    case DERControlName::VoltVarMode:
        return DerIecFunction::VoltVarMode;
    case DERControlName::WattVarMode:
        return DerIecFunction::WattVarMode;
    case DERControlName::WattCosPhiMode:
        return DerIecFunction::WattCosPhiMode;
    case DERControlName::DSOQSetpointProvision:
        return DerIecFunction::DSOQSetpointProvision;
    case DERControlName::DSOCosPhiSetpointProvision:
        return DerIecFunction::DSOCosPhiSetpointProvision;
    case DERControlName::DCInjectionRestriction:
        return DerIecFunction::DCInjectionRestriction;
    case DERControlName::ZeroCurrentMode:
        return DerIecFunction::ZeroCurrentMode;
    case DERControlName::OverVoltageFaultRideThroughMode:
        return DerIecFunction::OverVoltageFaultRideThroughMode;
    case DERControlName::UnderVoltageFaultRideThroughMode:
        return DerIecFunction::UnderVoltageFaultRideThroughMode;
    }
    throw std::out_of_range("DERControlName has no DerIecFunction");
}

DerSaeFunction to_der_sae_function(DerBitMapFunctions function) {
    switch (function) {
    case DerBitMapFunctions::ChargeFunction:
        return DerSaeFunction::ChargeFunction;
    case DerBitMapFunctions::DischargeFunction:
        return DerSaeFunction::DischargeFunction;
    case DerBitMapFunctions::EnterService:
        return DerSaeFunction::EnterService;
    case DerBitMapFunctions::ConstantPowerFactorUnderExcitedFunction:
        return DerSaeFunction::ConstantPowerFactorUnderExcitedFunction;
    case DerBitMapFunctions::ConstantPowerFactorOverExcitedFunction:
        return DerSaeFunction::ConstantPowerFactorOverExcitedFunction;
    case DerBitMapFunctions::ConstantReactivePowerFunction:
        return DerSaeFunction::ConstantReactivePowerFunction;
    case DerBitMapFunctions::ConstantActivePowerFunction:
        return DerSaeFunction::ConstantActivePowerFunction;
    case DerBitMapFunctions::FrequencyDroopFunction:
        return DerSaeFunction::FrequencyDroopFunction;
    case DerBitMapFunctions::HighFrequencyMayTripFunction:
        return DerSaeFunction::HighFrequencyMayTripFunction;
    case DerBitMapFunctions::HighFrequencyMustTripFunction:
        return DerSaeFunction::HighFrequencyMustTripFunction;
    case DerBitMapFunctions::HighVoltageMayTripFunction:
        return DerSaeFunction::HighVoltageMayTripFunction;
    case DerBitMapFunctions::HighVoltageMomentaryCessationFunction:
        return DerSaeFunction::HighVoltageMomentaryCessationFunction;
    case DerBitMapFunctions::HighVoltageMustTripFunction:
        return DerSaeFunction::HighVoltageMustTripFunction;
    case DerBitMapFunctions::LowFrequencyMayTripFunction:
        return DerSaeFunction::LowFrequencyMayTripFunction;
    case DerBitMapFunctions::LowFrequencyMustTripFunction:
        return DerSaeFunction::LowFrequencyMustTripFunction;
    case DerBitMapFunctions::LowVoltageMayTripFunction:
        return DerSaeFunction::LowVoltageMayTripFunction;
    case DerBitMapFunctions::LowVoltageMomentaryCessationFunction:
        return DerSaeFunction::LowVoltageMomentaryCessationFunction;
    case DerBitMapFunctions::LowVoltageMustTripFunction:
        return DerSaeFunction::LowVoltageMustTripFunction;
    case DerBitMapFunctions::LimitMaximumActiveDischargePowerFunction:
        return DerSaeFunction::LimitMaximumActiveDischargePowerFunction;
    case DerBitMapFunctions::EVSETargetReactivePowerFunction:
        return DerSaeFunction::EVSETargetReactivePowerFunction;
    case DerBitMapFunctions::EVSETargetActivePowerFunction:
        return DerSaeFunction::EVSETargetActivePowerFunction;
    case DerBitMapFunctions::VoltVarFunction:
        return DerSaeFunction::VoltVarFunction;
    case DerBitMapFunctions::VoltWattFunction:
        return DerSaeFunction::VoltWattFunction;
    case DerBitMapFunctions::WattVarFunction:
        return DerSaeFunction::WattVarFunction;
    }
    throw std::out_of_range("DerBitMapFunctions has no DerSaeFunction");
}

} // namespace

std::vector<DerSaeFunction> sae_der_functions(std::uint32_t bitmap) {
    std::vector<DerSaeFunction> functions;
    for (std::uint32_t bit = 0; bit < 32; ++bit) {
        if ((bitmap & iso15118::sae::SAE_MODE_BITMAP_MASK & (1U << bit)) != 0) {
            functions.push_back(to_der_sae_function(static_cast<DerBitMapFunctions>(bit)));
        }
    }
    return functions;
}

std::vector<DerIecFunction> iec_der_functions(std::uint32_t bitmap) {
    std::vector<DerIecFunction> functions;
    for (std::size_t bit = 0; bit < iso15118::ev::d20::der::DER_CONTROL_FUNCTION_COUNT; ++bit) {
        if ((bitmap & (1U << bit)) != 0) {
            functions.push_back(to_der_iec_function(static_cast<DERControlName>(bit)));
        }
    }
    return functions;
}

std::optional<types::iso15118::DerFlavor> der_flavor_for(iso15118::message_20::datatypes::ServiceCategory service) {
    using iso15118::message_20::datatypes::ServiceCategory;
    if (service == ServiceCategory::AC_DER_IEC) {
        return types::iso15118::DerFlavor::AC_DER_IEC;
    }
    if (service == ServiceCategory::AC_DER_SAE) {
        return types::iso15118::DerFlavor::AC_DER_SAE;
    }
    return std::nullopt;
}

types::iso15118::DerNegotiatedFunctions der_negotiated_functions(types::iso15118::DerFlavor flavor,
                                                                 std::uint32_t bitmap) {
    types::iso15118::DerNegotiatedFunctions negotiated;
    negotiated.flavor = flavor;
    if (flavor == types::iso15118::DerFlavor::AC_DER_SAE) {
        negotiated.sae_functions = sae_der_functions(bitmap);
    } else {
        negotiated.iec_functions = iec_der_functions(bitmap);
    }
    return negotiated;
}

types::iso15118::DerControlReceived
sae_der_control_received(types::iso15118::DerControlSource source, bool permit_service, std::uint32_t enabled,
                         const iso15118::ev::d20::der::DerControlProblems& problems) {
    types::iso15118::DerControlReceived received;
    received.flavor = types::iso15118::DerFlavor::AC_DER_SAE;
    received.source = source;
    received.permit_service = permit_service;
    received.enabled_functions = sae_der_functions(enabled);
    if (not problems.empty()) {
        received.problems = problems;
    }
    return received;
}

} // namespace module
