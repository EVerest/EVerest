// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "PhaseAllocation.hpp"

namespace module {

TEST(PerPhaseAllocation, DefaultIsAllZero) {
    PhaseAllocation a;
    EXPECT_FLOAT_EQ(a.l1_A, 0.0f);
    EXPECT_FLOAT_EQ(a.l2_A, 0.0f);
    EXPECT_FLOAT_EQ(a.l3_A, 0.0f);
    EXPECT_FLOAT_EQ(a.imbalance_A(), 0.0f);
}

TEST(PerPhaseAllocation, MinMaxAndImbalance) {
    const PhaseAllocation a{16.0f, 10.0f, 6.0f};

    EXPECT_FLOAT_EQ(a.min(), 6.0f);
    EXPECT_FLOAT_EQ(a.max(), 16.0f);
    EXPECT_FLOAT_EQ(a.imbalance_A(), 10.0f);
}

TEST(PerPhaseAllocation, PhaseAccessIsOneBased) {
    PhaseAllocation a{1.0f, 2.0f, 3.0f};

    EXPECT_FLOAT_EQ(a.phase(1), 1.0f);
    EXPECT_FLOAT_EQ(a.phase(2), 2.0f);
    EXPECT_FLOAT_EQ(a.phase(3), 3.0f);

    a.phase(2) = 9.0f;
    EXPECT_FLOAT_EQ(a.l2_A, 9.0f);
}

TEST(PerPhaseAllocation, AdditionIsPerPhase) {
    const PhaseAllocation a{1.0f, 2.0f, 3.0f};
    const PhaseAllocation b{10.0f, 20.0f, 30.0f};

    const auto sum = a + b;

    EXPECT_FLOAT_EQ(sum.l1_A, 11.0f);
    EXPECT_FLOAT_EQ(sum.l2_A, 22.0f);
    EXPECT_FLOAT_EQ(sum.l3_A, 33.0f);
}

TEST(PerPhaseSymmetry, WithinLimitWhenBalanced) {
    EXPECT_TRUE(is_within_symmetry({16.0f, 16.0f, 16.0f}, 16.0f));
    EXPECT_TRUE(is_within_symmetry({16.0f, 10.0f, 6.0f}, 10.0f));
}

TEST(PerPhaseSymmetry, ViolatedWhenSpreadExceedsLimit) {
    EXPECT_FALSE(is_within_symmetry({32.0f, 6.0f, 6.0f}, 16.0f));
}

TEST(PerPhaseSymmetry, ZeroImbalanceLimitRequiresPerfectBalance) {
    EXPECT_TRUE(is_within_symmetry({16.0f, 16.0f, 16.0f}, 0.0f));
    EXPECT_FALSE(is_within_symmetry({16.0f, 16.0f, 15.5f}, 0.0f));
}

TEST(PerPhaseEffectiveLimit, SymmetricLimitAppliesToAllPhases) {
    types::energy::LimitsReq limits;
    limits.ac_max_current_A = {16.0f, "TEST"};

    const auto effective = effective_per_phase_limit(limits, 3);

    EXPECT_FLOAT_EQ(effective.l1_A, 16.0f);
    EXPECT_FLOAT_EQ(effective.l2_A, 16.0f);
    EXPECT_FLOAT_EQ(effective.l3_A, 16.0f);
}

TEST(PerPhaseEffectiveLimit, InactivePhasesAreZero) {
    types::energy::LimitsReq limits;
    limits.ac_max_current_A = {16.0f, "TEST"};

    // A single phase connector can only draw on L1.
    const auto effective = effective_per_phase_limit(limits, 1);

    EXPECT_FLOAT_EQ(effective.l1_A, 16.0f);
    EXPECT_FLOAT_EQ(effective.l2_A, 0.0f);
    EXPECT_FLOAT_EQ(effective.l3_A, 0.0f);
}

TEST(PerPhaseEffectiveLimit, PerPhaseLimitOverridesWhenSmaller) {
    types::energy::LimitsReq limits;
    limits.ac_max_current_A = {32.0f, "TEST_symmetric"};

    types::energy::NumberWithSourcePerPhase per_phase;
    per_phase.L1 = types::energy::NumberWithSource{20.0f, "TEST_L1"};
    per_phase.L2 = types::energy::NumberWithSource{10.0f, "TEST_L2"};
    // L3 omitted: not limited beyond the symmetric value.
    limits.ac_max_current_per_phase_A = per_phase;

    const auto effective = effective_per_phase_limit(limits, 3);

    EXPECT_FLOAT_EQ(effective.l1_A, 20.0f);
    EXPECT_FLOAT_EQ(effective.l2_A, 10.0f);
    EXPECT_FLOAT_EQ(effective.l3_A, 32.0f);
}

TEST(PerPhaseEffectiveLimit, SymmetricLimitStillWinsWhenSmaller) {
    types::energy::LimitsReq limits;
    limits.ac_max_current_A = {8.0f, "TEST_symmetric"};

    types::energy::NumberWithSourcePerPhase per_phase;
    per_phase.L1 = types::energy::NumberWithSource{20.0f, "TEST_L1"};
    limits.ac_max_current_per_phase_A = per_phase;

    const auto effective = effective_per_phase_limit(limits, 3);

    // Neither side may be widened by the other.
    EXPECT_FLOAT_EQ(effective.l1_A, 8.0f);
}

TEST(PerPhaseEffectiveLimit, OmittedPhaseWithoutSymmetricLimitIsZero) {
    // Edge case pinned deliberately: a per phase object with an omitted phase and no
    // symmetric limit yields zero for that phase. The yaml reads "a phase that is omitted
    // is not limited by this object" - but with no other limit present there is nothing
    // to fall back to, and treating it as unlimited would let the broker buy unbounded
    // current. Zero is the safe direction. Unreachable with current producers, which
    // always fill all three phases.
    types::energy::LimitsReq limits;
    types::energy::NumberWithSourcePerPhase per_phase;
    per_phase.L1 = types::energy::NumberWithSource{20.0f, "TEST_L1"};
    limits.ac_max_current_per_phase_A = per_phase;

    const auto effective = effective_per_phase_limit(limits, 3);

    EXPECT_FLOAT_EQ(effective.l1_A, 20.0f);
    EXPECT_FLOAT_EQ(effective.l2_A, 0.0f);
    EXPECT_FLOAT_EQ(effective.l3_A, 0.0f);
}

TEST(PerPhaseEffectiveLimit, NoLimitsAtAllYieldsZero) {
    const types::energy::LimitsReq limits;

    const auto effective = effective_per_phase_limit(limits, 3);

    EXPECT_FLOAT_EQ(effective.l1_A, 0.0f);
    EXPECT_FLOAT_EQ(effective.l2_A, 0.0f);
    EXPECT_FLOAT_EQ(effective.l3_A, 0.0f);
}

TEST(PerPhaseConversion, ToPerPhaseLimitCarriesSource) {
    const auto limit = to_per_phase_limit({16.0f, 10.0f, 6.0f}, "TEST_source");

    ASSERT_TRUE(limit.L1.has_value());
    ASSERT_TRUE(limit.L2.has_value());
    ASSERT_TRUE(limit.L3.has_value());
    EXPECT_FLOAT_EQ(limit.L1.value().value, 16.0f);
    EXPECT_FLOAT_EQ(limit.L2.value().value, 10.0f);
    EXPECT_FLOAT_EQ(limit.L3.value().value, 6.0f);
    EXPECT_EQ(limit.L1.value().source, "TEST_source");
}

} // namespace module
