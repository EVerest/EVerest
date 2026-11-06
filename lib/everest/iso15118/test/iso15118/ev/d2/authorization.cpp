// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <cbv2g/iso_2/iso2_msgDefDecoder.h>

#include <iso15118/ev/d2/state/authorization.hpp>
#include <iso15118/ev/d2/state/payment_details.hpp>
#include <iso15118/ev/d2/state/payment_service_selection.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/payment_details.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/session_stop.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {

constexpr dt::GenChallenge CHALLENGE{16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};

message_2::AuthorizationRequest take_authorization_request(D2StateHelper& helper, bool signed_request) {
    const auto sent = helper.get_message_exchange().take_request();
    REQUIRE(sent.has_value());
    auto bytes = sent->first;
    auto doc = std::make_unique<iso2_exiDocument>();
    init_iso2_exiDocument(doc.get());
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, bytes.data(), bytes.size(), 0, nullptr);
    REQUIRE(decode_iso2_exiDocument(&stream, doc.get()) == 0);
    REQUIRE(static_cast<bool>(doc->V2G_Message.Header.Signature_isUsed) == signed_request);
    message_2::Variant decoded{io::StreamInputView{bytes.data(), bytes.size()}};
    REQUIRE(decoded.get_type() == message_2::Type::AuthorizationReq);
    return decoded.get<message_2::AuthorizationRequest>();
}

message_2::AuthorizationResponse make_response(const message_2::Header& header, dt::ResponseCode code,
                                               dt::EVSEProcessing processing) {
    message_2::AuthorizationResponse res;
    res.header = header;
    res.response_code = code;
    res.evse_processing = processing;
    return res;
}

message_2::PaymentDetailsResponse payment_details_ok() {
    message_2::PaymentDetailsResponse res;
    res.header = d2_header();
    res.response_code = dt::ResponseCode::OK;
    res.gen_challenge = CHALLENGE;
    return res;
}

} // namespace

SCENARIO("ISO15118-2 EV Authorization's EIM request goes out empty on the PaymentServiceSelection transition") {
    const ev::feedback::Callbacks callbacks{};
    const auto by_payment_service_selection = d2_entered_by([](D2StateHelper& helper) {
        message_2::PaymentServiceSelectionResponse res;
        res.header = d2_header();
        res.response_code = dt::ResponseCode::OK;
        return d2_successor_of<ev::d2::state::PaymentServiceSelection>(helper, res);
    });
    PrimedState<ev::d2::state::Authorization> primed{callbacks, d2_no_seed, by_payment_service_selection};

    const auto request = primed.take_requests().get<message_2::AuthorizationRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
    REQUIRE_FALSE(request->id.has_value());
    REQUIRE_FALSE(request->gen_challenge.has_value());
}

SCENARIO("ISO15118-2 EV Authorization resends on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::Authorization> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Ongoing));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::d2::Disposition::Awaiting);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::Authorization);
    const auto request = take_authorization_request(primed.helper, false);
    REQUIRE(request.header.session_id == D2_SESSION_ID);
    REQUIRE_FALSE(request.id.has_value());
    REQUIRE_FALSE(request.gen_challenge.has_value());
}

SCENARIO("ISO15118-2 EV Authorization leaves an Ongoing poll on a latched pause request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::Authorization> primed{callbacks, d2_no_seed};

    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Ongoing));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);

    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
}

SCENARIO("ISO15118-2 EV Authorization transitions to ChargeParameterDiscovery on Finished") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::Authorization> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::ChargeParameterDiscovery);
}

SCENARIO("ISO15118-2 EV Authorization diverts to SessionStop on a latched pause") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::Authorization> primed{callbacks, d2_no_seed};

    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
    const auto stop = primed.take_requests().get<message_2::SessionStopRequest>();
    REQUIRE(stop.has_value());
    REQUIRE(stop->charging_session == dt::ChargingSession::Pause);
}

SCENARIO("ISO15118-2 EV Authorization's request is signed on the PaymentDetails transition of a Contract session") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_contract = [](D2StateHelper& helper) {
        auto& pnc = helper.get_context().pnc;
        pnc.contract_selected = true;
        pnc.contract.emplace().key_pem = make_test_ec_key_pem();
    };
    const auto by_payment_details = d2_entered_by([](D2StateHelper& helper) {
        return d2_successor_of<ev::d2::state::PaymentDetails>(helper, payment_details_ok());
    });
    PrimedState<ev::d2::state::Authorization> primed{callbacks, seed_contract, by_payment_details};

    const auto request = take_authorization_request(primed.helper, true);
    REQUIRE(request.header.session_id == D2_SESSION_ID);
    REQUIRE(request.id == "id1");
    REQUIRE(request.gen_challenge == CHALLENGE);

    for (int poll = 0; poll < 2; ++poll) {
        primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Ongoing));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).output == ev::d2::Disposition::Awaiting);
        const auto repeat = take_authorization_request(primed.helper, false);
        REQUIRE(repeat.header.session_id == D2_SESSION_ID);
        REQUIRE_FALSE(repeat.id.has_value());
        REQUIRE_FALSE(repeat.gen_challenge.has_value());
    }
}

SCENARIO("ISO15118-2 EV Authorization is not entered when the contract key is unusable") {
    const auto seed_bad_key = [](D2StateHelper& helper) {
        auto& pnc = helper.get_context().pnc;
        pnc.contract_selected = true;
        pnc.contract.emplace().key_pem = "not a key";
    };
    auto pred = D2Predecessor::make<ev::d2::state::PaymentDetails>(ev::params::EvSessionParams{}, false, seed_bad_key);
    pred.stops_without_request(payment_details_ok());
}

SCENARIO("ISO15118-2 EV Authorization polls empty even when the contract key is unusable") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_bad_key = [](D2StateHelper& helper) {
        auto& pnc = helper.get_context().pnc;
        pnc.contract_selected = true;
        pnc.contract.emplace().key_pem = "not a key";
    };
    PrimedState<ev::d2::state::Authorization> primed{callbacks, seed_bad_key};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Ongoing));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).output == ev::d2::Disposition::Awaiting);
    const auto request = take_authorization_request(primed.helper, false);
    REQUIRE_FALSE(request.id.has_value());
    REQUIRE_FALSE(request.gen_challenge.has_value());
    REQUIRE_FALSE(primed.ctx.is_session_stopped());
}

SCENARIO("ISO15118-2 EV Authorization rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{ctx.create_state<ev::d2::state::Authorization>()};
    };
    const auto make_ok = [](const message_2::Header& header) {
        return make_response(header, dt::ResponseCode::OK, dt::EVSEProcessing::Finished);
    };
    const message_2::SessionStopResponse wrong{d2_header(), dt::ResponseCode::OK};
    check_rejection_paths(callbacks, ev::d2::StateID::Authorization, make_fsm, make_ok, wrong);
}
