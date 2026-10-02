// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/ev/d20/engine.hpp>

using namespace iso15118;
using io::v2gtp::PayloadType;

// The engine stages a response only for a payload type it can decode; anything else is dropped so
// the frame never reaches the states as an unexpected response [V2G20-800].
SCENARIO("Which V2GTP payload types the ISO 15118-20 engine accepts") {

    GIVEN("Every payload type ISO 15118-20 spreads its messages over") {
        THEN("All of them are decodable") {
            REQUIRE(ev::d20::decodable_payload_type(PayloadType::SAP));
            REQUIRE(ev::d20::decodable_payload_type(PayloadType::Part20Main));
            REQUIRE(ev::d20::decodable_payload_type(PayloadType::Part20AC));
            REQUIRE(ev::d20::decodable_payload_type(PayloadType::Part20DC));
            REQUIRE(ev::d20::decodable_payload_type(PayloadType::Part20DerIec));
            REQUIRE(ev::d20::decodable_payload_type(PayloadType::Part20DerSae));
        }
    }

    GIVEN("Values the V2GTP header can carry that are not -20 payload types") {
        THEN("None of them is decodable") {
            // SdpPacket::get_payload_type casts the wire's 16 bits without validating them, so any
            // value can arrive here, including the SDP discovery types and plain rubbish.
            for (const uint16_t raw : {0x0000, 0x9000, 0x8005, 0x8009, 0x8012, 0xFFFF, 0x9001, 0x9002}) {
                REQUIRE_FALSE(ev::d20::decodable_payload_type(static_cast<PayloadType>(raw)));
            }
        }
    }
}
