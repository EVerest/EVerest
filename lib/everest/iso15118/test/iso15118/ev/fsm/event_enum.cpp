// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/ev/d20/states.hpp>

TEST_CASE("ISO15118-20 EV Event enum exposes TIMEOUT") {
    using iso15118::ev::d20::Event;
    REQUIRE(static_cast<int>(Event::TIMEOUT) != static_cast<int>(Event::FAILED));
    REQUIRE(static_cast<int>(Event::TIMEOUT) != static_cast<int>(Event::CONTROL_MESSAGE));
}
