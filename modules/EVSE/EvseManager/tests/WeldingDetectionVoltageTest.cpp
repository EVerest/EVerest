// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "welding_detection/WeldingDetectionVoltage.hpp"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace module;
using namespace std::chrono_literals;

namespace {

using Source = WeldingDetectionVoltageSource;

struct Recorder {
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<float> pushed;
    int stops{0};
    std::vector<std::string> warnings;

    template <typename Predicate> bool wait(Predicate predicate, std::chrono::milliseconds timeout = 1000ms) {
        std::unique_lock lock(mtx);
        return cv.wait_for(lock, timeout, [&] { return predicate(*this); });
    }
};

std::unique_ptr<WeldingDetectionVoltage> make(Recorder& rec, Source source, WeldingDetectionVoltage::Timing timing) {
    return std::make_unique<WeldingDetectionVoltage>(
        source,
        [&rec](float voltage_V) {
            std::scoped_lock lock(rec.mtx);
            rec.pushed.push_back(voltage_V);
            rec.cv.notify_all();
        },
        [&rec] {
            std::scoped_lock lock(rec.mtx);
            rec.stops++;
            rec.cv.notify_all();
        },
        [&rec](const std::string& message) {
            std::scoped_lock lock(rec.mtx);
            rec.warnings.push_back(message);
            rec.cv.notify_all();
        },
        timing);
}

// No timer fires while a test drives the phase itself; a test that waits for one shortens that timer.
WeldingDetectionVoltage::Timing idle_timing() {
    WeldingDetectionVoltage::Timing timing;
    timing.watchdog_timeout = 5000ms;
    timing.slow_sample_interval = 5000ms;
    timing.deferred_stop_timeout = 5000ms;
    return timing;
}

} // namespace

TEST(WeldingDetectionVoltageSourceTest, parses_config_values) {
    EXPECT_EQ(welding_detection_voltage_source_from_string("powermeter_car_side"), Source::PowermeterCarSide);
    EXPECT_EQ(welding_detection_voltage_source_from_string("over_voltage_monitor"), Source::OverVoltageMonitor);
    EXPECT_EQ(welding_detection_voltage_source_from_string("isolation_monitor"), Source::IsolationMonitor);
    EXPECT_EQ(welding_detection_voltage_source_from_string("power_supply"), Source::PowerSupply);
    EXPECT_FALSE(welding_detection_voltage_source_from_string("imd").has_value());
    EXPECT_EQ(to_string(Source::OverVoltageMonitor), "over_voltage_monitor");
}

TEST(WeldingDetectionVoltageSourceTest, keeps_a_connected_source) {
    const WeldingDetectionVoltageSources all{true, true, true};
    EXPECT_EQ(resolve_welding_detection_voltage_source(Source::PowermeterCarSide, all), Source::PowermeterCarSide);
    EXPECT_EQ(resolve_welding_detection_voltage_source(Source::IsolationMonitor, all), Source::IsolationMonitor);
    EXPECT_EQ(resolve_welding_detection_voltage_source(Source::PowerSupply, all), Source::PowerSupply);
}

TEST(WeldingDetectionVoltageSourceTest, falls_back_in_order) {
    EXPECT_EQ(resolve_welding_detection_voltage_source(Source::PowermeterCarSide, {false, true, true}),
              Source::OverVoltageMonitor);
    EXPECT_EQ(resolve_welding_detection_voltage_source(Source::PowermeterCarSide, {false, false, false}),
              Source::PowerSupply);
    // Never back up the chain.
    EXPECT_EQ(resolve_welding_detection_voltage_source(Source::IsolationMonitor, {true, true, false}),
              Source::PowerSupply);
}

TEST(WeldingDetectionVoltageSourceTest, never_falls_back_to_the_isolation_monitor) {
    EXPECT_EQ(resolve_welding_detection_voltage_source(Source::PowermeterCarSide, {false, false, true}),
              Source::PowerSupply);
    EXPECT_EQ(resolve_welding_detection_voltage_source(Source::OverVoltageMonitor, {true, false, true}),
              Source::PowerSupply);
}

TEST(WeldingDetectionVoltageTest, pushes_only_the_selected_source_while_active) {
    Recorder rec;
    auto welding = make(rec, Source::OverVoltageMonitor, idle_timing());

    welding->update(Source::OverVoltageMonitor, 400.0f);
    welding->start();
    welding->update(Source::PowerSupply, 20.0f);
    welding->update(Source::OverVoltageMonitor, 398.0f);
    welding->finish();
    welding->update(Source::OverVoltageMonitor, 397.0f);

    std::scoped_lock lock(rec.mtx);
    EXPECT_EQ(rec.pushed, std::vector<float>{398.0f});
    EXPECT_TRUE(rec.warnings.empty());
}

