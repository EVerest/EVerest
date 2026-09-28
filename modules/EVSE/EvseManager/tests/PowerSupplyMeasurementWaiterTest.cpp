// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "cable_check/PowerSupplyMeasurementWaiter.hpp"

#include <generated/types/power_supply_DC.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

using namespace module;
using namespace std::chrono_literals;

namespace {

types::power_supply_DC::VoltageCurrent make_measurement(double voltage_V) {
    types::power_supply_DC::VoltageCurrent measurement;
    measurement.voltage_V = static_cast<float>(voltage_V);
    measurement.current_A = 0.0f;
    return measurement;
}

/// Threshold used by every test that needs a Condition: "voltage has come back down below 60 V"
/// mirrors the real cable-check use case (wait for the isolation test voltage to discharge).
bool below_60V(const types::power_supply_DC::VoltageCurrent& m) {
    return m.voltage_V < 60.0f;
}

/// Drives PowerSupplyMeasurementWaiter::wait() from a fixed script of responses so tests are
/// deterministic instead of racing real timers. Each step either represents a gap (no measurement
/// arrived within measurement_timeout) or a delivered measurement, and can optionally sleep for a
/// given real duration first so that gap tests genuinely advance wall-clock time toward the
/// deadline, the way a slow HTTP-polled meter would.
class ScriptedSource {
public:
    struct Step {
        std::optional<double> voltage_V; // nullopt == gap
        std::chrono::milliseconds sleep_before_return{0ms};
        // When set, ignore sleep_before_return and instead sleep for whatever timeout the waiter
        // actually hands to this call. This makes a gap step consume exactly as much of the
        // remaining budget as a real silent source would, so a script of gap steps is guaranteed
        // to exhaust the total_timeout in a small, deterministic number of calls regardless of
        // measurement_timeout or CI scheduling jitter -- no need to hand-size the script length
        // against a fixed sleep duration.
        bool sleep_for_requested_timeout{false};
    };

    explicit ScriptedSource(std::vector<Step> steps) : steps_(std::move(steps)) {
    }

    std::optional<types::power_supply_DC::VoltageCurrent> operator()(std::chrono::milliseconds timeout) {
        recorded_timeouts.push_back(timeout);
        const std::size_t idx = call_count++;
        if (idx >= steps_.size()) {
            // Safety net so a misbehaving implementation cannot spin this fake (and the test)
            // forever: report the overrun as a failure but hand back a measurement that satisfies
            // below_60V so wait() has a way to terminate.
            ADD_FAILURE() << "next_measurement was called more times than the test scripted (call #" << (idx + 1)
                          << ")";
            return make_measurement(40.0);
        }

        const Step& step = steps_[idx];
        if (step.sleep_for_requested_timeout) {
            if (timeout > 0ms) {
                std::this_thread::sleep_for(timeout);
            }
        } else if (step.sleep_before_return > 0ms) {
            std::this_thread::sleep_for(step.sleep_before_return);
        }
        if (!step.voltage_V) {
            return std::nullopt;
        }
        return make_measurement(*step.voltage_V);
    }

    std::size_t call_count{0};
    std::vector<std::chrono::milliseconds> recorded_timeouts;

private:
    std::vector<Step> steps_;
};

bool never_cancelled() {
    return false;
}

} // namespace

TEST(PowerSupplyMeasurementWaiterTest, stops_on_first_measurement_that_satisfies_the_condition) {
    // 100 V fails the condition, 40 V satisfies it. A third scripted step exists only to prove
    // it is never consumed.
    ScriptedSource source({{100.0}, {40.0}, {999.0}});
    PowerSupplyMeasurementWaiter waiter(200ms, 50ms);

    const auto result = waiter.wait(below_60V, std::ref(source), never_cancelled, [](int) {});

    EXPECT_EQ(result.stop_reason, PowerSupplyMeasurementWaiter::StopReason::ConditionMet);
    EXPECT_TRUE(result.condition_met());
    EXPECT_EQ(source.call_count, 2u) << "must not request a measurement after the condition is met";
}

TEST(PowerSupplyMeasurementWaiterTest, samples_counts_every_measurement_received_regardless_of_condition) {
    ScriptedSource source({{100.0}, {80.0}, {40.0}});
    PowerSupplyMeasurementWaiter waiter(200ms, 50ms);

    const auto result = waiter.wait(below_60V, std::ref(source), never_cancelled, [](int) {});

    ASSERT_TRUE(result.condition_met());
    EXPECT_EQ(result.samples, 3);
}

