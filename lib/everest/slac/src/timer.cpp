// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/timer.hpp>

#include <algorithm>

namespace everest::lib::slac {

void timer::set_duration_ms(long long value) {
    set_duration(std::chrono::milliseconds(value));
}

void timer::reset(tp now) {
    reference = now;
}

void timer::expire_at(tp deadline) {
    reference = deadline;
    duration = tick{0};
}

timer::tp timer::deadline() const {
    return reference + duration;
}

bool timer::expired(tp now) const {
    return now > deadline();
}

timer::tick timer::remaining(tp now) const {
    return std::chrono::duration_cast<tick>(deadline() - now);
}

earliest_deadline::earliest_deadline(timer::tp at) : now(at) {
}

void earliest_deadline::offer(timer const& t) {
    if (not t.expired(now)) {
        earliest = std::min(earliest.value_or(t.deadline()), t.deadline());
    }
}

std::optional<timer::tick> earliest_deadline::wait() const {
    if (not earliest) {
        return std::nullopt;
    }
    return std::chrono::floor<timer::tick>(*earliest - now) + timer::tick{1};
}

} // namespace everest::lib::slac
