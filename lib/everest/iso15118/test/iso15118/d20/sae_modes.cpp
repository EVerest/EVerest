// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/sae_modes.hpp>

using namespace iso15118;
using sae::DerBitMapFunctions;
using sae::parse_sae_function_name;
using sae::SAE_ENABLED_MODE_MASK;
using sae::sae_function_bit;
using sae::SAE_MODE_BITMAP_MASK;

SCENARIO("SAE function names parse to bitmap functions") {
    GIVEN("Every enumerator name") {
        THEN("Each name round-trips to its enumerator") {
            CHECK(parse_sae_function_name("charge") == DerBitMapFunctions::ChargeFunction);
            CHECK(parse_sae_function_name("discharge") == DerBitMapFunctions::DischargeFunction);
            CHECK(parse_sae_function_name("enter service") == DerBitMapFunctions::EnterService);
            CHECK(parse_sae_function_name("constant power factor under excited") ==
                  DerBitMapFunctions::ConstantPowerFactorUnderExcitedFunction);
            CHECK(parse_sae_function_name("constant power factor over excited") ==
                  DerBitMapFunctions::ConstantPowerFactorOverExcitedFunction);
            CHECK(parse_sae_function_name("constant var") == DerBitMapFunctions::ConstantReactivePowerFunction);
            CHECK(parse_sae_function_name("constant watt") == DerBitMapFunctions::ConstantActivePowerFunction);
            CHECK(parse_sae_function_name("frequency droop") == DerBitMapFunctions::FrequencyDroopFunction);
            CHECK(parse_sae_function_name("over frequency may trip curve") ==
                  DerBitMapFunctions::HighFrequencyMayTripFunction);
            CHECK(parse_sae_function_name("over frequency must trip curve") ==
                  DerBitMapFunctions::HighFrequencyMustTripFunction);
            CHECK(parse_sae_function_name("over voltage may trip curve") ==
                  DerBitMapFunctions::HighVoltageMayTripFunction);
            CHECK(parse_sae_function_name("over voltage momentary cessation trip curve") ==
                  DerBitMapFunctions::HighVoltageMomentaryCessationFunction);
            CHECK(parse_sae_function_name("over voltage must trip curve") ==
                  DerBitMapFunctions::HighVoltageMustTripFunction);
            CHECK(parse_sae_function_name("under frequency may trip curve") ==
                  DerBitMapFunctions::LowFrequencyMayTripFunction);
            CHECK(parse_sae_function_name("under frequency must trip curve") ==
                  DerBitMapFunctions::LowFrequencyMustTripFunction);
            CHECK(parse_sae_function_name("under voltage may trip curve") ==
                  DerBitMapFunctions::LowVoltageMayTripFunction);
            CHECK(parse_sae_function_name("under voltage momentary cessation trip curve") ==
                  DerBitMapFunctions::LowVoltageMomentaryCessationFunction);
            CHECK(parse_sae_function_name("under voltage must trip curve") ==
                  DerBitMapFunctions::LowVoltageMustTripFunction);
            CHECK(parse_sae_function_name("limit maximum discharge power") ==
                  DerBitMapFunctions::LimitMaximumActiveDischargePowerFunction);
            CHECK(parse_sae_function_name("evse target reactive power") ==
                  DerBitMapFunctions::EVSETargetReactivePowerFunction);
            CHECK(parse_sae_function_name("evse target active power") ==
                  DerBitMapFunctions::EVSETargetActivePowerFunction);
            CHECK(parse_sae_function_name("volt var") == DerBitMapFunctions::VoltVarFunction);
            CHECK(parse_sae_function_name("volt watt") == DerBitMapFunctions::VoltWattFunction);
            CHECK(parse_sae_function_name("watt var") == DerBitMapFunctions::WattVarFunction);
        }
    }

    GIVEN("An unknown name") {
        THEN("Parsing yields nullopt") {
            CHECK(parse_sae_function_name("frequency watt") == std::nullopt);
            CHECK(parse_sae_function_name("") == std::nullopt);
        }
    }
}

SCENARIO("SAE bitmap masks match the specified bit layout") {
    GIVEN("SAE_MODE_BITMAP_MASK") {
        THEN("The unused bits 2, 9, 25 and 27 to 31 are clear") {
            constexpr std::uint32_t unused_bits =
                (1U << 2) | (1U << 9) | (1U << 25) | (1U << 27) | (1U << 28) | (1U << 29) | (1U << 30) | (1U << 31);
            CHECK((SAE_MODE_BITMAP_MASK & unused_bits) == 0);
        }

        THEN("Every enumerator bit is set") {
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::ChargeFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::DischargeFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::EnterService)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK &
                   sae_function_bit(DerBitMapFunctions::ConstantPowerFactorUnderExcitedFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK &
                   sae_function_bit(DerBitMapFunctions::ConstantPowerFactorOverExcitedFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::ConstantReactivePowerFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::ConstantActivePowerFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::FrequencyDroopFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::HighFrequencyMayTripFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::HighFrequencyMustTripFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::HighVoltageMayTripFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK &
                   sae_function_bit(DerBitMapFunctions::HighVoltageMomentaryCessationFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::HighVoltageMustTripFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::LowFrequencyMayTripFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::LowFrequencyMustTripFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::LowVoltageMayTripFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::LowVoltageMomentaryCessationFunction)) !=
                  0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::LowVoltageMustTripFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK &
                   sae_function_bit(DerBitMapFunctions::LimitMaximumActiveDischargePowerFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::EVSETargetReactivePowerFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::EVSETargetActivePowerFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::VoltVarFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::VoltWattFunction)) != 0);
            CHECK((SAE_MODE_BITMAP_MASK & sae_function_bit(DerBitMapFunctions::WattVarFunction)) != 0);
        }
    }

    GIVEN("SAE_ENABLED_MODE_MASK") {
        THEN("The bits without an Enable in DERControlCPDRes are clear") {
            constexpr std::uint32_t not_enableable_bits = (1U << 0) | (1U << 1) | (1U << 21) | (1U << 22);
            CHECK((SAE_ENABLED_MODE_MASK & not_enableable_bits) == 0);
        }
    }
}
