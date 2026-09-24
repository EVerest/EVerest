// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <limits>

#include "helper.hpp"

#include <iso15118/ev/d20/secc_clock.hpp>

using iso15118::ev::d20::SeccClock;

namespace {

using namespace std::chrono_literals;

// Hand-driven local clocks; neither advances unless a test moves it.
std::uint64_t fake_system_us = 0;
std::chrono::steady_clock::time_point fake_steady{};

std::uint64_t fake_system_now() {
    return fake_system_us;
}

std::chrono::steady_clock::time_point fake_steady_now() {
    return fake_steady;
}

constexpr std::uint64_t LOCAL_UTC_US = 1'700'000'000'000'000ULL;

SeccClock make_clock() {
    fake_system_us = LOCAL_UTC_US;
    fake_steady = std::chrono::steady_clock::time_point{} + 1h;
    return SeccClock{fake_system_now, fake_steady_now};
}

} // namespace

SCENARIO("ISO15118-20 EV SeccClock guesses local UTC until synchronized [V2G20-1535]") {
    auto clock = make_clock();
    REQUIRE(clock.now() == LOCAL_UTC_US);

    fake_system_us += 250;
    REQUIRE(clock.now() == LOCAL_UTC_US + 250);
    REQUIRE(clock.stamp() == LOCAL_UTC_US + 250);
}

SCENARIO("ISO15118-20 EV SeccClock follows the SessionSetupRes reference on the steady clock [V2G20-1536]") {
    auto clock = make_clock();
    clock.synchronize(SECC_REFERENCE_US);
    REQUIRE(clock.now() == SECC_REFERENCE_US);

    fake_steady += 1500us;
    REQUIRE(clock.now() == SECC_REFERENCE_US + 1500);

    GIVEN("a local wall-clock step") {
        fake_system_us += 3'600'000'000ULL;
        THEN("SECC time does not move") {
            REQUIRE(clock.now() == SECC_REFERENCE_US + 1500);
        }
    }

    GIVEN("a new reference") {
        clock.synchronize(SECC_REFERENCE_US * 7);
        THEN("the offset is taken afresh") {
            REQUIRE(clock.now() == SECC_REFERENCE_US * 7);
        }
    }
}

SCENARIO("ISO15118-20 EV SeccClock stamps strictly increase when the clock stands still [V2G20-1537]") {
    auto clock = make_clock();

    GIVEN("no reference yet") {
        REQUIRE(clock.stamp() == LOCAL_UTC_US);
        REQUIRE(clock.stamp() == LOCAL_UTC_US + 1);
        REQUIRE(clock.stamp() == LOCAL_UTC_US + 2);
    }

    GIVEN("a reference far below the SessionSetupReq guess") {
        REQUIRE(clock.stamp() == LOCAL_UTC_US);
        clock.synchronize(SECC_REFERENCE_US);

        THEN("the next stamp drops to SECC time [V2G20-1538] and passes the reference") {
            REQUIRE(clock.stamp() == SECC_REFERENCE_US + 1);
            REQUIRE(clock.stamp() == SECC_REFERENCE_US + 2);
        }

        THEN("stamps resume real time once the clock catches up") {
            fake_steady += 10us;
            REQUIRE(clock.stamp() == SECC_REFERENCE_US + 10);
            REQUIRE(clock.stamp() == SECC_REFERENCE_US + 11);
        }
    }
}

SCENARIO("ISO15118-20 EV SeccClock saturates at UINT64_MAX instead of wrapping") {
    constexpr auto MAX = std::numeric_limits<std::uint64_t>::max();
    auto clock = make_clock();
    clock.synchronize(MAX - 1);
    fake_steady += 10us;

    REQUIRE(clock.now() == MAX);
    REQUIRE(clock.stamp() == MAX);
    REQUIRE(clock.stamp() == MAX);
}

SCENARIO("ISO15118-20 EV SeccClock moves a Unix-epoch time into SECC time") {
    auto clock = make_clock();
    constexpr std::uint64_t CERTIFIED_US = 1'600'000'000'000'000ULL;

    GIVEN("no reference yet") {
        REQUIRE(clock.from_unix(CERTIFIED_US) == CERTIFIED_US);
    }

    GIVEN("an SECC epoch at power-on, far below local UTC") {
        clock.synchronize(SECC_REFERENCE_US);
        fake_steady += 20us;
        fake_system_us += 20;
        THEN("a time keeps its distance from now") {
            REQUIRE(clock.from_unix(LOCAL_UTC_US + 20 - 1'000'000) == SECC_REFERENCE_US + 20 - 1'000'000);
        }
        THEN("a time before the SECC epoch clamps at zero") {
            REQUIRE(clock.from_unix(CERTIFIED_US) == 0);
        }
    }

    GIVEN("SECC time ahead of local UTC") {
        clock.synchronize(LOCAL_UTC_US + SECC_REFERENCE_US);
        REQUIRE(clock.from_unix(CERTIFIED_US) == CERTIFIED_US + SECC_REFERENCE_US);
    }
}

SCENARIO("ISO15118-20 EV SeccClock resumes a paused clock without going back [V2G20-1537]") {
    auto paused = make_clock();
    paused.synchronize(SECC_REFERENCE_US);
    fake_steady += 100us;
    const auto last = paused.stamp();

    SeccClock resumed{fake_system_now, fake_steady_now};
    fake_steady += 100us;
    resumed.resume(paused.state());
    REQUIRE(resumed.stamp() == last + 100);

    WHEN("the resumed SessionSetupRes carries a reference below the stamps already sent") {
        resumed.synchronize(SECC_REFERENCE_US);
        THEN("the next stamp still passes the last one") {
            REQUIRE(resumed.stamp() == last + 101);
        }
    }
}
