// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/ev/state/contract_authentication.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;
using namespace din::ev::state::contract_authentication;

SCENARIO("EVCC DIN ContractAuthentication request/response handling") {
    GIVEN("An EIM request") {
        const auto req = create_request();
        THEN("No id or challenge is set") {
            REQUIRE(not req.id.has_value());
            REQUIRE(not req.gen_challenge.has_value());
        }
    }

    GIVEN("An Ongoing response") {
        message_din::ContractAuthenticationResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Ongoing;
        THEN("The action is Retry") {
            REQUIRE(handle_response(res).action == Action::Retry);
        }
    }

    GIVEN("A Finished response") {
        message_din::ContractAuthenticationResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        THEN("The action is Done") {
            REQUIRE(handle_response(res).action == Action::Done);
        }
    }

    GIVEN("A failed response") {
        message_din::ContractAuthenticationResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        res.evse_processing = dt::EvseProcessing::Finished;
        THEN("The action is Failed") {
            REQUIRE(handle_response(res).action == Action::Failed);
        }
    }
}
