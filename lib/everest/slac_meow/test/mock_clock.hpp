// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>

#include <everest/slac/time.hpp>

namespace everest::slac::test {

// Nothing here sleeps: a test advances time explicitly, so a 600 ms deadline costs a few
// microseconds and the result is identical on every run and every machine.
class MockClock {
public:
    TimePoint now() const {
        return m_now;
    }

    TimeSource source() {
        return [this] { return m_now; };
    }

    void advance(std::chrono::milliseconds by) {
        m_now += by;
    }

private:
    // an arbitrary non-zero origin, so a default-constructed Timer is not accidentally "now"
    TimePoint m_now{std::chrono::milliseconds(1'000'000)};
};

} // namespace everest::slac::test
