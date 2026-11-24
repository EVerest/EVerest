// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <sunspec/limit_policy.hpp>

#include <algorithm>
#include <cmath>

namespace sunspec_solar {

LimitPolicy::LimitPolicy(const PolicyConfig& config) : config(config) {
}

void LimitPolicy::on_production(double production_W, Clock::time_point now) {
    last_production_W = production_W;
    last_production_time = now;
}

double LimitPolicy::current_limit_W(Clock::time_point now) const {
    if (!last_production_W.has_value() || now - last_production_time > config.stale_timeout) {
        return config.fallback_power_W;
    }
    return std::max(0.0, *last_production_W - config.reserve_W);
}

std::optional<double> LimitPolicy::limit_to_send(Clock::time_point now) {
    const auto limit = current_limit_W(now);
    const auto rounded = std::lround(limit);
    if (last_sent_W.has_value() && *last_sent_W == rounded && now - last_sent_time < config.resend_interval) {
        return std::nullopt;
    }
    last_sent_W = rounded;
    last_sent_time = now;
    return limit;
}

} // namespace sunspec_solar
