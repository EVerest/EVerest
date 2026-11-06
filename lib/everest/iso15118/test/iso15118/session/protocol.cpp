// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include <iso15118/session/protocol.hpp>

using namespace iso15118;

SCENARIO("Protocol generation names") {

    GIVEN("Every ProtocolId") {
        THEN("It has a stable name") {
            REQUIRE(protocol_id_to_string(ProtocolId::DIN70121) == "DIN70121");
            REQUIRE(protocol_id_to_string(ProtocolId::ISO15118_2) == "ISO15118-2-2013");
            REQUIRE(protocol_id_to_string(ProtocolId::ISO15118_20) == "ISO15118-20");
        }
    }
}
