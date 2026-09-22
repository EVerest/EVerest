// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string_view>

#include <iso15118/ev/session/fsm.hpp>

using iso15118::ev::session::Disposition;
using iso15118::ev::session::disposition_violation;

namespace {
// Argument order mirrors disposition_violation: consumed, has_request, session_stopped, transitioned.
std::optional<std::string_view> check(Disposition d, bool consumed, bool has_request, bool session_stopped,
                                      bool transitioned, bool handover = false, bool ongoing_wait = false) {
    return disposition_violation(d, consumed, has_request, session_stopped, transitioned, handover, ongoing_wait);
}
} // namespace

SCENARIO("EV disposition_violation names every mismatch") {
    GIVEN("a state that declared Awaiting") {
        THEN("a pending request satisfies it") {
            REQUIRE(check(Disposition::Awaiting, true, true, false, false) == std::nullopt);
        }
        THEN("no pending request is the named violation") {
            REQUIRE(check(Disposition::Awaiting, true, false, false, false) == "Awaiting without a pending request");
        }
    }

    GIVEN("a state that declared Stopping") {
        THEN("a stopped session satisfies it") {
            REQUIRE(check(Disposition::Stopping, true, false, true, false) == std::nullopt);
        }
        THEN("a live session is the named violation") {
            REQUIRE(check(Disposition::Stopping, true, false, false, false) == "Stopping without stop_session()");
        }
    }

    GIVEN("a state that declared Transitioning") {
        THEN("a new state with its first request pending satisfies it") {
            REQUIRE(check(Disposition::Transitioning, true, true, false, true) == std::nullopt);
        }
        THEN("staying put is the named violation") {
            REQUIRE(check(Disposition::Transitioning, true, true, false, false) == "Transitioning without a new state");
        }
        THEN("a new state with nothing pending is the named violation") {
            REQUIRE(check(Disposition::Transitioning, true, false, false, true) ==
                    "Transitioning without a pending request");
        }
        THEN("a control event that transitions with nothing pending is the named violation too") {
            REQUIRE(check(Disposition::Transitioning, false, false, false, true) ==
                    "Transitioning without a pending request");
        }
    }

    GIVEN("a state that declared Waiting") {
        THEN("a new state bounded by an ongoing timeout with nothing pending satisfies it") {
            REQUIRE(check(Disposition::Waiting, true, false, false, true, false, true) == std::nullopt);
        }
        THEN("staying put is the named violation") {
            REQUIRE(check(Disposition::Waiting, true, false, false, false, false, true) ==
                    "Waiting without a new state");
        }
        THEN("a pending request is the named violation") {
            REQUIRE(check(Disposition::Waiting, true, true, false, true, false, true) ==
                    "Waiting with a pending request");
        }
        THEN("a successor without an ongoing timeout is the named violation") {
            REQUIRE(check(Disposition::Waiting, true, false, false, true, false, false) ==
                    "Waiting without an ongoing timeout");
        }
    }

    GIVEN("a state that declared Handover") {
        THEN("a negotiated other protocol satisfies it") {
            REQUIRE(check(Disposition::Handover, true, false, false, false, true) == std::nullopt);
        }
        THEN("no negotiated other protocol is the named violation") {
            REQUIRE(check(Disposition::Handover, true, false, false, false, false) ==
                    "Handover without a negotiated other protocol");
        }
    }

    GIVEN("a state that declared Ignored") {
        THEN("a control event that consumed nothing satisfies it") {
            REQUIRE(check(Disposition::Ignored, false, false, false, false) == std::nullopt);
        }
        THEN("a consumed response is the named violation") {
            REQUIRE(check(Disposition::Ignored, true, false, false, false) == "Ignored but a response was consumed");
        }
    }
}

SCENARIO("BasicResult derives unhandled from the disposition it declares") {
    struct Dummy {};
    using Result = iso15118::ev::session::BasicResult<Dummy>;

    THEN("only Ignored leaves the event unhandled") {
        REQUIRE(Result::ignored().unhandled == true);
        REQUIRE(Result::awaiting().unhandled == false);
        REQUIRE(Result::stopping().unhandled == false);
        REQUIRE(Result::handover().unhandled == false);
        REQUIRE(Result{std::make_unique<Dummy>()}.unhandled == false);
        REQUIRE(Result::waiting(std::make_unique<Dummy>()).unhandled == false);
    }

    THEN("each factory carries its own disposition") {
        REQUIRE(Result::ignored().output == Disposition::Ignored);
        REQUIRE(Result::awaiting().output == Disposition::Awaiting);
        REQUIRE(Result::stopping().output == Disposition::Stopping);
        REQUIRE(Result::handover().output == Disposition::Handover);
        REQUIRE(Result{std::make_unique<Dummy>()}.output == Disposition::Transitioning);
        REQUIRE(Result::waiting(std::make_unique<Dummy>()).output == Disposition::Waiting);
    }

    THEN("waiting carries its successor") {
        REQUIRE(Result::waiting(std::make_unique<Dummy>()).new_state != nullptr);
    }
}
