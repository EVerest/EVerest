// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "PowerSupplyMeasurementWaiter.hpp"

#include <algorithm>

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
        } else {
            // A silent measurement stream is not a failure on its own: keep waiting for the
            // remaining budget so that a brief stall does not abort an otherwise healthy cable check.
            result.gaps++;
            on_gap(result.gaps);
        }
    }
}

} // namespace module
