// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// SupportedAppProtocol negotiation over a multi-generation offer: the schema id the
// SECC picks is mapped back to a protocol generation through SessionOptions::offered_protocols.
// Only the ISO 15118-20 engine exists, so anything else hands over into a stopped session.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <vector>

#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/supported_app_protocol.hpp>

#include <iso15118/ev/sap_offer.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using namespace iso15118::ev::test;

namespace {

using PT = io::v2gtp::PayloadType;

// DC offer of -20 (schema id 1) and -2 (schema id 2).
std::vector<ev::OfferedProtocol> dual_offer() {
    ev::SapOfferInput input;
    input.supported_protocols = {ProtocolId::ISO15118_20, ProtocolId::ISO15118_2};
    return build_sap_offer(input);
}

std::unique_ptr<SessionFixture> dual_offer_fixture() {
    const auto offer = dual_offer();
    std::vector<message_20::SupportedAppProtocol> advertised;
    for (const auto& entry : offer) {
        advertised.push_back(entry.entry);
    }
    ev::d20::SessionOptions options;
    options.offered_protocols = offer;
    return std::make_unique<SessionFixture>("EVTESTID01", ev::SessionTiming{5ms, 100ms}, ev::DcChargeParams{},
                                            advertised, message_20::datatypes::ServiceCategory::DC,
                                            ev::AcChargeParams{}, default_der_control_functions(), true,
                                            std::move(options));
}

message_20::SupportedAppProtocolResponse sap_response(uint8_t schema_id) {
    return {message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, schema_id};
}

} // namespace

SCENARIO("ISO15118-20 EV Session maps the negotiated schema id back to a protocol generation") {
    GIVEN("a Session offering ISO 15118-20 (schema 1) and ISO 15118-2 (schema 2)") {
        auto fx = dual_offer_fixture();
        fx->session.start();
        REQUIRE(run_reactor_until(
            fx->reactor, [&]() { return fx->captured.size() >= 1; }, 1s));

        WHEN("the SECC selects schema id 2") {
            fx->session.on_bytes_received(frame_payload(PT::SAP, serialize_msg(sap_response(2))));

            THEN("ISO 15118-2 is reported and the session stops: no engine implements it") {
                REQUIRE(fx->selected_protocol == ProtocolId::ISO15118_2);
                REQUIRE(fx->session.selected_protocol() == ProtocolId::ISO15118_2);
                REQUIRE(fx->session.is_finished());
                REQUIRE(fx->signals == std::vector<ev::feedback::Signal>{ev::feedback::Signal::DLINK_TERMINATE});
            }

            THEN("no SessionSetupRequest is emitted") {
                run_reactor_until(
                    fx->reactor, [&]() { return fx->captured.size() > 1; }, 100ms);
                REQUIRE(fx->captured.size() == 1);
            }
        }

        WHEN("the SECC selects a schema id that was never offered") {
            fx->session.on_bytes_received(frame_payload(PT::SAP, serialize_msg(sap_response(7))));

            THEN("the session stops and no protocol is reported") {
                REQUIRE(fx->session.is_finished());
                REQUIRE_FALSE(fx->selected_protocol.has_value());
                REQUIRE(fx->captured.size() == 1);
            }
        }

        WHEN("the SECC selects schema id 1") {
            fx->session.on_bytes_received(frame_payload(PT::SAP, serialize_msg(sap_response(1))));

            THEN("ISO 15118-20 is reported and the session proceeds to SessionSetup") {
                REQUIRE(fx->selected_protocol == ProtocolId::ISO15118_20);
                REQUIRE(run_reactor_until(
                    fx->reactor, [&]() { return fx->captured.size() >= 2; }, 1s));
                auto variant = decode_frame(fx->captured.back());
                REQUIRE(variant.get_if<message_20::SessionSetupRequest>() != nullptr);
                REQUIRE_FALSE(fx->session.is_finished());
            }
        }
    }

    GIVEN("a Session with an empty offered_protocols map") {
        SessionFixture fx;
        fx.session.start();
        REQUIRE(run_reactor_until(
            fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));

        WHEN("the SECC selects schema id 1") {
            fx.session.on_bytes_received(frame_payload(PT::SAP, serialize_msg(sap_response(1))));

            THEN("every offered entry counts as ISO 15118-20 and the session proceeds") {
                REQUIRE(fx.selected_protocol == ProtocolId::ISO15118_20);
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() >= 2; }, 1s));
                REQUIRE_FALSE(fx.session.is_finished());
            }
        }
    }
}
