// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "helper.hpp"

#include <iso15118/ev/din/state/service_discovery.hpp>
#include <iso15118/message_din/service_discovery.hpp>
#include <iso15118/message_din/service_payment_selection.hpp>
#include <iso15118/message_din/session_stop.hpp>

using namespace iso15118;

namespace {
namespace dt = message_din::datatypes;
using ev::din::StateID;
using State = ev::din::state::ServiceDiscovery;

message_din::ServiceDiscoveryResponse make_response(const message_din::Header& hdr, dt::ResponseCode code,
                                                    dt::SupportedEnergyTransferMode mode, dt::PaymentOption payment) {
    message_din::ServiceDiscoveryResponse res;
    res.header = hdr;
    res.response_code = code;
    res.payment_options = {payment};
    res.charge_service.service_tag.service_id = 7;
    res.charge_service.energy_transfer_type = mode;
    return res;
}

message_din::ServiceDiscoveryResponse ok_response(const message_din::Header& hdr) {
    return make_response(hdr, dt::ResponseCode::OK, dt::SupportedEnergyTransferMode::DC_extended,
                         dt::PaymentOption::ExternalPayment);
}
} // namespace

SCENARIO("DIN 70121 EV ServiceDiscovery sends the request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    const auto requests = primed.take_requests();
    const auto request = requests.get<message_din::ServiceDiscoveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == SESSION_ID);
}

SCENARIO("DIN 70121 EV ServiceDiscovery transitions to ServicePaymentSelection on a matching offer") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    primed.handle_response(ok_response(header()));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::ServicePaymentSelection);
    REQUIRE(primed.ctx.evse_info.charge_service_id == 7);
    REQUIRE(primed.take_requests().get<message_din::ServicePaymentSelectionRequest>().has_value());
}

SCENARIO("DIN 70121 EV ServiceDiscovery stops when the SECC offers no ExternalPayment") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    primed.handle_response(make_response(header(), dt::ResponseCode::OK, dt::SupportedEnergyTransferMode::DC_extended,
                                         dt::PaymentOption::Contract));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(result.output == ev::din::Disposition::Transitioning);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

SCENARIO("DIN 70121 EV ServiceDiscovery stops on an AC-only energy transfer mode") {
    const ev::feedback::Callbacks callbacks{};

    // The only two offers a DC EV cannot work with.
    const auto ac_only = GENERATE(dt::SupportedEnergyTransferMode::AC_single_phase_core,
                                  dt::SupportedEnergyTransferMode::AC_three_phase_core);

    PrimedState<State> primed{callbacks, no_seed};

    primed.handle_response(make_response(header(), dt::ResponseCode::OK, ac_only, dt::PaymentOption::ExternalPayment));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

SCENARIO("DIN 70121 EV ServiceDiscovery continues against a charger offering DC in another form") {
    const ev::feedback::Callbacks callbacks{};

    // How a combo charger advertises both families. The EV asks for DC_extended; none of these is
    // that value, but all of them carry DC, so the session goes on and ChargeParameterDiscovery
    // settles whether the charger can really serve it.
    const auto combo =
        GENERATE(dt::SupportedEnergyTransferMode::DC_core, dt::SupportedEnergyTransferMode::DC_combo_core,
                 dt::SupportedEnergyTransferMode::DC_dual, dt::SupportedEnergyTransferMode::AC_core1p_DC_extended,
                 dt::SupportedEnergyTransferMode::AC_single_DC_core,
                 dt::SupportedEnergyTransferMode::AC_single_phase_three_phase_core_DC_extended,
                 dt::SupportedEnergyTransferMode::AC_core3p_DC_extended);

    PrimedState<State> primed{callbacks, no_seed};

    primed.handle_response(make_response(header(), dt::ResponseCode::OK, combo, dt::PaymentOption::ExternalPayment));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::ServicePaymentSelection);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(primed.take_requests().get<message_din::ServicePaymentSelectionRequest>().has_value());
}

SCENARIO("DIN 70121 EV ServiceDiscovery diverts to SessionStop on a latched stop request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    primed.ctx.set_stop_charging_requested(true);
    primed.handle_response(ok_response(header()));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

SCENARIO("DIN 70121 EV ServiceDiscovery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](DinStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(SESSION_ID);
        return fsm::v2::FSM<ev::din::StateBase>{ctx.create_state<State>()};
    };
    message_din::ServicePaymentSelectionResponse wrong;
    wrong.header = header();
    wrong.response_code = dt::ResponseCode::OK;
    check_rejection_paths(callbacks, StateID::ServiceDiscovery, make_fsm, ok_response, wrong);
}
