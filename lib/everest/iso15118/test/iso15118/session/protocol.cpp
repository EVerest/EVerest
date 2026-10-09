// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <string>

#include <iso15118/session/protocol.hpp>

using namespace iso15118;

SCENARIO("Protocol generation names") {

    GIVEN("Every ProtocolId") {
        THEN("It is named the way EvseV2G names it") {
            REQUIRE(std::string(protocol_id_to_string(ProtocolId::DIN70121)) == "DIN70121");
            REQUIRE(std::string(protocol_id_to_string(ProtocolId::ISO15118_2)) == "ISO15118-2-2013");
            REQUIRE(std::string(protocol_id_to_string(ProtocolId::ISO15118_20)) == "ISO15118-20");
        }
    }
}
