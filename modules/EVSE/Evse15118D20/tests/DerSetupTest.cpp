// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <optional>

#include <iso15118/d20/limits.hpp>
#include <iso15118/message/common_types.hpp>

#include "der_setup.hpp"

namespace dt = iso15118::message_20::datatypes;

namespace {

bool equal(const dt::RationalNumber& a, const dt::RationalNumber& b) {
    return a.value == b.value and a.exponent == b.exponent;
}

iso15118::d20::AcTransferLimits make_ac_limits(float charge_max, std::optional<float> discharge_max) {
    iso15118::d20::AcTransferLimits ac{};
    ac.charge_power.max = dt::from_float(charge_max);
    ac.charge_power.min = dt::from_float(0.0f);
    ac.nominal_frequency = dt::from_float(50.0f);
    if (discharge_max.has_value()) {
        iso15118::d20::Limit<dt::RationalNumber> discharge{};
        discharge.max = dt::from_float(discharge_max.value());
        discharge.min = dt::from_float(0.0f);
        ac.discharge_power = discharge;
    }
    return ac;
}

} // namespace

TEST(DerSetupTest, nominal_charge_equals_ac_charge_max) {
    const auto ac = make_ac_limits(7400.0f, 11000.0f);

    const auto limits = module::build_iec_der_transfer_limits(ac);

    EXPECT_TRUE(equal(limits.nominal_charge_power, ac.charge_power.max));
}

TEST(DerSetupTest, discharge_present_passes_through_to_both_fields) {
    const auto ac = make_ac_limits(11000.0f, 11000.0f);

    const auto limits = module::build_iec_der_transfer_limits(ac);

    EXPECT_TRUE(equal(limits.nominal_discharge_power, ac.discharge_power->max));
    EXPECT_TRUE(equal(limits.max_discharge_power, ac.discharge_power->max));
}

TEST(DerSetupTest, discharge_absent_yields_zero_discharge) {
    const auto ac = make_ac_limits(11000.0f, std::nullopt);

    const auto limits = module::build_iec_der_transfer_limits(ac);

    EXPECT_TRUE(equal(limits.nominal_charge_power, ac.charge_power.max));
    EXPECT_TRUE(equal(limits.nominal_discharge_power, dt::from_float(0.0f)));
    EXPECT_TRUE(equal(limits.max_discharge_power, dt::from_float(0.0f)));
}

TEST(DerSetupTest, negative_ac_discharge_yields_negative_outputs) {
    const auto ac = make_ac_limits(11000.0f, -11000.0f);

    const auto limits = module::build_iec_der_transfer_limits(ac);

    EXPECT_TRUE(equal(limits.nominal_discharge_power, ac.discharge_power->max));
    EXPECT_TRUE(equal(limits.max_discharge_power, ac.discharge_power->max));
    EXPECT_LT(dt::from_RationalNumber(limits.max_discharge_power), 0.0f);
    // Charge stays positive.
    EXPECT_TRUE(equal(limits.nominal_charge_power, ac.charge_power.max));
    EXPECT_GT(dt::from_RationalNumber(limits.nominal_charge_power), 0.0f);
}

TEST(DerSetupTest, positive_ac_discharge_yields_positive_outputs) {
    const auto ac = make_ac_limits(11000.0f, 11000.0f);

    const auto limits = module::build_iec_der_transfer_limits(ac);

    EXPECT_GT(dt::from_RationalNumber(limits.max_discharge_power), 0.0f);
    EXPECT_GT(dt::from_RationalNumber(limits.nominal_discharge_power), 0.0f);
}
