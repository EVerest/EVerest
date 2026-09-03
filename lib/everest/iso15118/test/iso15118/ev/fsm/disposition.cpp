// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <iso15118/ev/d20/states.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>

using namespace iso15118;
using ev::d20::Disposition;
using ev::d20::disposition_violation;

namespace {
// Argument order mirrors disposition_violation: consumed, has_request, session_stopped, transitioned.
const char* check(Disposition d, bool consumed, bool has_request, bool session_stopped, bool transitioned) {
    return disposition_violation(d, consumed, has_request, session_stopped, transitioned);
}
} // namespace

SCENARIO("ISO15118-20 EV disposition_violation names every mismatch") {
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
