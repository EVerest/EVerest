// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <functional>

#include <everest/slac/timer.hpp>

namespace everest::lib::slac::test {

// Deterministic time for the state machine tests: install source() as ContextCallbacks::now and
// advance() instead of sleeping. Starts one second after the epoch so a default-constructed, never
// armed timer reads as expired, exactly as it does against the real clock.
class MockClock {
public:
    timer::tp now() const {
        return m_now;
    }
    std::function<timer::tp()> source() {
        return [this] { return m_now; };
    }
    void advance(std::chrono::microseconds duration) {
        m_now += duration;
    }
    void advance_ms(long long ms) {
        advance(std::chrono::milliseconds(ms));
    }

private:
    timer::tp m_now{timer::tp{} + std::chrono::seconds(1)};
};

} // namespace everest::lib::slac::test
