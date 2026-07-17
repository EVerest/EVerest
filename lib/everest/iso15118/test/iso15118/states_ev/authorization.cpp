// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <variant>

#include <iso15118/detail/d20/ev/state/authorization.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

SCENARIO("EVCC Authorization request/response handling (EIM ongoing loop)") {

    GIVEN("An EIM authorization request") {
        const auto req = d20::ev::state::authorization::create_request(dt::Authorization::EIM);
        THEN("EIM is selected with EIM authorization mode") {
            REQUIRE(req.selected_authorization_service == dt::Authorization::EIM);
            REQUIRE(std::holds_alternative<dt::EIM_ASReqAuthorizationMode>(req.authorization_mode));
        }
    }

    GIVEN("An ongoing response") {
        message_20::AuthorizationResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::Processing::Ongoing;

        const auto result = d20::ev::state::authorization::handle_response(res);
        THEN("The decision is to retry (resend unaltered)") {
            REQUIRE(result.action == d20::ev::state::authorization::Action::Retry);
        }
    }

    GIVEN("A finished response") {
        message_20::AuthorizationResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::Processing::Finished;

        const auto result = d20::ev::state::authorization::handle_response(res);
        THEN("The decision is done (proceed to ServiceDiscovery)") {
            REQUIRE(result.action == d20::ev::state::authorization::Action::Done);
        }
    }

    GIVEN("A failed response") {
        message_20::AuthorizationResponse res;
        res.response_code = dt::ResponseCode::FAILED;

        const auto result = d20::ev::state::authorization::handle_response(res);
        THEN("The decision is failed") {
            REQUIRE(result.action == d20::ev::state::authorization::Action::Failed);
        }
    }

    GIVEN("An authorization rejection via warning (EIM failure, processing finished)") {
        message_20::AuthorizationResponse res;
        res.response_code = dt::ResponseCode::WARNING_EIMAuthorizationFailure;
        res.evse_processing = dt::Processing::Finished;

        const auto result = d20::ev::state::authorization::handle_response(res);
        THEN("The decision is failed (a warning never authorizes)") {
            REQUIRE(result.action == d20::ev::state::authorization::Action::Failed);
        }
    }

    GIVEN("An authorization rejection via warning (invalid selection, processing finished)") {
        message_20::AuthorizationResponse res;
        res.response_code = dt::ResponseCode::WARNING_AuthorizationSelectionInvalid;
        res.evse_processing = dt::Processing::Finished;

        const auto result = d20::ev::state::authorization::handle_response(res);
        THEN("The decision is failed (a warning never authorizes)") {
            REQUIRE(result.action == d20::ev::state::authorization::Action::Failed);
        }
    }
}
