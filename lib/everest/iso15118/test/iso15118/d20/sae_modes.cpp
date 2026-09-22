// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

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

namespace {

namespace dt_sae = message_20::datatypes::sae;

using Fn = sae::DerBitMapFunctions;
using sae::derive_enabled_modes;
using Cpd = dt_sae::DERControlCPDRes;
using Cl = dt_sae::DERControlCLRes;
using Excitation = dt_sae::PowerFactorExcitation;

template <typename Control> struct EnableCase {
    void (*enable)(Control&);
    Fn function;
};

dt_sae::DERCurve enabled_curve() {
    dt_sae::DERCurve curve;
    curve.enable = true;
    return curve;
}

template <typename Leaf> Leaf enabled() {
    Leaf leaf;
    leaf.enable = true;
    return leaf;
}

dt_sae::ConstantPowerFactor power_factor(Excitation excitation) {
    auto leaf = enabled<dt_sae::ConstantPowerFactor>();
    leaf.power_factor_excitation = excitation;
    return leaf;
}

const std::vector<EnableCase<Cpd>> CPD_CASES{
    {[](Cpd& c) { c.voltage_trip.over_voltage_must_trip_curve.enable = true; }, Fn::HighVoltageMustTripFunction},
    {[](Cpd& c) { c.voltage_trip.under_voltage_must_trip_curve.enable = true; }, Fn::LowVoltageMustTripFunction},
    {[](Cpd& c) { c.voltage_trip.over_voltage_momentary_cessation_trip_curve = enabled_curve(); },
     Fn::HighVoltageMomentaryCessationFunction},
    {[](Cpd& c) { c.voltage_trip.under_voltage_momentary_cessation_trip_curve = enabled_curve(); },
     Fn::LowVoltageMomentaryCessationFunction},
    {[](Cpd& c) { c.voltage_trip.over_voltage_may_trip_curve = enabled_curve(); }, Fn::HighVoltageMayTripFunction},
    {[](Cpd& c) { c.voltage_trip.under_voltage_may_trip_curve = enabled_curve(); }, Fn::LowVoltageMayTripFunction},
    {[](Cpd& c) { c.frequency_trip.over_frequency_must_trip_curve.enable = true; }, Fn::HighFrequencyMustTripFunction},
    {[](Cpd& c) { c.frequency_trip.under_frequency_must_trip_curve.enable = true; }, Fn::LowFrequencyMustTripFunction},
    {[](Cpd& c) { c.frequency_trip.over_frequency_may_trip_curve = enabled_curve(); },
     Fn::HighFrequencyMayTripFunction},
    {[](Cpd& c) { c.frequency_trip.under_frequency_may_trip_curve = enabled_curve(); },
     Fn::LowFrequencyMayTripFunction},
    {[](Cpd& c) { c.enter_service_cpd_res.permit_service = true; }, Fn::EnterService},
    {[](Cpd& c) { c.reactive_power_support_cpd_res.constant_power_factor = power_factor(Excitation::OverExcited); },
     Fn::ConstantPowerFactorOverExcitedFunction},
    {[](Cpd& c) { c.reactive_power_support_cpd_res.constant_power_factor = power_factor(Excitation::UnderExcited); },
     Fn::ConstantPowerFactorUnderExcitedFunction},
    {[](Cpd& c) { c.reactive_power_support_cpd_res.volt_var.enable = true; }, Fn::VoltVarFunction},
    {[](Cpd& c) { c.reactive_power_support_cpd_res.watt_var.enable = true; }, Fn::WattVarFunction},
    {[](Cpd& c) { c.reactive_power_support_cpd_res.constant_var.enable = true; }, Fn::ConstantReactivePowerFunction},
    {[](Cpd& c) { c.active_power_support_cpd_res.frequency_droop.enable = true; }, Fn::FrequencyDroopFunction},
    {[](Cpd& c) { c.active_power_support_cpd_res.volt_watt.enable = true; }, Fn::VoltWattFunction},
    {[](Cpd& c) { c.active_power_support_cpd_res.constant_watt.enable = true; }, Fn::ConstantActivePowerFunction},
    {[](Cpd& c) { c.active_power_support_cpd_res.limit_max_discharge_power.enable = true; },
     Fn::LimitMaximumActiveDischargePowerFunction},
};

const std::vector<EnableCase<Cl>> CL_CASES{
    {[](Cl& c) { c.voltage_trip.emplace().over_voltage_must_trip_curve.enable = true; },
     Fn::HighVoltageMustTripFunction},
    {[](Cl& c) { c.voltage_trip.emplace().under_voltage_must_trip_curve.enable = true; },
     Fn::LowVoltageMustTripFunction},
    {[](Cl& c) { c.voltage_trip.emplace().over_voltage_momentary_cessation_trip_curve = enabled_curve(); },
     Fn::HighVoltageMomentaryCessationFunction},
    {[](Cl& c) { c.voltage_trip.emplace().under_voltage_momentary_cessation_trip_curve = enabled_curve(); },
     Fn::LowVoltageMomentaryCessationFunction},
    {[](Cl& c) { c.voltage_trip.emplace().over_voltage_may_trip_curve = enabled_curve(); },
     Fn::HighVoltageMayTripFunction},
    {[](Cl& c) { c.voltage_trip.emplace().under_voltage_may_trip_curve = enabled_curve(); },
     Fn::LowVoltageMayTripFunction},
    {[](Cl& c) { c.frequency_trip.emplace().over_frequency_must_trip_curve.enable = true; },
     Fn::HighFrequencyMustTripFunction},
    {[](Cl& c) { c.frequency_trip.emplace().under_frequency_must_trip_curve.enable = true; },
     Fn::LowFrequencyMustTripFunction},
    {[](Cl& c) { c.frequency_trip.emplace().over_frequency_may_trip_curve = enabled_curve(); },
     Fn::HighFrequencyMayTripFunction},
    {[](Cl& c) { c.frequency_trip.emplace().under_frequency_may_trip_curve = enabled_curve(); },
     Fn::LowFrequencyMayTripFunction},
    {[](Cl& c) { c.enter_service_cl_res.permit_service = true; }, Fn::EnterService},
    {[](Cl& c) {
         c.reactive_power_support_cl_res.emplace().constant_power_factor = power_factor(Excitation::OverExcited);
     },
     Fn::ConstantPowerFactorOverExcitedFunction},
    {[](Cl& c) {
         c.reactive_power_support_cl_res.emplace().constant_power_factor = power_factor(Excitation::UnderExcited);
     },
     Fn::ConstantPowerFactorUnderExcitedFunction},
    {[](Cl& c) { c.reactive_power_support_cl_res.emplace().volt_var = enabled<dt_sae::VoltVar>(); },
     Fn::VoltVarFunction},
    {[](Cl& c) { c.reactive_power_support_cl_res.emplace().watt_var = enabled<dt_sae::WattVar>(); },
     Fn::WattVarFunction},
    {[](Cl& c) { c.reactive_power_support_cl_res.emplace().constant_var = enabled<dt_sae::ConstantVar>(); },
     Fn::ConstantReactivePowerFunction},
    {[](Cl& c) { c.active_power_support_cl_res.emplace().frequency_droop = enabled<dt_sae::FrequencyDroop>(); },
     Fn::FrequencyDroopFunction},
    {[](Cl& c) { c.active_power_support_cl_res.emplace().volt_watt = enabled<dt_sae::VoltWatt>(); },
     Fn::VoltWattFunction},
    {[](Cl& c) { c.active_power_support_cl_res.emplace().constant_watt = enabled<dt_sae::ConstantWatt>(); },
     Fn::ConstantActivePowerFunction},
    {[](Cl& c) {
         c.active_power_support_cl_res.emplace().limit_max_discharge_power = enabled<dt_sae::LimitMaxDischargePower>();
     },
     Fn::LimitMaximumActiveDischargePowerFunction},
};

template <typename Control> void check_each_enable_maps_to_its_bit(const std::vector<EnableCase<Control>>& cases) {
    REQUIRE(derive_enabled_modes(Control{}) == 0);
    std::uint32_t all = 0;
    for (const auto& entry : cases) {
        Control control{};
        entry.enable(control);
        CAPTURE(sae::sae_function_names(sae_function_bit(entry.function)));
        CHECK(derive_enabled_modes(control) == sae_function_bit(entry.function));
        all |= sae_function_bit(entry.function);
    }
    // The cases cover every enableable bit.
    CHECK(all == sae::SAE_ENABLED_MODE_MASK);
}

constexpr auto OVER_EXCITED = sae_function_bit(Fn::ConstantPowerFactorOverExcitedFunction);
constexpr auto UNDER_EXCITED = sae_function_bit(Fn::ConstantPowerFactorUnderExcitedFunction);

dt_sae::ConstantPowerFactor mixed_power_factor(bool enable) {
    auto leaf = power_factor(Excitation::OverExcited);
    leaf.enable = enable;
    leaf.power_factor_excitation_L3 = Excitation::UnderExcited;
    return leaf;
}

} // namespace

