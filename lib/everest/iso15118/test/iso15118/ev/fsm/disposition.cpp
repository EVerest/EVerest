// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <iso15118/ev/fsm_base.hpp>

using iso15118::ev::Disposition;
using iso15118::ev::disposition_violation;

namespace {
// Argument order mirrors disposition_violation: consumed, has_request, session_stopped, transitioned.
const char* check(Disposition d, bool consumed, bool has_request, bool session_stopped, bool transitioned,
                  bool handover = false) {
    return disposition_violation(d, consumed, has_request, session_stopped, transitioned, handover);
}
} // namespace

SCENARIO("EV disposition_violation names every mismatch") {
    GIVEN("a state that declared Awaiting") {
        THEN("a pending request satisfies it") {
            REQUIRE(check(Disposition::Awaiting, true, true, false, false) == nullptr);
        }
        THEN("no pending request is the named violation") {
            const auto* violation = check(Disposition::Awaiting, true, false, false, false);
            REQUIRE(violation != nullptr);
            REQUIRE(std::strcmp(violation, "Awaiting without a pending request") == 0);
        }
    }

    GIVEN("a state that declared Stopping") {
        THEN("a stopped session satisfies it") {
            REQUIRE(check(Disposition::Stopping, true, false, true, false) == nullptr);
        }
        THEN("a live session is the named violation") {
            const auto* violation = check(Disposition::Stopping, true, false, false, false);
            REQUIRE(violation != nullptr);
            REQUIRE(std::strcmp(violation, "Stopping without stop_session()") == 0);
        }
    }

    GIVEN("a state that declared Transitioning") {
        THEN("a new state satisfies it") {
            REQUIRE(check(Disposition::Transitioning, true, true, false, true) == nullptr);
        }
        THEN("staying put is the named violation") {
            const auto* violation = check(Disposition::Transitioning, true, true, false, false);
            REQUIRE(violation != nullptr);
            REQUIRE(std::strcmp(violation, "Transitioning without a new state") == 0);
        }
    }

    GIVEN("a state that declared Handover") {
        THEN("a negotiated other protocol satisfies it") {
            REQUIRE(check(Disposition::Handover, true, false, false, false, true) == nullptr);
        }
        THEN("no negotiated other protocol is the named violation") {
            const auto* violation = check(Disposition::Handover, true, false, false, false, false);
            REQUIRE(violation != nullptr);
            REQUIRE(std::strcmp(violation, "Handover without a negotiated other protocol") == 0);
        }
    }

    GIVEN("a state that declared Ignored") {
        THEN("a control event that consumed nothing satisfies it") {
            REQUIRE(check(Disposition::Ignored, false, false, false, false) == nullptr);
        }
        THEN("a consumed response is the named violation") {
            const auto* violation = check(Disposition::Ignored, true, false, false, false);
            REQUIRE(violation != nullptr);
            REQUIRE(std::strcmp(violation, "Ignored but a response was consumed") == 0);
        }
    }
}

SCENARIO("BasicResult derives unhandled from the disposition it declares") {
    struct Dummy {};
    using Result = iso15118::ev::BasicResult<Dummy>;

    THEN("only Ignored leaves the event unhandled") {
        REQUIRE(Result::ignored().unhandled == true);
        REQUIRE(Result::awaiting().unhandled == false);
        REQUIRE(Result::stopping().unhandled == false);
        REQUIRE(Result::handover().unhandled == false);
        REQUIRE(Result{std::make_unique<Dummy>()}.unhandled == false);
    }

    THEN("each factory carries its own disposition") {
        REQUIRE(Result::ignored().output == Disposition::Ignored);
        REQUIRE(Result::awaiting().output == Disposition::Awaiting);
        REQUIRE(Result::stopping().output == Disposition::Stopping);
        REQUIRE(Result::handover().output == Disposition::Handover);
        REQUIRE(Result{std::make_unique<Dummy>()}.output == Disposition::Transitioning);
    }
}
