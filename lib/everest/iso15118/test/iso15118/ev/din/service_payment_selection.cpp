// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/din/state/service_payment_selection.hpp>
#include <iso15118/message_din/contract_authentication.hpp>
#include <iso15118/message_din/service_payment_selection.hpp>
#include <iso15118/message_din/session_stop.hpp>

using namespace iso15118;

namespace {
namespace dt = message_din::datatypes;
using ev::din::StateID;
using State = ev::din::state::ServicePaymentSelection;

message_din::ServicePaymentSelectionResponse ok_response(const message_din::Header& hdr) {
    message_din::ServicePaymentSelectionResponse res;
    res.header = hdr;
    res.response_code = dt::ResponseCode::OK;
    return res;
}

const auto seed_charge_service = [](DinStateHelper& helper) { helper.get_context().evse_info.charge_service_id = 7; };
} // namespace

SCENARIO("DIN 70121 EV ServicePaymentSelection selects ExternalPayment and the offered ChargeService") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_charge_service};

    const auto request = primed.take_requests().get<message_din::ServicePaymentSelectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->selected_payment_option == dt::PaymentOption::ExternalPayment);
    REQUIRE(request->selected_service_list.size() == 1);
    REQUIRE(request->selected_service_list.front().service_id == 7);
}

SCENARIO("DIN 70121 EV ServicePaymentSelection transitions to ContractAuthentication on OK") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_charge_service};

    primed.handle_response(ok_response(header()));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::ContractAuthentication);
    REQUIRE(primed.take_requests().get<message_din::ContractAuthenticationRequest>().has_value());
}

SCENARIO("DIN 70121 EV ServicePaymentSelection diverts to SessionStop on a latched pause request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_charge_service};

    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(ok_response(header()));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

SCENARIO("DIN 70121 EV ServicePaymentSelection rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](DinStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(SESSION_ID);
        return fsm::v2::FSM<ev::din::StateBase>{ctx.create_state<State>()};
    };
    message_din::ContractAuthenticationResponse wrong;
    wrong.header = header();
    wrong.response_code = dt::ResponseCode::OK;
    check_rejection_paths(callbacks, StateID::ServicePaymentSelection, make_fsm, ok_response, wrong);
}
