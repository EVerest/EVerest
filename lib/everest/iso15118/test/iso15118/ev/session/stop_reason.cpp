// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <optional>

#include <iso15118/ev/session/stop_reason.hpp>

using iso15118::ev::session::combine;
using iso15118::ev::session::is_pause;
using iso15118::ev::session::StopReason;

SCENARIO("EV stop reasons combine with any stop outranking any pause") {
    struct Row {
        StopReason ev;
        StopReason evse;
        StopReason combined;
    };
    const auto row = GENERATE(Row{StopReason::EvStop, StopReason::EvseTerminate, StopReason::EvseTerminate},
                              Row{StopReason::EvStop, StopReason::EvsePause, StopReason::EvStop},
                              Row{StopReason::EvPause, StopReason::EvseTerminate, StopReason::EvseTerminate},
                              Row{StopReason::EvPause, StopReason::EvsePause, StopReason::EvsePause});
    CAPTURE(row.ev, row.evse);

    REQUIRE(combine(row.ev, row.evse) == row.combined);
}

SCENARIO("EV stop reasons keep the one side that asked") {
    const auto reason =
        GENERATE(StopReason::EvStop, StopReason::EvPause, StopReason::EvseTerminate, StopReason::EvsePause);
    CAPTURE(reason);

    REQUIRE(combine(reason, std::nullopt) == reason);
    REQUIRE(combine(std::nullopt, reason) == reason);
}

SCENARIO("EV stop reasons terminate when neither side asked") {
    REQUIRE_FALSE(is_pause(combine(std::nullopt, std::nullopt)));
}

SCENARIO("A pause reason is a pause and a stop reason is not") {
    REQUIRE(is_pause(StopReason::EvPause));
    REQUIRE(is_pause(StopReason::EvsePause));
    REQUIRE_FALSE(is_pause(StopReason::EvStop));
    REQUIRE_FALSE(is_pause(StopReason::EvseTerminate));
}
