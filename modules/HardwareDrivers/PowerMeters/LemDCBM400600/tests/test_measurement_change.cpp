// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Unit tests for the publication filter of the live measurement loop: a sample the device reports again on
// the next poll is not published again. Models a device refreshing once per second polled every 200 ms.

#include "measurement_change.hpp"
#include <gtest/gtest.h>

namespace module::main {

namespace {

types::powermeter::Powermeter sample(const std::string& timestamp, float voltage, float current) {
    types::powermeter::Powermeter p;
    p.timestamp = timestamp;
    p.meter_id = "LEM-1";
    p.energy_Wh_import = {12345.0f};
    p.energy_Wh_export = types::units::Energy{0.0f};
    p.voltage_V = types::units::Voltage{};
    p.voltage_V->DC = voltage;
    p.current_A = types::units::Current{};
    p.current_A->DC = current;
    p.power_W = types::units::Power{voltage * current};
    return p;
}

} // namespace

/// \brief Nothing was published yet, so the first sample is new
TEST(MeasurementChangeTest, first_sample_is_new) {
    EXPECT_TRUE(is_new_measurement(std::nullopt, sample("2026-10-15T10:00:00.000+02:00", 400.0f, 0.0f)));
}

/// \brief A 1 Hz device polled every 200 ms returns the same sample four more times; none of them is new
TEST(MeasurementChangeTest, repeated_sample_of_a_slow_device_is_not_new) {
    const auto published = sample("2026-10-15T10:00:00.000+02:00", 400.0f, 0.0f);
    for (int poll = 1; poll < 5; ++poll) {
        EXPECT_FALSE(is_new_measurement(published, sample("2026-10-15T10:00:00.000+02:00", 400.0f, 0.0f)))
            << "poll " << poll;
    }
}

/// \brief The next refresh carries a new timestamp; it is new even when the values did not change
///
/// This is the welding detection case: side B stays at 0 V, and each measurement of it has to reach the
/// HLC stack so that a WeldingDetectionReq is answered with a value measured after it.
TEST(MeasurementChangeTest, next_refresh_with_equal_values_is_new) {
    const auto published = sample("2026-10-15T10:00:00.000+02:00", 0.0f, 0.0f);
    EXPECT_TRUE(is_new_measurement(published, sample("2026-10-15T10:00:01.000+02:00", 0.0f, 0.0f)));
}

/// \brief Changed values with an unchanged timestamp are new: a coarse timestamp cannot hide a measurement
TEST(MeasurementChangeTest, changed_values_with_equal_timestamp_are_new) {
    const auto published = sample("2026-10-15T10:00:00.000+02:00", 400.0f, 10.0f);
    EXPECT_TRUE(is_new_measurement(published, sample("2026-10-15T10:00:00.000+02:00", 399.5f, 10.0f)));
    EXPECT_TRUE(is_new_measurement(published, sample("2026-10-15T10:00:00.000+02:00", 400.0f, 9.5f)));
}

} // namespace module::main
