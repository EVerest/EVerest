// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "PowerSupplyMeasurementWaiter.hpp"

#include <algorithm>

#include <fmt/format.h>

namespace module {

PowerSupplyMeasurementWaiter::PowerSupplyMeasurementWaiter(std::chrono::milliseconds total_timeout,
                                                           std::chrono::milliseconds measurement_timeout) :
    total_timeout(total_timeout), measurement_timeout(measurement_timeout) {
}

PowerSupplyMeasurementWaiter::Result PowerSupplyMeasurementWaiter::wait(const Condition& condition,
                                                                        const MeasurementSource& next_measurement,
                                                                        const CancelledCheck& cancelled,
                                                                        const GapObserver& on_gap) const {
    Result result;

    const auto deadline = std::chrono::steady_clock::now() + total_timeout;

    while (true) {
        if (cancelled()) {
            result.stop_reason = StopReason::Cancelled;
            return result;
        }

        // Truncate to milliseconds before the comparison: a sub-millisecond remainder cannot be
        // waited on with millisecond resolution, so it counts as expired. Comparing the untruncated
        // remainder instead would busy-spin, calling next_measurement() with a timeout of 0 ms as
        // fast as the CPU allows until the last fraction of a millisecond elapsed.
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds::zero()) {
            result.stop_reason = StopReason::Timeout;
            return result;
        }

        // Never wait past the overall deadline, so that the budget is honoured even when the
        // measurement timeout is longer than what is left of it.
        const auto wait_for = std::min(measurement_timeout, remaining);

        if (const auto measurement = next_measurement(wait_for); measurement.has_value()) {
            result.samples++;
            result.last_voltage_V = measurement->voltage_V;
            if (condition(measurement.value())) {
                result.stop_reason = StopReason::ConditionMet;
                return result;
            }
        } else if (wait_for == measurement_timeout) {
            // A silent measurement stream is not a failure on its own: keep waiting for the
            // remaining budget so that a brief stall does not abort an otherwise healthy cable check.
            // A wait clamped to the end of the budget is shorter than the measurement timeout, so
            // an empty result there is not a gap.
            result.gaps++;
            on_gap(result.gaps);
        }
    }
}

std::string PowerSupplyMeasurementWaiter::describe_failure(const Result& result, const std::string& goal,
                                                           std::chrono::seconds total_timeout,
                                                           const std::string& cancel_reason) {
    if (result.stop_reason == StopReason::Cancelled) {
        return fmt::format("cancelled while waiting for the voltage to {}: {}", goal, cancel_reason);
    }
    if (not result.last_voltage_V.has_value()) {
        return fmt::format("no measurement received from the power supply at all within {} s, so it is unknown "
                           "whether the voltage did {}",
                           total_timeout.count(), goal);
    }
    return fmt::format("voltage did not {} within timeout, last measured {:.2f} V ({} measurements, {} gaps)", goal,
                       result.last_voltage_V.value(), result.samples, result.gaps);
}

} // namespace module
