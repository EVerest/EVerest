// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/ev/d20/der/iec_functions.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/sae_modes.hpp>

#include "der_publish_names.hpp"

namespace {

using iso15118::iec::DERControlName;
using iso15118::sae::DerBitMapFunctions;
using iso15118::sae::sae_function_bit;
using types::iso15118::DerControlSource;
using types::iso15118::DerFlavor;
using types::iso15118::DerIecFunction;
using types::iso15118::DerSaeFunction;
using IecFunctions = std::vector<DerIecFunction>;
using SaeFunctions = std::vector<DerSaeFunction>;

std::uint32_t iec_bit(DERControlName name) {
    return 1U << static_cast<std::uint32_t>(name);
}

TEST(SaeDerFunctions, EmptyBitmapYieldsNoFunctions) {
    EXPECT_TRUE(module::sae_der_functions(0).empty());
}

TEST(SaeDerFunctions, ListsSetBitsInAscendingOrder) {
    const auto bitmap = sae_function_bit(DerBitMapFunctions::VoltVarFunction) |
                        sae_function_bit(DerBitMapFunctions::ChargeFunction) |
                        sae_function_bit(DerBitMapFunctions::DischargeFunction);
    EXPECT_EQ(module::sae_der_functions(bitmap),
              (SaeFunctions{DerSaeFunction::ChargeFunction, DerSaeFunction::DischargeFunction,
                            DerSaeFunction::VoltVarFunction}));
}

TEST(SaeDerFunctions, IgnoresBitsTheSpecificationLeavesUnused) {
    // AMD1 Table M.6 uses neither bit 2, 9, 25 nor 27 to 31.
    const std::uint32_t bitmap = (1U << 2) | (1U << 9) | (1U << 25) | (1U << 27) | (1U << 31);
    EXPECT_TRUE(module::sae_der_functions(bitmap).empty());
}

// Literal list, so a bit mapped to the wrong function fails here.
TEST(SaeDerFunctions, MapsEveryBitToItsFunction) {
    EXPECT_EQ(module::sae_der_functions(iso15118::sae::SAE_MODE_BITMAP_MASK),
              (SaeFunctions{DerSaeFunction::ChargeFunction,
                            DerSaeFunction::DischargeFunction,
                            DerSaeFunction::EnterService,
                            DerSaeFunction::ConstantPowerFactorUnderExcitedFunction,
                            DerSaeFunction::ConstantPowerFactorOverExcitedFunction,
                            DerSaeFunction::ConstantReactivePowerFunction,
                            DerSaeFunction::ConstantActivePowerFunction,
                            DerSaeFunction::FrequencyDroopFunction,
                            DerSaeFunction::HighFrequencyMayTripFunction,
                            DerSaeFunction::HighFrequencyMustTripFunction,
                            DerSaeFunction::HighVoltageMayTripFunction,
                            DerSaeFunction::HighVoltageMomentaryCessationFunction,
                            DerSaeFunction::HighVoltageMustTripFunction,
                            DerSaeFunction::LowFrequencyMayTripFunction,
                            DerSaeFunction::LowFrequencyMustTripFunction,
                            DerSaeFunction::LowVoltageMayTripFunction,
                            DerSaeFunction::LowVoltageMomentaryCessationFunction,
                            DerSaeFunction::LowVoltageMustTripFunction,
                            DerSaeFunction::LimitMaximumActiveDischargePowerFunction,
                            DerSaeFunction::EVSETargetReactivePowerFunction,
                            DerSaeFunction::EVSETargetActivePowerFunction,
                            DerSaeFunction::VoltVarFunction,
                            DerSaeFunction::VoltWattFunction,
                            DerSaeFunction::WattVarFunction}));
}

TEST(IecDerFunctions, EmptyBitmapYieldsNoFunctions) {
    EXPECT_TRUE(module::iec_der_functions(0).empty());
}

// Literal list, so a bit mapped to the wrong function fails here.
TEST(IecDerFunctions, MapsEveryBitToItsFunction) {
    const std::uint32_t every_bit = (1U << iso15118::ev::d20::der::DER_CONTROL_FUNCTION_COUNT) - 1;
    EXPECT_EQ(module::iec_der_functions(every_bit),
              (IecFunctions{DerIecFunction::OverFrequencyWattMode, DerIecFunction::UnderFrequencyWattMode,
                            DerIecFunction::VoltWattMode, DerIecFunction::VoltVarMode, DerIecFunction::WattVarMode,
                            DerIecFunction::WattCosPhiMode, DerIecFunction::DSOQSetpointProvision,
                            DerIecFunction::DSOCosPhiSetpointProvision, DerIecFunction::DCInjectionRestriction,
                            DerIecFunction::ZeroCurrentMode, DerIecFunction::OverVoltageFaultRideThroughMode,
                            DerIecFunction::UnderVoltageFaultRideThroughMode}));
}

TEST(IecDerFunctions, ListsSelectedFunctions) {
    const auto bitmap = iec_bit(DERControlName::UnderVoltageFaultRideThroughMode) |
                        iec_bit(DERControlName::OverFrequencyWattMode) | iec_bit(DERControlName::DSOQSetpointProvision);
    EXPECT_EQ(module::iec_der_functions(bitmap),
              (IecFunctions{DerIecFunction::OverFrequencyWattMode, DerIecFunction::DSOQSetpointProvision,
                            DerIecFunction::UnderVoltageFaultRideThroughMode}));
}

TEST(IecDerFunctions, IgnoresBitsBeyondTheEnum) {
    EXPECT_TRUE(module::iec_der_functions(1U << iso15118::ev::d20::der::DER_CONTROL_FUNCTION_COUNT).empty());
}

TEST(DerFlavorFor, NamesTheAnnexOfEachDerService) {
    using iso15118::message_20::datatypes::ServiceCategory;
    EXPECT_EQ(module::der_flavor_for(ServiceCategory::AC_DER_IEC), DerFlavor::AC_DER_IEC);
    EXPECT_EQ(module::der_flavor_for(ServiceCategory::AC_DER_SAE), DerFlavor::AC_DER_SAE);
}

TEST(DerFlavorFor, IsEmptyForServicesWithoutDer) {
    using iso15118::message_20::datatypes::ServiceCategory;
    for (const auto service : {ServiceCategory::AC, ServiceCategory::AC_BPT, ServiceCategory::DC,
                               ServiceCategory::DC_BPT, ServiceCategory::MCS, ServiceCategory::MCS_BPT}) {
        EXPECT_FALSE(module::der_flavor_for(service).has_value()) << static_cast<int>(service);
    }
}

TEST(DerNegotiatedFunctions, SaeFlavorUsesTheSaeLayout) {
    const auto bitmap =
        sae_function_bit(DerBitMapFunctions::ChargeFunction) | sae_function_bit(DerBitMapFunctions::DischargeFunction);
    const auto negotiated = module::der_negotiated_functions(DerFlavor::AC_DER_SAE, bitmap);
    EXPECT_EQ(negotiated.flavor, DerFlavor::AC_DER_SAE);
    EXPECT_EQ(negotiated.sae_functions,
              (SaeFunctions{DerSaeFunction::ChargeFunction, DerSaeFunction::DischargeFunction}));
    EXPECT_FALSE(negotiated.iec_functions.has_value());
}

TEST(DerNegotiatedFunctions, IecFlavorUsesTheIecLayout) {
    // Bit 0 is ChargeFunction for SAE and OverFrequencyWattMode for IEC.
    const auto negotiated = module::der_negotiated_functions(DerFlavor::AC_DER_IEC, 0b1);
    EXPECT_EQ(negotiated.flavor, DerFlavor::AC_DER_IEC);
    EXPECT_EQ(negotiated.iec_functions, IecFunctions{DerIecFunction::OverFrequencyWattMode});
    EXPECT_FALSE(negotiated.sae_functions.has_value());
}

TEST(SaeDerControlReceived, CarriesTheBlockSummary) {
    const auto enabled = sae_function_bit(DerBitMapFunctions::VoltWattFunction);
    const auto received =
        module::sae_der_control_received(DerControlSource::ChargeLoop, true, enabled, {"a problem", "another"});
    EXPECT_EQ(received.flavor, DerFlavor::AC_DER_SAE);
    EXPECT_EQ(received.source, DerControlSource::ChargeLoop);
    EXPECT_EQ(received.permit_service, true);
    EXPECT_EQ(received.enabled_functions, SaeFunctions{DerSaeFunction::VoltWattFunction});
    EXPECT_EQ(received.problems, (std::vector<std::string>{"a problem", "another"}));
    EXPECT_FALSE(received.dso_q_setpoint.has_value());
    EXPECT_FALSE(received.dso_cos_phi_setpoint.has_value());
}

TEST(SaeDerControlReceived, LeavesProblemsAbsentWhenClean) {
    const auto received = module::sae_der_control_received(DerControlSource::ChargeParameterDiscovery, false, 0, {});
    EXPECT_EQ(received.source, DerControlSource::ChargeParameterDiscovery);
    EXPECT_EQ(received.permit_service, false);
    EXPECT_EQ(received.enabled_functions, SaeFunctions{});
    EXPECT_FALSE(received.problems.has_value());
}

} // namespace