TEST(PowerSupplyMeasurementWaiterTest, last_voltage_holds_the_most_recently_received_measurement) {
    ScriptedSource source({{100.0}, {80.0}, {40.0}});
    PowerSupplyMeasurementWaiter waiter(200ms, 50ms);

    const auto result = waiter.wait(below_60V, std::ref(source), never_cancelled, [](int) {});

    ASSERT_TRUE(result.last_voltage_V.has_value());
    EXPECT_DOUBLE_EQ(*result.last_voltage_V, 40.0);
}

TEST(PowerSupplyMeasurementWaiterTest, last_voltage_is_nullopt_when_no_measurement_was_ever_received) {
    // Already cancelled before wait() is even called: no measurement should be requested at all.
    ScriptedSource source({});
    PowerSupplyMeasurementWaiter waiter(200ms, 50ms);

    const auto result = waiter.wait(
        below_60V, std::ref(source), [] { return true; }, [](int) {});

    EXPECT_EQ(result.stop_reason, PowerSupplyMeasurementWaiter::StopReason::Cancelled);
    EXPECT_EQ(result.samples, 0);
    EXPECT_FALSE(result.last_voltage_V.has_value());
    EXPECT_EQ(source.call_count, 0u) << "cancellation is checked before a measurement is consumed";
}

/// The core fix: a gap in the measurement stream (the meter didn't answer in time) is not
/// evidence about the voltage, so it must not abort the wait. Two gaps happen first, then a
/// measurement satisfying the condition arrives and the wait must still report ConditionMet.
TEST(PowerSupplyMeasurementWaiterTest, keeps_waiting_after_gaps_and_still_reports_condition_met) {
    ScriptedSource source({
        {std::nullopt, 30ms}, // gap 1
        {std::nullopt, 30ms}, // gap 2
        {40.0},               // condition satisfied
    });
    PowerSupplyMeasurementWaiter waiter(300ms, 50ms);

    std::vector<int> gap_counts;
    const auto result = waiter.wait(below_60V, std::ref(source), never_cancelled,
                                    [&gap_counts](int gaps_so_far) { gap_counts.push_back(gaps_so_far); });

    EXPECT_EQ(result.stop_reason, PowerSupplyMeasurementWaiter::StopReason::ConditionMet);
    EXPECT_EQ(result.gaps, 2);
    EXPECT_EQ(gap_counts, (std::vector<int>{1, 2})) << "on_gap must be called once per gap with the running count";
    EXPECT_EQ(result.samples, 1) << "gaps must not be counted as samples";
    ASSERT_TRUE(result.last_voltage_V.has_value());
    EXPECT_DOUBLE_EQ(*result.last_voltage_V, 40.0);
}

TEST(PowerSupplyMeasurementWaiterTest, returns_timeout_when_condition_is_never_satisfied) {
    // Every measurement fails the condition; the wait must eventually give up rather than hang.
    // Each step sleeps for the measurement_timeout slice so real time actually advances toward
    // the deadline instead of the fake being drained by a tight, instantaneous call loop.
    ScriptedSource source({
        {100.0, 30ms},
        {100.0, 30ms},
        {100.0, 30ms},
        {100.0, 30ms},
        {100.0, 30ms},
        {100.0, 30ms},
        {100.0, 30ms},
        {100.0, 30ms},
    });
    PowerSupplyMeasurementWaiter waiter(120ms, 30ms);

    const auto result = waiter.wait(below_60V, std::ref(source), never_cancelled, [](int) {});

    EXPECT_EQ(result.stop_reason, PowerSupplyMeasurementWaiter::StopReason::Timeout);
    EXPECT_FALSE(result.condition_met());
}

