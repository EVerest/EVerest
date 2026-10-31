// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <limits>

#include <iso15118/ev/config/config.hpp>
#include <iso15118/ev/session/stop_reason.hpp>

namespace iso15118::ev::welding_detection {

enum class Outcome {
    Continue,
    Safe,
    Failed
};

inline Outcome should_finish(float voltage_v, int cycles, std::chrono::steady_clock::duration elapsed,
                             const config::WeldingDetectionConfig& config) {
    if (voltage_v < config.safe_voltage_v) {
        return Outcome::Safe;
    }
    return cycles >= config.max_cycles or elapsed >= config.timeout ? Outcome::Failed : Outcome::Continue;
}

class Progress {
public:
    Outcome record(float voltage_v, const config::WeldingDetectionConfig& config) {
        ++m_cycles;
        m_last_voltage_v = voltage_v;
        const auto outcome = should_finish(voltage_v, m_cycles, std::chrono::steady_clock::now() - m_started, config);
        m_concluded = outcome != Outcome::Continue;
        return outcome;
    }

    // Safe or failed, the check is over.
    bool concluded() const {
        return m_concluded;
    }

    // A failed welding exchange terminates the session even when a pause was requested.
    template <typename Context> session::StopReason report_failure(Context& ctx) {
        m_concluded = true;
        ctx.feedback.welding_detection_failed(m_last_voltage_v, m_cycles);
        return session::StopReason::EvStop;
    }

    // RESPONSE_TIMEOUT: an unanswered request fails a check still running, and the session stops.
    template <typename Result, typename Context> Result response_timeout(Context& ctx) {
        if (not m_concluded) {
            report_failure(ctx);
        }
        ctx.stop_session();
        return Result::stopping();
    }

private:
    std::chrono::steady_clock::time_point m_started{std::chrono::steady_clock::now()};
    float m_last_voltage_v{std::numeric_limits<float>::quiet_NaN()};
    int m_cycles{0};
    bool m_concluded{false};
};

} // namespace iso15118::ev::welding_detection