TEST(WeldingDetectionVoltageTest, deferred_stop_runs_once_at_finish) {
    Recorder rec;
    auto welding = make(rec, Source::OverVoltageMonitor, idle_timing());

    welding->defer_source_stop();
    EXPECT_TRUE(welding->holds_source());
    EXPECT_FALSE(welding->is_active());
    welding->start();
    welding->finish();
    welding->finish();

    EXPECT_FALSE(welding->holds_source());
    std::scoped_lock lock(rec.mtx);
    EXPECT_EQ(rec.stops, 1);
}

TEST(WeldingDetectionVoltageTest, finish_without_deferral_stops_nothing) {
    Recorder rec;
    auto welding = make(rec, Source::OverVoltageMonitor, idle_timing());

    welding->start();
    welding->finish();

    std::scoped_lock lock(rec.mtx);
    EXPECT_EQ(rec.stops, 0);
}

TEST(WeldingDetectionVoltageTest, deferred_stop_without_a_phase_runs_at_the_next_end) {
    Recorder rec;
    auto welding = make(rec, Source::IsolationMonitor, idle_timing());

    welding->defer_source_stop();
    welding->finish();

    std::scoped_lock lock(rec.mtx);
    EXPECT_EQ(rec.stops, 1);
}

TEST(WeldingDetectionVoltageTest, deferred_stop_without_a_phase_runs_after_the_timeout) {
    Recorder rec;
    auto timing = idle_timing();
    timing.deferred_stop_timeout = 50ms;
    auto welding = make(rec, Source::IsolationMonitor, timing);

    welding->defer_source_stop();
    EXPECT_TRUE(welding->holds_source());

    ASSERT_TRUE(rec.wait([](auto& r) { return r.stops == 1; }));
    EXPECT_FALSE(welding->holds_source());
    welding->finish();
    std::scoped_lock lock(rec.mtx);
    EXPECT_EQ(rec.stops, 1);
    EXPECT_EQ(rec.warnings.size(), 1);
}

TEST(WeldingDetectionVoltageTest, a_phase_started_in_time_keeps_the_deferred_stop) {
    Recorder rec;
    auto timing = idle_timing();
    timing.deferred_stop_timeout = 50ms;
    auto welding = make(rec, Source::OverVoltageMonitor, timing);

    welding->defer_source_stop();
    welding->start();
    std::this_thread::sleep_for(100ms);
    EXPECT_TRUE(welding->holds_source());
    {
        std::scoped_lock lock(rec.mtx);
        EXPECT_EQ(rec.stops, 0);
    }

    welding->finish();
    std::scoped_lock lock(rec.mtx);
    EXPECT_EQ(rec.stops, 1);
}

TEST(WeldingDetectionVoltageTest, watchdog_ends_the_phase) {
    Recorder rec;
    auto timing = idle_timing();
    timing.watchdog_timeout = 200ms;
    auto welding = make(rec, Source::OverVoltageMonitor, timing);

    welding->defer_source_stop();
    welding->start();
    welding->update(Source::OverVoltageMonitor, 400.0f);

    ASSERT_TRUE(rec.wait([](auto& r) { return r.stops == 1; }));
    EXPECT_FALSE(welding->is_active());
    EXPECT_FALSE(welding->holds_source());
    std::scoped_lock lock(rec.mtx);
    EXPECT_EQ(rec.warnings.size(), 1);
}

TEST(WeldingDetectionVoltageTest, warns_once_about_a_slow_source) {
    Recorder rec;
    auto timing = idle_timing();
    timing.slow_sample_interval = 20ms;
    auto welding = make(rec, Source::OverVoltageMonitor, timing);

    welding->start();
    welding->update(Source::OverVoltageMonitor, 400.0f);
    std::this_thread::sleep_for(40ms);
    welding->update(Source::OverVoltageMonitor, 399.0f);
    std::this_thread::sleep_for(40ms);
    welding->update(Source::OverVoltageMonitor, 398.0f);
    welding->finish();

    std::scoped_lock lock(rec.mtx);
    EXPECT_EQ(rec.pushed.size(), 3);
    EXPECT_EQ(rec.warnings.size(), 1);
}

TEST(WeldingDetectionVoltageTest, restart_after_finish_starts_a_new_phase) {
    Recorder rec;
    auto welding = make(rec, Source::PowerSupply, idle_timing());

    welding->start();
    welding->update(Source::PowerSupply, 10.0f);
    welding->finish();
    welding->start();
    welding->update(Source::PowerSupply, 11.0f);
    welding->finish();

    std::scoped_lock lock(rec.mtx);
    EXPECT_EQ(rec.pushed, (std::vector<float>{10.0f, 11.0f}));
}