SCENARIO("SAE derive_enabled_modes maps each Enable to its bit") {
    GIVEN("DERControlCPDRes") {
        check_each_enable_maps_to_its_bit(CPD_CASES);
    }
    GIVEN("DERControlCLRes") {
        check_each_enable_maps_to_its_bit(CL_CASES);
    }
}

SCENARIO("SAE derive_enabled_modes sets a bit for each excitation direction on any line") {
    GIVEN("L1 OverExcited and L3 UnderExcited, enabled") {
        Cpd cpd{};
        cpd.reactive_power_support_cpd_res.constant_power_factor = mixed_power_factor(true);
        Cl cl{};
        cl.reactive_power_support_cl_res.emplace().constant_power_factor = mixed_power_factor(true);

        THEN("Both bits are set") {
            CHECK(derive_enabled_modes(cpd) == (OVER_EXCITED | UNDER_EXCITED));
            CHECK(derive_enabled_modes(cl) == (OVER_EXCITED | UNDER_EXCITED));
        }
    }

    GIVEN("L1 UnderExcited and L2 OverExcited, enabled") {
        Cpd cpd{};
        auto leaf = power_factor(Excitation::UnderExcited);
        leaf.power_factor_excitation_L2 = Excitation::OverExcited;
        cpd.reactive_power_support_cpd_res.constant_power_factor = leaf;

        THEN("Both bits are set") {
            CHECK(derive_enabled_modes(cpd) == (OVER_EXCITED | UNDER_EXCITED));
        }
    }

    GIVEN("Mixed excitation, disabled") {
        Cpd cpd{};
        cpd.reactive_power_support_cpd_res.constant_power_factor = mixed_power_factor(false);

        THEN("No bit is set") {
            CHECK(derive_enabled_modes(cpd) == 0);
        }
    }
}

SCENARIO("SAE sae_function_names lists the set bits") {
    CHECK(sae::sae_function_names(0) == "none");
    // Bit 2 is unused by AMD1 Table M.6.
    CHECK(sae::sae_function_names(1U << 2) == "none");
    CHECK(sae::sae_function_names(sae_function_bit(Fn::VoltVarFunction)) == "VoltVarFunction");
    CHECK(sae::sae_function_names(sae_function_bit(Fn::VoltVarFunction) | sae_function_bit(Fn::ChargeFunction)) ==
          "ChargeFunction, VoltVarFunction");
}

SCENARIO("SAE is_function_set reads one bit") {
    const auto bitmap = sae_function_bit(Fn::EnterService) | sae_function_bit(Fn::WattVarFunction);
    CHECK(sae::is_function_set(bitmap, Fn::EnterService));
    CHECK(sae::is_function_set(bitmap, Fn::WattVarFunction));
    CHECK_FALSE(sae::is_function_set(bitmap, Fn::VoltVarFunction));
    CHECK_FALSE(sae::is_function_set(0, Fn::ChargeFunction));
}
