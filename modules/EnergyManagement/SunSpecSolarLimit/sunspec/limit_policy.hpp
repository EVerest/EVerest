// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef SUNSPEC_SOLAR_LIMIT_LIMIT_POLICY_HPP
#define SUNSPEC_SOLAR_LIMIT_LIMIT_POLICY_HPP

#include <chrono>
#include <optional>

namespace sunspec_solar {

struct PolicyConfig {
    double reserve_W{0.0};
    double fallback_power_W{0.0};
    std::chrono::milliseconds stale_timeout{std::chrono::seconds(30)};
    std::chrono::milliseconds resend_interval{std::chrono::seconds(10)};
};

// Turns production readings into the charging power limit. Until the first
// valid reading, and once readings are older than the stale timeout, the
// fallback power applies. Not thread-safe.
class LimitPolicy {
public:
    using Clock = std::chrono::steady_clock;

    explicit LimitPolicy(const PolicyConfig& config);

    void on_production(double production_W, Clock::time_point now);

    double current_limit_W(Clock::time_point now) const;

    // The limit to send now: when it differs from the last one sent (to the
    // watt) or the resend interval has passed. std::nullopt otherwise.
    std::optional<double> limit_to_send(Clock::time_point now);

private:
    PolicyConfig config;
    std::optional<double> last_production_W;
    Clock::time_point last_production_time;
    std::optional<long> last_sent_W;
    Clock::time_point last_sent_time;
};

} // namespace sunspec_solar

#endif // SUNSPEC_SOLAR_LIMIT_LIMIT_POLICY_HPP
