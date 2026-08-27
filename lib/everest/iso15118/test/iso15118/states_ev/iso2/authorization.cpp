// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/ev/state/authorization.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;
using Action = d2::ev::state::authorization::Action;

SCENARIO("EVCC ISO-2 Authorization request/response handling") {

    GIVEN("An EIM request") {
        const auto req = d2::ev::state::authorization::create_request();
        THEN("It carries neither id nor gen_challenge") {
            REQUIRE(not req.id.has_value());
            REQUIRE(not req.gen_challenge.has_value());
        }
    }

    GIVEN("An Ongoing response") {
        message_2::AuthorizationResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EVSEProcessing::Ongoing;
        THEN("The action is Retry") {
            REQUIRE(d2::ev::state::authorization::handle_response(res).action == Action::Retry);
        }
    }

    GIVEN("A Finished response") {
        message_2::AuthorizationResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EVSEProcessing::Finished;
        THEN("The action is Done") {
            REQUIRE(d2::ev::state::authorization::handle_response(res).action == Action::Done);
        }
    }

    GIVEN("A failed response") {
        message_2::AuthorizationResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        res.evse_processing = dt::EVSEProcessing::Finished;
        THEN("The action is Failed") {
            REQUIRE(d2::ev::state::authorization::handle_response(res).action == Action::Failed);
        }
    }
}