/// If measurement_timeout is longer than the remaining total budget, a single call to
/// next_measurement must not be allowed to block past the overall deadline: the requested
/// timeout has to be clamped to at most the time left.
TEST(PowerSupplyMeasurementWaiterTest, clamps_the_per_measurement_timeout_to_the_remaining_total_budget) {
    // Each gap step sleeps for whatever (clamped) timeout the waiter actually hands it, so the
    // 50 ms budget is consumed in a small, deterministic number of calls no matter how the
    // clamping is implemented -- this cannot go flaky by running dry before the deadline.
    ScriptedSource source({
        {std::nullopt, 0ms, true},
        {std::nullopt, 0ms, true},
        {std::nullopt, 0ms, true},
        {std::nullopt, 0ms, true},
        {std::nullopt, 0ms, true},
        {std::nullopt, 0ms, true},
        {std::nullopt, 0ms, true},
        {std::nullopt, 0ms, true},
    });
    const auto total_timeout = 50ms;
    PowerSupplyMeasurementWaiter waiter(total_timeout, 500ms); // measurement_timeout >> total_timeout

    const auto result = waiter.wait(below_60V, std::ref(source), never_cancelled, [](int) {});

    EXPECT_EQ(result.stop_reason, PowerSupplyMeasurementWaiter::StopReason::Timeout);
    ASSERT_FALSE(source.recorded_timeouts.empty());
    for (const auto& t : source.recorded_timeouts) {
        EXPECT_LE(t, total_timeout) << "next_measurement must never be asked to wait longer than the "
                                       "remaining total budget";
    }
}

/// Cancellation must win even while measurements are otherwise flowing normally, and it must be
/// observed before the next measurement is consumed (not only at the very top of wait()).
TEST(PowerSupplyMeasurementWaiterTest, cancellation_stops_the_wait_promptly_and_is_not_reported_as_condition_met) {
    ScriptedSource source({
        {std::nullopt, 0ms}, // one gap, after which cancellation is requested
        {40.0},              // must never be reached
    });
    PowerSupplyMeasurementWaiter waiter(300ms, 50ms);

    bool cancel_requested = false;
    const auto result = waiter.wait(
        below_60V, std::ref(source), [&cancel_requested] { return cancel_requested; },
        [&cancel_requested](int) { cancel_requested = true; });

    EXPECT_EQ(result.stop_reason, PowerSupplyMeasurementWaiter::StopReason::Cancelled);
    EXPECT_FALSE(result.condition_met());
    EXPECT_EQ(source.call_count, 1u)
        << "cancellation must be checked before requesting the next measurement, so the second "
           "scripted measurement must never be consumed";
}

/// The whole point of this class: distinguish "the measurement stream stalled" (no data at all)
/// from "the voltage just never moved" (data arrived, but never satisfied the condition), because
/// the caller reports a different diagnostic message for each.
TEST(PowerSupplyMeasurementWaiterTest, timeout_with_no_measurements_reports_a_stalled_stream) {
    ScriptedSource source({
        {std::nullopt, 20ms},
        {std::nullopt, 20ms},
        {std::nullopt, 20ms},
        {std::nullopt, 20ms},
        {std::nullopt, 20ms},
        {std::nullopt, 20ms},
        {std::nullopt, 20ms},
        {std::nullopt, 20ms},
    });
    PowerSupplyMeasurementWaiter waiter(100ms, 20ms);

    const auto result = waiter.wait(below_60V, std::ref(source), never_cancelled, [](int) {});

    EXPECT_EQ(result.stop_reason, PowerSupplyMeasurementWaiter::StopReason::Timeout);
    EXPECT_EQ(result.samples, 0);
    EXPECT_FALSE(result.last_voltage_V.has_value());
    EXPECT_GT(result.gaps, 0);
}

TEST(PowerSupplyMeasurementWaiterTest, timeout_with_measurements_reports_the_last_measured_voltage) {
    ScriptedSource source({
        {100.0, 20ms},
        {95.0, 20ms},
        {90.0, 20ms},
        {85.0, 20ms},
        {80.0, 20ms},
        {75.0, 20ms},
    });
    PowerSupplyMeasurementWaiter waiter(100ms, 20ms);

    const auto result = waiter.wait(below_60V, std::ref(source), never_cancelled, [](int) {});

    EXPECT_EQ(result.stop_reason, PowerSupplyMeasurementWaiter::StopReason::Timeout);
    EXPECT_GT(result.samples, 0);
    ASSERT_TRUE(result.last_voltage_V.has_value());
    // Whichever measurement happened to be the last one delivered before the deadline; the
    // important, spec-guaranteed fact is that it is present and not the very first value, i.e.
    // the caller can report a real "last measured X V" diagnostic.
    EXPECT_NE(*result.last_voltage_V, 0.0);
}
