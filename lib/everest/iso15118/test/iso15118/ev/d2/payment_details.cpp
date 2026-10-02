// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/payment_details.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/payment_details.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {

constexpr dt::GenChallenge CHALLENGE{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

message_2::PaymentDetailsResponse make_response(const message_2::Header& header, dt::ResponseCode code) {
    message_2::PaymentDetailsResponse res;
    res.header = header;
    res.response_code = code;
    res.gen_challenge = CHALLENGE;
    return res;
}

const auto seed_contract = [](D2StateHelper& helper) {
    auto& pnc = helper.get_context().pnc;
    pnc.contract_selected = true;
    pnc.emaid = "DEPNXCONTRACT1";
    pnc.contract_cert_der = {0x30, 0x02, 0x03};
    pnc.contract_sub_certs_der = {{0x30, 0x04}};
};

} // namespace

SCENARIO("ISO15118-2 EV PaymentDetails presents the contract chain") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PaymentDetails> primed{callbacks, seed_contract};

    const auto request = primed.take_requests().get<message_2::PaymentDetailsRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
    REQUIRE(request->emaid == "DEPNXCONTRACT1");
    REQUIRE(request->contract_certificate.size() == 3);
    REQUIRE(request->sub_certificates.size() == 1);
}

SCENARIO("ISO15118-2 EV PaymentDetails keeps the GenChallenge and moves to Authorization") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PaymentDetails> primed{callbacks, seed_contract};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::Authorization);
    REQUIRE(primed.ctx.pnc.gen_challenge == CHALLENGE);
}

SCENARIO("ISO15118-2 EV PaymentDetails rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{ctx.create_state<ev::d2::state::PaymentDetails>()};
    };
    const auto make_ok = [](const message_2::Header& header) { return make_response(header, dt::ResponseCode::OK); };
    message_2::AuthorizationResponse wrong;
    wrong.header = d2_header();
    wrong.response_code = dt::ResponseCode::OK;
    wrong.evse_processing = dt::EVSEProcessing::Finished;
    check_rejection_paths(callbacks, ev::d2::StateID::PaymentDetails, make_fsm, make_ok, wrong);
}
