// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_din/contract_authentication.hpp>
#include <iso15118/message_din/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_din;

SCENARIO("Se/Deserialize DIN contract authentication messages") {

    const datatypes::SessionId session_id = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};

    GIVEN("Serialize and deserialize contract_authentication_req (EIM, no Id/GenChallenge)") {
        ContractAuthenticationRequest req;
        req.header.session_id = session_id;

        const auto bytes = serialize_helper(req);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::ContractAuthenticationReq);
            const auto& msg = variant.get<ContractAuthenticationRequest>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.id.has_value() == false);
            REQUIRE(msg.gen_challenge.has_value() == false);
        }
    }

    GIVEN("Serialize and deserialize contract_authentication_req with Id and GenChallenge") {
        ContractAuthenticationRequest req;
        req.header.session_id = session_id;
        req.id = "id1";
        req.gen_challenge = "foo";

        const auto bytes = serialize_helper(req);

        THEN("Both optional elements survive the round-trip") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::ContractAuthenticationReq);
            const auto& msg = variant.get<ContractAuthenticationRequest>();
            REQUIRE(msg.id == "id1");
            REQUIRE(msg.gen_challenge == "foo");
        }
    }

    GIVEN("Serialize and deserialize contract_authentication_res") {
        ContractAuthenticationResponse res;
        res.header.session_id = session_id;
        res.response_code = datatypes::ResponseCode::OK;
        res.evse_processing = datatypes::EvseProcessing::Finished;

        const auto bytes = serialize_helper(res);

        THEN("The encoded response converts back field for field") {
            const auto doc = decode_helper(bytes);
            REQUIRE(doc.V2G_Message.Body.ContractAuthenticationRes_isUsed);
            const auto msg =
                to_response<ContractAuthenticationResponse>(doc, doc.V2G_Message.Body.ContractAuthenticationRes);
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.response_code == datatypes::ResponseCode::OK);
            REQUIRE(msg.evse_processing == datatypes::EvseProcessing::Finished);
        }
    }
}
