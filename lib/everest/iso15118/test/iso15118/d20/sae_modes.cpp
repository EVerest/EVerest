// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/sae_modes.hpp>

using namespace iso15118;
using sae::DerBitMapFunctions;
using sae::for_each_sae_function;
using sae::parse_sae_function_name;
using sae::SAE_ENABLEABLE_FUNCTIONS;
using sae::SAE_ENABLED_MODE_MASK;
using sae::sae_function_bit;
using sae::sae_function_name;
using sae::sae_function_names;
using sae::SAE_MODE_BITMAP_MASK;

SCENARIO("SAE function names derive from one switch") {
    GIVEN("The 32 bit positions") {
        THEN("Exactly the used bits have a name, so every enumerator has one") {
            std::uint32_t named = 0;
            for_each_sae_function([&named](DerBitMapFunctions function) { named |= sae_function_bit(function); });
            CHECK(named == SAE_MODE_BITMAP_MASK);
        }

        THEN("The unused bits 2, 9 and 25 have none") {
            CHECK(sae_function_name(static_cast<DerBitMapFunctions>(2)).empty());
            CHECK(sae_function_name(static_cast<DerBitMapFunctions>(9)).empty());
            CHECK(sae_function_name(static_cast<DerBitMapFunctions>(25)).empty());
        }
    }

    GIVEN("Every enumerator") {
        THEN("Parsing its name gives it back") {
            for_each_sae_function([](DerBitMapFunctions function) {
                CAPTURE(sae_function_name(function));
                CHECK(parse_sae_function_name(sae_function_name(function)) == function);
            });
        }
    }

    GIVEN("A bitmap") {
        THEN("The set bits are named in bit order and unused bits are skipped") {
            CHECK(sae_function_names(0) == "none");
            CHECK(sae_function_names(1U << 2) == "none");
            CHECK(sae_function_names(sae_function_bit(DerBitMapFunctions::WattVarFunction) |
                                     sae_function_bit(DerBitMapFunctions::ChargeFunction) |
                                     sae_function_bit(DerBitMapFunctions::EnterService) | (1U << 9)) ==
                  "ChargeFunction, EnterService, WattVarFunction");
        }
    }

    GIVEN("SAE_ENABLEABLE_FUNCTIONS") {
        THEN("It lists exactly the bits of SAE_ENABLED_MODE_MASK") {
            std::uint32_t mask = 0;
            for (const auto function : SAE_ENABLEABLE_FUNCTIONS) {
                mask |= sae_function_bit(function);
            }
            CHECK(SAE_ENABLEABLE_FUNCTIONS.size() == 20);
            CHECK(mask == SAE_ENABLED_MODE_MASK);
        }
    }
}

SCENARIO("SAE function names parse to bitmap functions") {
    GIVEN("Every enumerator name") {
        THEN("Each name round-trips to its enumerator") {
            CHECK(parse_sae_function_name("ChargeFunction") == DerBitMapFunctions::ChargeFunction);
            CHECK(parse_sae_function_name("DischargeFunction") == DerBitMapFunctions::DischargeFunction);
            CHECK(parse_sae_function_name("EnterService") == DerBitMapFunctions::EnterService);
            CHECK(parse_sae_function_name("ConstantPowerFactorUnderExcitedFunction") ==
                  DerBitMapFunctions::ConstantPowerFactorUnderExcitedFunction);
            CHECK(parse_sae_function_name("ConstantPowerFactorOverExcitedFunction") ==
                  DerBitMapFunctions::ConstantPowerFactorOverExcitedFunction);
            CHECK(parse_sae_function_name("ConstantReactivePowerFunction") ==
                  DerBitMapFunctions::ConstantReactivePowerFunction);
            CHECK(parse_sae_function_name("ConstantActivePowerFunction") ==
                  DerBitMapFunctions::ConstantActivePowerFunction);
            CHECK(parse_sae_function_name("FrequencyDroopFunction") == DerBitMapFunctions::FrequencyDroopFunction);
            CHECK(parse_sae_function_name("HighFrequencyMayTripFunction") ==
                  DerBitMapFunctions::HighFrequencyMayTripFunction);
            CHECK(parse_sae_function_name("HighFrequencyMustTripFunction") ==
                  DerBitMapFunctions::HighFrequencyMustTripFunction);
            CHECK(parse_sae_function_name("HighVoltageMayTripFunction") ==
                  DerBitMapFunctions::HighVoltageMayTripFunction);
            CHECK(parse_sae_function_name("HighVoltageMomentaryCessationFunction") ==
                  DerBitMapFunctions::HighVoltageMomentaryCessationFunction);
            CHECK(parse_sae_function_name("HighVoltageMustTripFunction") ==
                  DerBitMapFunctions::HighVoltageMustTripFunction);
            CHECK(parse_sae_function_name("LowFrequencyMayTripFunction") ==
                  DerBitMapFunctions::LowFrequencyMayTripFunction);
            CHECK(parse_sae_function_name("LowFrequencyMustTripFunction") ==
                  DerBitMapFunctions::LowFrequencyMustTripFunction);
            CHECK(parse_sae_function_name("LowVoltageMayTripFunction") ==
                  DerBitMapFunctions::LowVoltageMayTripFunction);
            CHECK(parse_sae_function_name("LowVoltageMomentaryCessationFunction") ==
                  DerBitMapFunctions::LowVoltageMomentaryCessationFunction);
            CHECK(parse_sae_function_name("LowVoltageMustTripFunction") ==
                  DerBitMapFunctions::LowVoltageMustTripFunction);
            CHECK(parse_sae_function_name("LimitMaximumActiveDischargePowerFunction") ==
                  DerBitMapFunctions::LimitMaximumActiveDischargePowerFunction);
            CHECK(parse_sae_function_name("EVSETargetReactivePowerFunction") ==
                  DerBitMapFunctions::EVSETargetReactivePowerFunction);
            CHECK(parse_sae_function_name("EVSETargetActivePowerFunction") ==
                  DerBitMapFunctions::EVSETargetActivePowerFunction);
            CHECK(parse_sae_function_name("VoltVarFunction") == DerBitMapFunctions::VoltVarFunction);
            CHECK(parse_sae_function_name("VoltWattFunction") == DerBitMapFunctions::VoltWattFunction);
            CHECK(parse_sae_function_name("WattVarFunction") == DerBitMapFunctions::WattVarFunction);
        }
    }

    GIVEN("An unknown name") {
        THEN("Parsing yields nullopt") {
            CHECK(parse_sae_function_name("FrequencyWattFunction") == std::nullopt);
            CHECK(parse_sae_function_name("") == std::nullopt);
        }

        THEN("The lookup is case sensitive and does not trim") {
            CHECK(parse_sae_function_name("chargeFunction") == std::nullopt);
            CHECK(parse_sae_function_name("ChargeFunction ") == std::nullopt);
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
    }

    GIVEN("SAE_ENABLED_MODE_MASK") {
        THEN("The bits without an Enable in DERControlCPDRes are clear") {
            constexpr std::uint32_t not_enableable_bits = (1U << 0) | (1U << 1) | (1U << 21) | (1U << 22);
            CHECK((SAE_ENABLED_MODE_MASK & not_enableable_bits) == 0);
        }

        THEN("Every other used bit is set") {
            constexpr std::uint32_t not_enableable_bits = (1U << 0) | (1U << 1) | (1U << 21) | (1U << 22);
            CHECK(SAE_ENABLED_MODE_MASK == (SAE_MODE_BITMAP_MASK & ~not_enableable_bits));
        }
    }
}
