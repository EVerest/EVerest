// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <functional>
#include <optional>

#include <generated/types/power_supply_DC.hpp>

namespace module {

/// \brief Waits for the power supply measurements to satisfy a condition during the DC cable check.
///
/// During the cable check EvseManager has to wait for the output voltage to reach the isolation test
/// voltage, and to drop below the safe voltage again. Both are decided from the measurement stream
/// published by the power supply.
///
/// That stream is not necessarily fast or regular: depending on the hardware it may originate from a
/// power meter that is polled over the network. A gap in it therefore says nothing about the voltage,
/// and must not be confused with the voltage failing to move. This class keeps the two apart, and
/// keeps waiting for the whole time budget instead of giving up on the first gap.
///
/// The class performs no I/O and has no side effects: reading measurements, cancellation and logging
/// are all injected by the caller.
class PowerSupplyMeasurementWaiter {
public:
    /// \brief Why waiting stopped.
    enum class StopReason {
        ConditionMet, ///< The condition was satisfied by a received measurement.
        Timeout,      ///< The total time budget expired.
        Cancelled,    ///< The cancellation check asked to stop.
    };

    /// \brief Outcome of a wait.
    struct Result {
        StopReason stop_reason{StopReason::Timeout};
        /// Number of measurements received during the wait.
        int samples{0};
        /// Number of times no measurement arrived within the per measurement timeout.
        int gaps{0};
        /// The last measured voltage, or std::nullopt if no measurement was received at all.
        std::optional<double> last_voltage_V{std::nullopt};

        [[nodiscard]] bool condition_met() const {
            return stop_reason == StopReason::ConditionMet;
        }
    };

    /// \brief Returns the next measurement, or std::nullopt if none arrived within \p timeout.
    using MeasurementSource =
        std::function<std::optional<types::power_supply_DC::VoltageCurrent>(std::chrono::milliseconds timeout)>;
    /// \brief Returns true when waiting should be abandoned.
    using CancelledCheck = std::function<bool()>;
    /// \brief Condition that a measurement has to satisfy.
    using Condition = std::function<bool(const types::power_supply_DC::VoltageCurrent&)>;
    /// \brief Called once for every gap in the measurement stream, with the number of gaps so far.
    using GapObserver = std::function<void(int gaps_so_far)>;

    /// \param total_timeout overall budget for the condition to be satisfied. Note that for the cable
    ///        check this budget sits inside the CableCheck timeout of the EV, so it must not be raised
    ///        carelessly.
    /// \param measurement_timeout how long to wait for a single measurement before counting a gap
    PowerSupplyMeasurementWaiter(std::chrono::milliseconds total_timeout,
                                 std::chrono::milliseconds measurement_timeout);

    /// \brief Waits until \p condition is satisfied, the budget expires, or waiting is cancelled.
    ///
    /// A gap in the measurement stream is not a failure by itself: waiting continues for the
    /// remaining budget, so a measurement source that goes briefly silent does not abort an otherwise
    /// healthy cable check. Every gap is reported to \p on_gap and counted in the result, so that the
    /// caller can report a stalled measurement stream as something other than a voltage that did not
    /// move.
    Result wait(const Condition& condition, const MeasurementSource& next_measurement, const CancelledCheck& cancelled,
                const GapObserver& on_gap) const;

private:
    std::chrono::milliseconds total_timeout;
    std::chrono::milliseconds measurement_timeout;
};

} // namespace module
