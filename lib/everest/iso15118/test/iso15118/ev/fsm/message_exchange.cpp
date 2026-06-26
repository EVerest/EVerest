// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>

#include <iso15118/ev/d20/context.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/variant.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 EV MessageExchange serializes requests") {

    ev::d20::MessageExchange msg_exch{};

    GIVEN("A SessionSetupRequest set as the pending request") {
        message_20::SessionSetupRequest request{};
        request.header.session_id = {0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02};
        request.evccid = "EVEREST_EV";

        msg_exch.set_request(request);

        THEN("has_request is true before taking it") {
            REQUIRE(msg_exch.has_request() == true);
        }

        WHEN("the request is taken") {
            const auto taken = msg_exch.take_request();

            THEN("it yields a non-empty Part20Main payload and clears the pending flag") {
                REQUIRE(taken.has_value());
                REQUIRE(taken->first.empty() == false);
                REQUIRE(taken->second == io::v2gtp::PayloadType::Part20Main);
                REQUIRE(msg_exch.has_request() == false);
            }

            THEN("the framed bytes round-trip-decode back to the SessionSetupRequest") {
                REQUIRE(taken.has_value());
                const auto& bytes = taken->first;
                message_20::Variant decoded{taken->second, io::StreamInputView{bytes.data(), bytes.size()}};
                const auto* decoded_request = decoded.get_if<message_20::SessionSetupRequest>();
                REQUIRE(decoded_request != nullptr);
                REQUIRE(decoded_request->evccid == request.evccid);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV MessageExchange retains typed request for introspection") {

    ev::d20::MessageExchange msg_exch{};

    GIVEN("A DC_PreChargeRequest with Finished processing set as the pending request") {
        message_20::DC_PreChargeRequest request{};
        request.processing = message_20::datatypes::Processing::Finished;
        request.present_voltage = {4000, -1};
        request.target_voltage = {4000, -1};

        msg_exch.set_request(request);

        THEN("get_request of the matching type round-trips the typed value") {
            const auto retrieved = msg_exch.get_request<message_20::DC_PreChargeRequest>();
            REQUIRE(retrieved.has_value());
            REQUIRE(retrieved->processing == message_20::datatypes::Processing::Finished);
        }

        THEN("get_request of a different type returns nullopt") {
            const auto wrong = msg_exch.get_request<message_20::SessionSetupRequest>();
            REQUIRE(wrong.has_value() == false);
        }
    }
}

SCENARIO("ISO15118-20 EV MessageExchange guards against encode overflow") {

    ev::d20::MessageExchange msg_exch{};

    GIVEN("A ScheduleExchangeRequest whose power schedule far exceeds the encode buffer") {
        // The AuthorizationRequest PnC certificate is dropped by the CPP->CB convert()
        // in message/authorization.cpp (a TODO there), so it can never overflow. A
        // ScheduleExchangeRequest power schedule IS fully serialized: 1024
        // EVPowerScheduleEntry elements produce an EXI payload well past the 4096-byte
        // buffer, so encode_iso20_exiDocument errors, serialize throws, and take_request
        // must surface the failure so the caller can stop.
        message_20::ScheduleExchangeRequest request{};
        request.max_supporting_points = 1024;

        message_20::datatypes::Scheduled_SEReqControlMode mode{};
        message_20::datatypes::EVEnergyOffer offer{};
        offer.power_schedule.time_anchor = 0;
        for (std::size_t i = 0; i < 1024; ++i) {
            offer.power_schedule.entries.emplace_back(message_20::datatypes::EVPowerScheduleEntry{60, {1000, 0}});
        }
        mode.energy_offer = offer;
        request.control_mode = mode;

        msg_exch.set_request(request);

        WHEN("the request is taken") {
            const auto taken = msg_exch.take_request();

            THEN("the encode failure surfaces as nullopt and the pending flag is cleared") {
                REQUIRE(taken.has_value() == false);
                REQUIRE(msg_exch.has_request() == false);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV MessageExchange guards its response slot") {

    ev::d20::MessageExchange msg_exch{};

    GIVEN("An empty MessageExchange") {

        THEN("peek_response_type reports None when no response is staged") {
            REQUIRE(msg_exch.peek_response_type() == message_20::Type::None);
        }

        THEN("pull_response on an empty slot throws") {
            REQUIRE_THROWS_AS(msg_exch.pull_response(), std::runtime_error);
        }
    }

    GIVEN("A MessageExchange with a response already staged") {
        message_20::SessionSetupResponse res{};
        res.response_code = message_20::datatypes::ResponseCode::OK_NewSessionEstablished;
        res.evseid = "DE*PNX*E12345";
        msg_exch.set_response(std::make_unique<message_20::Variant>(res));

        THEN("a second set_response throws rather than dropping the unhandled response") {
            auto second = std::make_unique<message_20::Variant>(res);
            REQUIRE_THROWS_AS(msg_exch.set_response(std::move(second)), std::runtime_error);
        }
    }
}
