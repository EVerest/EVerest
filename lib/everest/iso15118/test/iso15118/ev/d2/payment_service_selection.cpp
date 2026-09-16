// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/payment_service_selection.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {

message_2::PaymentServiceSelectionResponse make_response(const message_2::Header& header, dt::ResponseCode code) {
    message_2::PaymentServiceSelectionResponse res;
    res.header = header;
    res.response_code = code;
    return res;
}

// A minimal contract so PnCConfig::needs_cert_install() is false.
ev::EvSessionParams params_with_contract() {
    ev::EvSessionParams params;
    params.pnc.prefer_contract = true;
    params.pnc.contract_cert_der = {0x30, 0x01};
    params.pnc.contract_key_pem = "key";
    params.pnc.contract_emaid = "DEPNXCONTRACT1";
    return params;
}

ev::EvSessionParams params_prefer_contract() {
    ev::EvSessionParams params;
    params.pnc.prefer_contract = true;
    return params;
}

const auto offer_contract = [](D2StateHelper& helper) {
    helper.get_context().evse_info.selected_charge_service_id = dt::CHARGE_SERVICE_ID;
    helper.get_context().evse_info.contract_offered = true;
    helper.get_context().evse_info.certificate_service_offered = true;
};

} // namespace

SCENARIO("ISO15118-2 EV PaymentServiceSelection selects ExternalPayment by default") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed = [](D2StateHelper& helper) {
        helper.get_context().evse_info.selected_charge_service_id = dt::CHARGE_SERVICE_ID;
    };
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, seed};

    const auto request = primed.take_requests().get<message_2::PaymentServiceSelectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->selected_payment_option == dt::PaymentOption::ExternalPayment);
    REQUIRE(request->selected_service_list.size() == 1);
    REQUIRE(request->selected_service_list.front().service_id == dt::CHARGE_SERVICE_ID);
    REQUIRE(primed.ctx.pnc.contract_selected == false);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection adds the Certificate service for an installation") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, params_prefer_contract(), false,
                                                               offer_contract};

    const auto request = primed.take_requests().get<message_2::PaymentServiceSelectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->selected_payment_option == dt::PaymentOption::Contract);
    REQUIRE(request->selected_service_list.size() == 2);
    REQUIRE(request->selected_service_list.at(1).service_id == dt::CERTIFICATE_SERVICE_ID);
    REQUIRE(primed.ctx.pnc.contract_selected == true);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection routes EIM to Authorization") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::Authorization);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection routes a missing contract to CertificateInstallation") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, params_prefer_contract(), false,
                                                               offer_contract};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::CertificateInstallation);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection routes a preloaded contract to PaymentDetails") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, params_with_contract(), false,
                                                               offer_contract};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PaymentDetails);
    REQUIRE(primed.ctx.pnc.emaid == "DEPNXCONTRACT1");
    REQUIRE(primed.ctx.pnc.contract_cert_der.size() == 2);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection selects Contract when enforced without an offer") {
    const ev::feedback::Callbacks callbacks{};
    ev::EvSessionParams params;
    params.pnc.enforce_contract = true;
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, params, false, d2_no_seed};

    const auto request = primed.take_requests().get<message_2::PaymentServiceSelectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->selected_payment_option == dt::PaymentOption::Contract);
    // The Certificate service was not offered, so it is not selected.
    REQUIRE(request->selected_service_list.size() == 1);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection falls back to EIM without usable contract material") {
    const ev::feedback::Callbacks callbacks{};
    // Contract offered and preferred, but the SECC offers no Certificate service and the EV carries no
    // pre-installed contract: nothing to authenticate with.
    const auto offer_contract_only = [](D2StateHelper& helper) {
        helper.get_context().evse_info.selected_charge_service_id = dt::CHARGE_SERVICE_ID;
        helper.get_context().evse_info.contract_offered = true;
    };
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, params_prefer_contract(), false,
                                                               offer_contract_only};

    const auto request = primed.take_requests().get<message_2::PaymentServiceSelectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->selected_payment_option == dt::PaymentOption::ExternalPayment);
    REQUIRE(request->selected_service_list.size() == 1);
    REQUIRE(primed.ctx.pnc.contract_selected == false);

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::Authorization);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection keeps Contract without material when enforced") {
    const ev::feedback::Callbacks callbacks{};
    ev::EvSessionParams params;
    params.pnc.enforce_contract = true;
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, params, false, d2_no_seed};

    REQUIRE(primed.ctx.pnc.contract_selected == true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PaymentDetails);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{ctx.create_state<ev::d2::state::PaymentServiceSelection>()};
    };
    const auto make_ok = [](const message_2::Header& header) { return make_response(header, dt::ResponseCode::OK); };
    message_2::AuthorizationResponse wrong;
    wrong.header = d2_header();
    wrong.response_code = dt::ResponseCode::OK;
    wrong.evse_processing = dt::EVSEProcessing::Finished;
    check_rejection_paths(callbacks, ev::d2::StateID::PaymentServiceSelection, make_fsm, make_ok, wrong);
}
