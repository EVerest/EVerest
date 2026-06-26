// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/variant.hpp>

#include "helper.hpp"

using namespace iso15118;

SCENARIO("EV d20 Context encodes a request that round-trips back to the same message") {

    GIVEN("An EV d20 Context with an empty MessageExchange") {

        const ev::feedback::Callbacks callbacks{};
        FsmStateHelper helper{callbacks};

        auto& ctx = helper.get_context();
        auto& mx = helper.get_message_exchange();

        WHEN("The Context responds with a SessionSetupRequest") {

            message_20::SessionSetupRequest req{message_20::Header{}, "EVTESTID01"};
            ctx.respond(req);

            THEN("The MessageExchange holds a pending request") {
                REQUIRE(mx.has_request());

                const auto taken = mx.take_request();
                REQUIRE(taken.has_value());
                const auto& [bytes, type] = *taken;

                AND_THEN("The payload type is Part20Main") {
                    REQUIRE(type == io::v2gtp::PayloadType::Part20Main);
                }

                AND_THEN("The EXI bytes decode back to the same SessionSetupRequest") {
                    message_20::Variant variant(type, io::StreamInputView{bytes.data(), bytes.size()});

                    REQUIRE(variant.get_type() == message_20::Type::SessionSetupReq);

                    const auto* decoded = variant.get_if<message_20::SessionSetupRequest>();
                    REQUIRE(decoded != nullptr);
                    REQUIRE(decoded->evccid == "EVTESTID01");
                }
            }
        }
    }
}
