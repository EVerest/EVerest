// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/service_selection.hpp>
#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/service_selection.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::ResponseCode;
using message_20::datatypes::ServiceCategory;

message_20::ServiceSelectionResponse make_response(const message_20::Header& header, ResponseCode code) {
    message_20::ServiceSelectionResponse res{};
    res.header = header;
    res.response_code = code;
    return res;
}

const auto seed_ac = [](FsmStateHelper& helper) { helper.get_context().set_selected_service(ServiceCategory::AC); };
} // namespace

SCENARIO("ISO15118-20 EV ServiceSelection emits a DC ServiceSelectionRequest on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceSelection> primed{callbacks, no_seed, uint16_t{3}};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->selected_energy_transfer_service.service_id == message_20::datatypes::ServiceCategory::DC);
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 3);
    REQUIRE_FALSE(request_message->selected_vas_list.has_value());
}

SCENARIO("ISO15118-20 EV ServiceSelection transitions to DC_ChargeParameterDiscovery on OK") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceSelection> primed{callbacks, no_seed, uint16_t{1}};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeParameterDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV ServiceSelection emits an AC ServiceSelectionRequest on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceSelection> primed{callbacks, seed_ac, uint16_t{3}};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.service_id == ServiceCategory::AC);
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 3);
}

SCENARIO("ISO15118-20 EV ServiceSelection transitions to AC_ChargeParameterDiscovery on OK") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceSelection> primed{callbacks, seed_ac, uint16_t{1}};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeParameterDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(primed.ctx.selected_service() == ServiceCategory::AC);
}

SCENARIO("ISO15118-20 EV ServiceSelection rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ServiceSelection>(uint16_t{1})};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_response(header, ResponseCode::OK); };
    check_rejection_paths(callbacks, ev::d20::StateID::ServiceSelection, make_fsm, make_ok,
                          message_20::ServiceDetailResponse{});
}
