// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "dc_output_disabled_confirmation.hpp"

namespace module {
namespace {

TEST(DcOutputDisabledConfirmationTest, confirms_immediately_before_any_switch_off) {
    DcOutputDisabledConfirmation confirmation;
    EXPECT_TRUE(confirmation.request());
}

TEST(DcOutputDisabledConfirmationTest, waits_for_two_measurements_after_switch_off) {
    DcOutputDisabledConfirmation confirmation;
    confirmation.on_output_off();

    EXPECT_FALSE(confirmation.request());
    EXPECT_FALSE(confirmation.on_measurement(0.0f));
    EXPECT_TRUE(confirmation.on_measurement(0.0f));
    // Confirmed once per request
    EXPECT_FALSE(confirmation.on_measurement(0.0f));
}

TEST(DcOutputDisabledConfirmationTest, measurements_before_the_request_count) {
    DcOutputDisabledConfirmation confirmation;
    confirmation.on_output_off();
    EXPECT_FALSE(confirmation.on_measurement(0.0f));
    EXPECT_FALSE(confirmation.on_measurement(0.0f));

    EXPECT_TRUE(confirmation.request());
}

TEST(DcOutputDisabledConfirmationTest, measurements_before_the_switch_off_do_not_count) {
    DcOutputDisabledConfirmation confirmation;
    EXPECT_FALSE(confirmation.on_measurement(0.0f));
    EXPECT_FALSE(confirmation.on_measurement(0.0f));
    confirmation.on_output_off();

    EXPECT_FALSE(confirmation.request());
    EXPECT_FALSE(confirmation.on_measurement(0.0f));
    EXPECT_TRUE(confirmation.on_measurement(0.0f));
}

TEST(DcOutputDisabledConfirmationTest, measurements_without_request_confirm_nothing) {
    DcOutputDisabledConfirmation confirmation;
    confirmation.on_output_off();
    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(confirmation.on_measurement(0.0f));
    }
}

TEST(DcOutputDisabledConfirmationTest, switch_off_during_pending_request_restarts_counting) {
    DcOutputDisabledConfirmation confirmation;
    confirmation.on_output_off();
    EXPECT_FALSE(confirmation.request());
    EXPECT_FALSE(confirmation.on_measurement(0.0f));

    confirmation.on_output_off();
    EXPECT_FALSE(confirmation.on_measurement(0.0f));
    EXPECT_TRUE(confirmation.on_measurement(0.0f));
}

TEST(DcOutputDisabledConfirmationTest, waits_for_the_current_to_drop_below_5_A) {
    DcOutputDisabledConfirmation confirmation;
    confirmation.on_output_off();

    EXPECT_FALSE(confirmation.request());
    EXPECT_FALSE(confirmation.on_measurement(80.0f));
    EXPECT_FALSE(confirmation.on_measurement(12.0f));
    EXPECT_FALSE(confirmation.on_measurement(5.0f));
    EXPECT_FALSE(confirmation.on_measurement(4.9f));
    EXPECT_TRUE(confirmation.on_measurement(0.2f));
}

TEST(DcOutputDisabledConfirmationTest, current_of_5_A_or_more_restarts_the_count) {
    DcOutputDisabledConfirmation confirmation;
    confirmation.on_output_off();

    EXPECT_FALSE(confirmation.request());
    EXPECT_FALSE(confirmation.on_measurement(0.0f));
    EXPECT_FALSE(confirmation.on_measurement(6.0f));
    EXPECT_FALSE(confirmation.on_measurement(0.0f));
    EXPECT_TRUE(confirmation.on_measurement(0.0f));
}

TEST(DcOutputDisabledConfirmationTest, discharge_current_counts_by_magnitude) {
    DcOutputDisabledConfirmation confirmation;
    confirmation.on_output_off();

    EXPECT_FALSE(confirmation.request());
    EXPECT_FALSE(confirmation.on_measurement(-20.0f));
    EXPECT_FALSE(confirmation.on_measurement(-1.0f));
    EXPECT_TRUE(confirmation.on_measurement(-0.5f));
}

TEST(DcOutputDisabledConfirmationTest, stale_high_current_before_the_request_blocks_an_immediate_confirmation) {
    DcOutputDisabledConfirmation confirmation;
    confirmation.on_output_off();
    EXPECT_FALSE(confirmation.on_measurement(0.0f));
    EXPECT_FALSE(confirmation.on_measurement(30.0f));

    EXPECT_FALSE(confirmation.request());
    EXPECT_FALSE(confirmation.on_measurement(0.0f));
    EXPECT_TRUE(confirmation.on_measurement(0.0f));
}

} // namespace
} // namespace module
