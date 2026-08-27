// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/ev/state/authorization_setup.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

SCENARIO("EVCC AuthorizationSetup response handling") {

    GIVEN("An EIM-only response") {
        message_20::AuthorizationSetupResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.certificate_installation_service = false;
        res.authorization_services = {dt::Authorization::EIM};

        const auto result = d20::ev::state::authorization_setup::handle_response(res);
        THEN("EIM is reported as offered") {
            REQUIRE(result.valid == true);
            REQUIRE(result.certificate_installation_service == false);
            REQUIRE(result.offered_auth_services.size() == 1);
            REQUIRE(result.offered_auth_services[0] == dt::Authorization::EIM);
        }
    }

    GIVEN("A failed response") {
        message_20::AuthorizationSetupResponse res;
        res.response_code = dt::ResponseCode::FAILED_UnknownSession;

        const auto result = d20::ev::state::authorization_setup::handle_response(res);
        THEN("The result is invalid") {
            REQUIRE(result.valid == false);
        }
    }
}
