// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include <iso15118/d20/timeout.hpp>

namespace {

struct FakeClock {
    iso15118::TimePoint now{};

    void advance(int ms) {
        now += std::chrono::milliseconds(ms);
    }
};

} // namespace

SCENARIO("Timeouts Tests") {
    FakeClock clock;
    const auto now = [&clock] { return clock.now; };

    GIVEN("Basic Timeout") {
        auto timeout = iso15118::Timeout(20, clock.now);

        clock.advance(10);
        REQUIRE(timeout.is_reached(clock.now) == false);

        clock.advance(10);
        REQUIRE(timeout.is_reached(clock.now) == true);
    }

    GIVEN("Start Timeout and reset timeout after reaching it") {
        auto timeouts = iso15118::d20::Timeouts{now};

        timeouts.start_timeout(iso15118::d20::TimeoutType::CONTACTOR, 20);
        clock.advance(10);

        auto reached = timeouts.check();
        REQUIRE(reached.empty());

        clock.advance(10);
        reached = timeouts.check();
        REQUIRE(reached.size() == 1);
        REQUIRE(reached.at(0) == iso15118::d20::TimeoutType::CONTACTOR);

        timeouts.reset_timeout(iso15118::d20::TimeoutType::CONTACTOR);
        REQUIRE(timeouts.check().empty());
    }

    GIVEN("Start Timeout and stop timeout before reaching it") {
        auto timeouts = iso15118::d20::Timeouts{now};

        timeouts.start_timeout(iso15118::d20::TimeoutType::CONTACTOR, 20);
        clock.advance(10);
        REQUIRE(timeouts.check().empty());

        clock.advance(5);
        timeouts.stop_timeout(iso15118::d20::TimeoutType::CONTACTOR);

        REQUIRE(timeouts.check().empty());

        clock.advance(10);
        REQUIRE(timeouts.check().empty());
    }

    GIVEN("Parallel Timeouts") {

        auto timeouts = iso15118::d20::Timeouts{now};

        timeouts.start_timeout(iso15118::d20::TimeoutType::SEQUENCE, 30);
        timeouts.start_timeout(iso15118::d20::TimeoutType::PERFORMANCE, 10);
        timeouts.start_timeout(iso15118::d20::TimeoutType::CONTACTOR, 20);

        clock.advance(100);

        const auto reached = timeouts.check();

        REQUIRE(reached.size() == 3);
        REQUIRE(reached.at(0) == iso15118::d20::TimeoutType::PERFORMANCE);
        REQUIRE(reached.at(1) == iso15118::d20::TimeoutType::CONTACTOR);
        REQUIRE(reached.at(2) == iso15118::d20::TimeoutType::SEQUENCE);
    }

    GIVEN("Two timeouts started with the same duration") {
        auto timeouts = iso15118::d20::Timeouts{now};

        timeouts.start_timeout(iso15118::d20::TimeoutType::SEQUENCE, 10);
        timeouts.start_timeout(iso15118::d20::TimeoutType::CONTACTOR, 10);

        clock.advance(30);

        THEN("Both are reported, even when their deadlines coincide") {
            const auto reached = timeouts.check();
            REQUIRE(reached.size() == 2);
        }
    }
}
