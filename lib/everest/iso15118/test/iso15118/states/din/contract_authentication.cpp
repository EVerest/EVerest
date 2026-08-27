// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/state/contract_authentication.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;

SCENARIO("DIN SECC ContractAuthentication state handling") {
    const dt::SessionId session{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("Not yet authorized") {
        const auto res = din::state::handle_request(false, session);
        THEN("EVSEProcessing is Ongoing") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EvseProcessing::Ongoing);
        }
    }

    GIVEN("Authorized") {
        const auto res = din::state::handle_request(true, session);
        THEN("EVSEProcessing is Finished") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EvseProcessing::Finished);
        }
    }

    GIVEN("Authorization rejected") {
        const auto res = din::state::handle_request(false, session, true);
        THEN("FAILED and Finished (not Ongoing forever)") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(res.evse_processing == dt::EvseProcessing::Finished);
        }
    }
}
