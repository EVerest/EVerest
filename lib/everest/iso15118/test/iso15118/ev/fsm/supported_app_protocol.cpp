// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/variant.hpp>
#include <iso15118/v2gtp/sdp.hpp>

#include <iso15118/ev/d20/engine.hpp>
#include <iso15118/ev/d20/state/supported_app_protocol.hpp>
#include <iso15118/ev/session/latches.hpp>

#include "helper.hpp"

using namespace iso15118;

namespace {

std::vector<uint8_t> serialize_helper(const message_20::SupportedAppProtocolResponse& res) {
    uint8_t buffer[1024];
    io::StreamOutputView out({buffer, sizeof(buffer)});
    const auto size = message_20::serialize(res, out);
    return std::vector<uint8_t>(buffer, buffer + size);
}

std::unique_ptr<message_20::ResponseVariant>
make_sap_response_variant(const message_20::SupportedAppProtocolResponse& res) {
    const auto bytes = serialize_helper(res);
    return std::make_unique<message_20::ResponseVariant>(v2gtp::PayloadType::SAP,
                                                         io::StreamInputView{bytes.data(), bytes.size()});
}

// The engine sends the SupportedAppProtocolRequest as it starts, before the first state exists.
std::optional<std::pair<std::vector<uint8_t>, v2gtp::PayloadType>>
request_on_start(std::vector<message_20::SupportedAppProtocol> protocols) {
    ev::session::Latches latches;
    everest::lib::util::monitor<ev::params::DcChargeParams> dc_params{ev::params::DcChargeParams{}};
    everest::lib::util::monitor<ev::params::AcChargeParams> ac_params{ev::params::AcChargeParams{}};
    ev::d20::Engine engine{{},
                           "EVTESTID01",
                           std::move(protocols),
                           latches,
                           dc_params,
                           ac_params,
                           message_20::datatypes::ServiceCategory::DC,
                           {}};
    engine.start();
    auto request = engine.take_request();
    REQUIRE_FALSE(engine.has_request());
    return request;
}

} // namespace

SCENARIO("ISO15118-20 EV SupportedAppProtocol entry state initiates negotiation") {

    GIVEN("A SupportedAppProtocol state on a fresh context") {

        const ev::feedback::Callbacks callbacks{};
        FsmStateHelper helper{callbacks};

        auto& ctx = helper.get_context();
        auto& mx = helper.get_message_exchange();

        ev::d20::state::SupportedAppProtocol state{ctx};

        WHEN("The state is entered") {
            state.enter();

            THEN("It sends nothing") {
                REQUIRE_FALSE(mx.has_request());
            }
        }

        WHEN("The engine starts") {
            THEN("A SupportedAppProtocolRequest is produced") {
                const auto taken = request_on_start(DEFAULT_APP_PROTOCOLS);
                REQUIRE(taken.has_value());
                const auto& [bytes, type] = *taken;
                REQUIRE(type == v2gtp::PayloadType::SAP);

                message_20::RequestVariant variant(type, io::StreamInputView{bytes.data(), bytes.size()});
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

            const auto res = message_20::SupportedAppProtocolResponse{
                message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1};
            mx.set_response(make_sap_response_variant(res));

            auto result = state.feed(ev::d20::Event::V2GTP_MESSAGE);

            THEN("It sends a SessionSetupRequest as it transitions to SessionSetup") {
                REQUIRE(result.new_state != nullptr);
                REQUIRE(result.new_state->get_id() == ev::d20::StateID::SessionSetup);
                REQUIRE(ctx.is_session_stopped() == false);
                REQUIRE(mx.has_request());

                const auto taken = mx.take_request();
                REQUIRE(taken.has_value());
                const auto& [bytes, type] = *taken;
                REQUIRE(type == v2gtp::PayloadType::Part20Main);

                message_20::RequestVariant variant(type, io::StreamInputView{bytes.data(), bytes.size()});
                REQUIRE(variant.get_type() == message_20::Type::SessionSetupReq);

                const auto* req = variant.get_if<message_20::SessionSetupRequest>();
                REQUIRE(req != nullptr);
                REQUIRE(req->evccid == "EVTESTID01");
            }
        }

        WHEN("A SupportedAppProtocolResponse with a minor deviation is fed") {
            state.enter();

            const auto res = message_20::SupportedAppProtocolResponse{
                message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiationWithMinorDeviation, 1};
            mx.set_response(make_sap_response_variant(res));

            auto result = state.feed(ev::d20::Event::V2GTP_MESSAGE);

            THEN("It is accepted and sends a SessionSetupRequest as it transitions to SessionSetup") {
                REQUIRE(result.new_state != nullptr);
                REQUIRE(result.new_state->get_id() == ev::d20::StateID::SessionSetup);
                REQUIRE(ctx.is_session_stopped() == false);
                REQUIRE(mx.has_request());

                const auto taken = mx.take_request();
                REQUIRE(taken.has_value());
                const auto& [bytes, type] = *taken;
                REQUIRE(type == v2gtp::PayloadType::Part20Main);

                message_20::RequestVariant variant(type, io::StreamInputView{bytes.data(), bytes.size()});
                REQUIRE(variant.get_if<message_20::SessionSetupRequest>() != nullptr);
            }
        }

        WHEN("A failed SupportedAppProtocolResponse is fed") {
            state.enter();

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

            // A SessionSetupResponse is not the expected SAP response variant.
            message_20::SessionSetupResponse session_res{};
            session_res.response_code = message_20::datatypes::ResponseCode::OK_NewSessionEstablished;
            session_res.evseid = "DE*PNX*E1234";
            mx.set_response(std::make_unique<message_20::ResponseVariant>(session_res));

            auto result = state.feed(ev::d20::Event::V2GTP_MESSAGE);

            THEN("The session is stopped and no transition occurs") {
                REQUIRE(ctx.is_session_stopped() == true);
                REQUIRE(result.new_state == nullptr);
            }
        }

        WHEN("A non-V2GTP event is fed") {
            state.enter();

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

    GIVEN("An engine whose context advertises two app protocols in order") {

        // Two distinct namespaces with descending priority. The request must carry every
        // advertised entry, preserving order.
        const std::vector<message_20::SupportedAppProtocol> protocols{
            {"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1},
            {"urn:iso:std:iso:15118:-20:AC", 1, 0, 2, 2},
        };

        WHEN("The engine starts") {
            const auto taken = request_on_start(protocols);

            THEN("The emitted SupportedAppProtocolRequest carries both entries in order") {
                REQUIRE(taken.has_value());
                const auto& [bytes, type] = *taken;
                REQUIRE(type == v2gtp::PayloadType::SAP);

                message_20::RequestVariant variant(type, io::StreamInputView{bytes.data(), bytes.size()});
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
