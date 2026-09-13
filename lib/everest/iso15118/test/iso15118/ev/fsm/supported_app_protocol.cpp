// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/variant.hpp>

#include <iso15118/ev/d20/state/supported_app_protocol.hpp>

#include "helper.hpp"

using namespace iso15118;

namespace {

std::vector<uint8_t> serialize_helper(const message_20::SupportedAppProtocolResponse& res) {
    uint8_t buffer[1024];
    io::StreamOutputView out({buffer, sizeof(buffer)});
    const auto size = message_20::serialize(res, out);
    return std::vector<uint8_t>(buffer, buffer + size);
}

std::unique_ptr<message_20::Variant> make_sap_response_variant(const message_20::SupportedAppProtocolResponse& res) {
    const auto bytes = serialize_helper(res);
    return std::make_unique<message_20::Variant>(io::v2gtp::PayloadType::SAP,
                                                 io::StreamInputView{bytes.data(), bytes.size()});
}

} // namespace

SCENARIO("EV d20 SupportedAppProtocol entry state initiates negotiation") {

    GIVEN("A SupportedAppProtocol state on a fresh context") {

        const ev::feedback::Callbacks callbacks{};
        FsmStateHelper helper{callbacks};

        auto& ctx = helper.get_context();
        auto& mx = helper.get_message_exchange();

        ev::d20::state::SupportedAppProtocol state{ctx};

        WHEN("The state is entered") {
            state.enter();

            THEN("A SupportedAppProtocolRequest is produced") {
                REQUIRE(mx.has_request());

                const auto taken = mx.take_request();
                REQUIRE(taken.has_value());
                const auto& [bytes, type] = *taken;
                REQUIRE(type == io::v2gtp::PayloadType::SAP);

                message_20::Variant variant(type, io::StreamInputView{bytes.data(), bytes.size()});
                REQUIRE(variant.get_type() == message_20::Type::SupportedAppProtocolReq);

                const auto* req = variant.get_if<message_20::SupportedAppProtocolRequest>();
                REQUIRE(req != nullptr);
                REQUIRE(req->app_protocol.size() == 1);

                const auto& ap = req->app_protocol[0];
                REQUIRE(ap.protocol_namespace == "urn:iso:std:iso:15118:-20:DC");
                REQUIRE(ap.version_number_major == 1);
                REQUIRE(ap.version_number_minor == 0);
                REQUIRE(ap.schema_id == 1);
                REQUIRE(ap.priority == 1);
            }
        }

        WHEN("A successful SupportedAppProtocolResponse is fed") {
            state.enter();
            mx.take_request(); // drain the SAP request

            const auto res = message_20::SupportedAppProtocolResponse{
                message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1};
            mx.set_response(make_sap_response_variant(res));

            auto result = state.feed(ev::d20::Event::V2GTP_MESSAGE);

            THEN("It produces a SessionSetupRequest and transitions to SessionSetup") {
                REQUIRE(mx.has_request());

                const auto taken = mx.take_request();
                REQUIRE(taken.has_value());
                const auto& [bytes, type] = *taken;
                REQUIRE(type == io::v2gtp::PayloadType::Part20Main);

                message_20::Variant variant(type, io::StreamInputView{bytes.data(), bytes.size()});
                REQUIRE(variant.get_type() == message_20::Type::SessionSetupReq);

                const auto* req = variant.get_if<message_20::SessionSetupRequest>();
                REQUIRE(req != nullptr);
                REQUIRE(req->evccid == "EVTESTID01");

                REQUIRE(result.new_state != nullptr);
                REQUIRE(result.new_state->get_id() == ev::d20::StateID::SessionSetup);
                REQUIRE(ctx.is_session_stopped() == false);
            }
        }

        WHEN("A SupportedAppProtocolResponse with a minor deviation is fed") {
            state.enter();
            mx.take_request(); // drain the SAP request

            const auto res = message_20::SupportedAppProtocolResponse{
                message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiationWithMinorDeviation, 1};
            mx.set_response(make_sap_response_variant(res));

            auto result = state.feed(ev::d20::Event::V2GTP_MESSAGE);

            THEN("It is accepted: a SessionSetupRequest is produced and it transitions to SessionSetup") {
                REQUIRE(mx.has_request());

                const auto taken = mx.take_request();
                REQUIRE(taken.has_value());
                const auto& [bytes, type] = *taken;
                REQUIRE(type == io::v2gtp::PayloadType::Part20Main);

                message_20::Variant variant(type, io::StreamInputView{bytes.data(), bytes.size()});
                REQUIRE(variant.get_if<message_20::SessionSetupRequest>() != nullptr);

                REQUIRE(result.new_state != nullptr);
                REQUIRE(result.new_state->get_id() == ev::d20::StateID::SessionSetup);
                REQUIRE(ctx.is_session_stopped() == false);
            }
        }

        WHEN("A failed SupportedAppProtocolResponse is fed") {
            state.enter();
            mx.take_request();

            const auto res = message_20::SupportedAppProtocolResponse{
                message_20::SupportedAppProtocolResponse::ResponseCode::Failed_NoNegotiation, std::nullopt};
            mx.set_response(make_sap_response_variant(res));

            auto result = state.feed(ev::d20::Event::V2GTP_MESSAGE);

            THEN("The session is stopped and no transition occurs") {
                REQUIRE(ctx.is_session_stopped() == true);
                REQUIRE(result.new_state == nullptr);
            }
        }

        WHEN("A wrong-variant response is fed") {
            state.enter();
            mx.take_request();

            // A SessionSetupResponse is not the expected SAP response variant.
            message_20::SessionSetupResponse session_res{};
            session_res.response_code = message_20::datatypes::ResponseCode::OK_NewSessionEstablished;
            session_res.evseid = "DE*PNX*E1234";
            mx.set_response(std::make_unique<message_20::Variant>(session_res));

            auto result = state.feed(ev::d20::Event::V2GTP_MESSAGE);

            THEN("The session is stopped and no transition occurs") {
                REQUIRE(ctx.is_session_stopped() == true);
                REQUIRE(result.new_state == nullptr);
            }
        }

        WHEN("A non-V2GTP event is fed") {
            state.enter();
            mx.take_request();

            auto result = state.feed(ev::d20::Event::RESET);

            THEN("Nothing happens") {
                REQUIRE(result.unhandled == true);
                REQUIRE(result.new_state == nullptr);
                REQUIRE(ctx.is_session_stopped() == false);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV SupportedAppProtocol advertises a multi-entry app protocol list") {

    GIVEN("A SupportedAppProtocol state whose context advertises two app protocols in order") {

        const ev::feedback::Callbacks callbacks{};
        FsmStateHelper helper{callbacks};

        auto& ctx = helper.get_context();
        auto& mx = helper.get_message_exchange();

        // Two distinct namespaces with descending priority. The state must copy every
        // advertised entry into the request, preserving order.
        ctx.advertised_app_protocols = {
            {"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1},
            {"urn:iso:std:iso:15118:-20:AC", 1, 0, 2, 2},
        };

        ev::d20::state::SupportedAppProtocol state{ctx};

        WHEN("The state is entered") {
            state.enter();

            THEN("The emitted SupportedAppProtocolRequest carries both entries in order") {
                REQUIRE(mx.has_request());

                const auto taken = mx.take_request();
                REQUIRE(taken.has_value());
                const auto& [bytes, type] = *taken;
                REQUIRE(type == io::v2gtp::PayloadType::SAP);

                message_20::Variant variant(type, io::StreamInputView{bytes.data(), bytes.size()});
                const auto* req = variant.get_if<message_20::SupportedAppProtocolRequest>();
                REQUIRE(req != nullptr);

                REQUIRE(req->app_protocol.size() == 2);
                REQUIRE(req->app_protocol[0].protocol_namespace == "urn:iso:std:iso:15118:-20:DC");
                REQUIRE(req->app_protocol[0].schema_id == 1);
                REQUIRE(req->app_protocol[1].protocol_namespace == "urn:iso:std:iso:15118:-20:AC");
                REQUIRE(req->app_protocol[1].schema_id == 2);
            }
        }
    }
}
