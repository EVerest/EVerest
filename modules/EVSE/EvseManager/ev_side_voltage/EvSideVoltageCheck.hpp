// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <optional>

namespace module {

/// \brief Judges the DC voltage on the EV side of the open EVSE output contactors before the cable check
/// (IEC 61851-23:2023 Table CC.8).
///
/// t2 - t3: ChargeParameterDiscoveryRes may only report Finished once the absolute voltage is below 60 V.
/// From t3: a voltage below -60 V for 400 consecutive ms is an error shutdown.
///
/// The class performs no I/O and reads no clock: samples and the current time are passed in.
class EvSideVoltageCheck {
public:
    using Clock = std::chrono::steady_clock;

    enum class State {
        Safe,     ///< The last sample is recent and its absolute value is below 60 V.
        Unsafe,   ///< The last sample is recent and its absolute value is 60 V or more.
        Stale,    ///< The source published before but the last sample is older than Timing::max_sample_age.
        NoSample, ///< The source never published, so it cannot be used for the check.
    };

    struct Timing {
        std::chrono::milliseconds max_sample_age{3000};
        std::chrono::milliseconds reverse_voltage_duration{400};
    };

    static constexpr double SAFE_VOLTAGE_V{60.};

    explicit EvSideVoltageCheck(Timing timing);
    EvSideVoltageCheck();

    void update(double voltage_V, Clock::time_point now);

    State state(Clock::time_point now) const;
    std::optional<double> last_voltage_V() const;

    /// \brief Forgets the reverse voltage seen so far; only samples from here on count.
    void restart_reverse_voltage();

    /// \brief Every sample since restart_reverse_voltage() or since the last sample at or above -60 V was below
    /// -60 V, and the first and the last of them are at least Timing::reverse_voltage_duration apart.
    bool reverse_voltage() const;

private:
    struct Sample {
        double voltage_V;
        Clock::time_point time;
    };

    Timing m_timing;
    std::optional<Sample> m_last;
    std::optional<Clock::time_point> m_reverse_since;
};

} // namespace module
