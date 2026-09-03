// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/slac/timer.hpp>

namespace everest::lib::slac {

void timer::set_duration_ms(long long value) {
    set_duration(std::chrono::milliseconds(value));
}

void timer::reset(tp now) {
    reference = now;
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

} // namespace everest::lib::slac
