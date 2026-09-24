// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d20/secc_clock.hpp>

#include <algorithm>
#include <limits>

namespace iso15118::ev::d20 {

namespace {
using std::chrono::duration_cast;
using std::chrono::microseconds;

std::uint64_t add_saturated(std::uint64_t a, std::uint64_t b) {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    return a > max - b ? max : a + b;
}
} // namespace

std::uint64_t SeccClock::system_now_us() {
    return static_cast<std::uint64_t>(
        duration_cast<microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

std::uint64_t SeccClock::now() const {
    if (not state_.reference.has_value()) {
        return system_now_();
    }
    const auto elapsed = duration_cast<microseconds>(steady_now_() - state_.reference->taken_at).count();
    return add_saturated(state_.reference->secc_time, static_cast<std::uint64_t>(elapsed));
}

void SeccClock::synchronize(std::uint64_t secc_time) {
    const bool resynchronizing = state_.reference.has_value() and state_.last_stamp.has_value();
    state_.reference = Reference{secc_time, steady_now_()};
    state_.last_stamp = resynchronizing ? std::max(*state_.last_stamp, secc_time) : secc_time;
}

std::uint64_t SeccClock::stamp() {
    auto next = now();
    if (state_.last_stamp.has_value() and next <= *state_.last_stamp) {
        next = add_saturated(*state_.last_stamp, 1);
    }
    state_.last_stamp = next;
    return next;
}

std::uint64_t SeccClock::from_unix(std::uint64_t unix_time) const {
    if (not state_.reference.has_value()) {
        return unix_time;
    }
    const auto secc = now();
    const auto local = system_now_();
    if (secc >= local) {
        return add_saturated(unix_time, secc - local);
    }
    const auto behind = local - secc;
    return unix_time > behind ? unix_time - behind : 0;
}

} // namespace iso15118::ev::d20
