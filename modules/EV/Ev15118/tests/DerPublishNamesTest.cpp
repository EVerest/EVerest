// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/enum_names.hpp>
#include <iso15118/ev/der_control_functions.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/sae_modes.hpp>

#include "der_publish_names.hpp"

namespace {

using iso15118::iec::DERControlName;
using iso15118::sae::DerBitMapFunctions;
using iso15118::sae::sae_function_bit;
using types::iso15118::DerControlSource;
using types::iso15118::DerFlavor;
using Names = std::vector<std::string>;

std::uint32_t iec_bit(DERControlName name) {
    return 1U << static_cast<std::uint32_t>(name);
}

TEST(SaeDerFunctionNames, EmptyBitmapYieldsNoNames) {
    EXPECT_TRUE(module::sae_der_function_names(0).empty());
}

TEST(SaeDerFunctionNames, NamesSetBitsInAscendingOrder) {
    const auto bitmap = sae_function_bit(DerBitMapFunctions::VoltVarFunction) |
                        sae_function_bit(DerBitMapFunctions::ChargeFunction) |
                        sae_function_bit(DerBitMapFunctions::DischargeFunction);
    EXPECT_EQ(module::sae_der_function_names(bitmap),
              (Names{"ChargeFunction", "DischargeFunction", "VoltVarFunction"}));
}

TEST(SaeDerFunctionNames, IgnoresBitsTheSpecificationLeavesUnused) {
    // AMD1 Table M.6 uses neither bit 2, 9, 25 nor 27 to 31.
    const std::uint32_t bitmap = (1U << 2) | (1U << 9) | (1U << 25) | (1U << 27) | (1U << 31);
    EXPECT_TRUE(module::sae_der_function_names(bitmap).empty());
}

TEST(SaeDerFunctionNames, EveryNameParsesBackToItsBit) {
    const auto names = module::sae_der_function_names(iso15118::sae::SAE_MODE_BITMAP_MASK);
    std::size_t enumerators = 0;
    iso15118::sae::for_each_sae_function([&enumerators](DerBitMapFunctions) { ++enumerators; });
    EXPECT_EQ(names.size(), enumerators);
    std::uint32_t rebuilt = 0;
    for (const auto& name : names) {
        const auto function = iso15118::sae::parse_sae_function_name(name);
        ASSERT_TRUE(function.has_value()) << name;
        rebuilt |= sae_function_bit(*function);
    }
    EXPECT_EQ(rebuilt, iso15118::sae::SAE_MODE_BITMAP_MASK);
}

TEST(IecDerFunctionNames, EmptyBitmapYieldsNoNames) {
    EXPECT_TRUE(module::iec_der_function_names(0).empty());
}

TEST(IecDerFunctionNames, EverySingleBitNamesItsEnumerator) {
    iso15118::for_each_enum_value<DERControlName, iso15118::ev::DER_CONTROL_FUNCTION_COUNT>(
        iso15118::iec::der_control_name, [](DERControlName function) {
            const auto name = std::string{iso15118::iec::der_control_name(function)};
            EXPECT_EQ(module::iec_der_function_names(iec_bit(function)), Names{name}) << name;
        });
}

// Literal spellings, so a rename in the shared switch fails here instead of round-tripping.
TEST(IecDerFunctionNames, NamesSelectedFunctions) {
    const auto bitmap = iec_bit(DERControlName::UnderVoltageFaultRideThroughMode) |
                        iec_bit(DERControlName::OverFrequencyWattMode) | iec_bit(DERControlName::DSOQSetpointProvision);
    EXPECT_EQ(module::iec_der_function_names(bitmap),
              (Names{"OverFrequencyWattMode", "DSOQSetpointProvision", "UnderVoltageFaultRideThroughMode"}));
}

TEST(IecDerFunctionNames, IgnoresBitsBeyondTheEnum) {
    EXPECT_TRUE(module::iec_der_function_names(1U << iso15118::ev::DER_CONTROL_FUNCTION_COUNT).empty());
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
    EXPECT_EQ(negotiated.functions, (Names{"ChargeFunction", "DischargeFunction"}));
}

TEST(DerNegotiatedFunctions, IecFlavorUsesTheIecLayout) {
    // Bit 0 is ChargeFunction for SAE and OverFrequencyWattMode for IEC.
    const auto negotiated = module::der_negotiated_functions(DerFlavor::AC_DER_IEC, 0b1);
    EXPECT_EQ(negotiated.flavor, DerFlavor::AC_DER_IEC);
    EXPECT_EQ(negotiated.functions, Names{"OverFrequencyWattMode"});
}

TEST(SaeDerControlReceived, CarriesTheBlockSummary) {
    const auto enabled = sae_function_bit(DerBitMapFunctions::VoltWattFunction);
    const auto received =
        module::sae_der_control_received(DerControlSource::ChargeLoop, true, enabled, {"a problem", "another"});
    EXPECT_EQ(received.flavor, DerFlavor::AC_DER_SAE);
    EXPECT_EQ(received.source, DerControlSource::ChargeLoop);
    EXPECT_EQ(received.permit_service, true);
    EXPECT_EQ(received.enabled_functions, Names{"VoltWattFunction"});
    EXPECT_EQ(received.problems, (Names{"a problem", "another"}));
    EXPECT_FALSE(received.dso_q_setpoint.has_value());
    EXPECT_FALSE(received.dso_cos_phi_setpoint.has_value());
}

TEST(SaeDerControlReceived, LeavesProblemsAbsentWhenClean) {
    const auto received = module::sae_der_control_received(DerControlSource::ChargeParameterDiscovery, false, 0, {});
    EXPECT_EQ(received.source, DerControlSource::ChargeParameterDiscovery);
    EXPECT_EQ(received.permit_service, false);
    EXPECT_EQ(received.enabled_functions, Names{});
    EXPECT_FALSE(received.problems.has_value());
}

} // namespace
