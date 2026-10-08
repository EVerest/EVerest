// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <algorithm>
#include <bitset>
#include <initializer_list>

#include <iso15118/d20/der_functions.hpp>

#include "der_setup.hpp"

namespace {

using module::map_ev_supported_der_controls;
using DT = types::grid_support::DirectiveType;
using iso15118::iec::DERControlName;

std::bitset<12> bits_for(std::initializer_list<DERControlName> names) {
    std::bitset<12> b{};
    for (const auto name : names) {
        b.set(static_cast<size_t>(name));
    }
    return b;
}

} // namespace

TEST(DerControlsTest, empty_selection_yields_nullopt) {
    // ev_supported_dercontrol has minItems:1, so an empty selection must be unset, not [].
    EXPECT_FALSE(map_ev_supported_der_controls(std::bitset<12>{}).has_value());
}

TEST(DerControlsTest, single_bit_maps_to_its_directive_type) {
    const auto out = map_ev_supported_der_controls(bits_for({DERControlName::VoltVarMode}));
    ASSERT_TRUE(out.has_value());
    ASSERT_EQ(out->size(), 1u);
    EXPECT_EQ(out->front(), DT::VoltVar);
}

TEST(DerControlsTest, over_and_under_frequency_watt_dedupe_to_single_freqwatt) {
    const auto out = map_ev_supported_der_controls(
        bits_for({DERControlName::OverFrequencyWattMode, DERControlName::UnderFrequencyWattMode}));
    ASSERT_TRUE(out.has_value());
    ASSERT_EQ(out->size(), 1u);
    EXPECT_EQ(out->front(), DT::FreqWatt);
}

TEST(DerControlsTest, each_remaining_name_maps_to_its_directive_type) {
    struct Case {
        DERControlName name;
        DT expected;
    };
    const Case cases[] = {
        {DERControlName::VoltWattMode, DT::VoltWatt},
        {DERControlName::VoltVarMode, DT::VoltVar},
        {DERControlName::WattVarMode, DT::WattVar},
        {DERControlName::WattCosPhiMode, DT::WattPF},
        {DERControlName::DSOQSetpointProvision, DT::DSOQSetpoint},
        {DERControlName::DSOCosPhiSetpointProvision, DT::DSOCosPhiSetpoint},
        {DERControlName::DCInjectionRestriction, DT::MaximumLevelDCInjection},
        {DERControlName::ZeroCurrentMode, DT::ZeroCurrent},
        {DERControlName::OverVoltageFaultRideThroughMode, DT::OvervoltageFaultRideThrough},
        {DERControlName::UnderVoltageFaultRideThroughMode, DT::UndervoltageFaultRideThrough},
    };
    for (const auto& c : cases) {
        const auto out = map_ev_supported_der_controls(bits_for({c.name}));
        ASSERT_TRUE(out.has_value());
        ASSERT_EQ(out->size(), 1u);
        EXPECT_EQ(out->front(), c.expected);
    }
}

TEST(DerControlsTest, all_bits_set_yields_eleven_distinct_types) {
    // 12 control names, but Over/UnderFrequencyWatt collapse to one FreqWatt -> 11 distinct.
    std::bitset<12> all{};
    all.set();
    const auto out = map_ev_supported_der_controls(all);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->size(), 11u);
    // No duplicates.
    for (auto it = out->begin(); it != out->end(); ++it) {
        EXPECT_EQ(std::count(out->begin(), out->end(), *it), 1);
    }
}
