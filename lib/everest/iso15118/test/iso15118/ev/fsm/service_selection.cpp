// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/service_detail.hpp>
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

// ServiceSelection's first request, sent by ServiceDetail as it selects the one offered Dynamic set.
DecodedRequests first_request(ServiceCategory service, uint16_t parameter_set_id) {
    auto detail = Predecessor::in<ev::d20::state::ServiceDetail>(service, {});
    message_20::datatypes::ParameterSet set{};
    set.id = parameter_set_id;
    set.parameter.push_back({"Connector", static_cast<int32_t>(1)});
    set.parameter.push_back({"ControlMode", static_cast<int32_t>(message_20::datatypes::ControlMode::Dynamic)});
    message_20::ServiceDetailResponse res{};
    res.header = SESSION_HEADER;
    res.response_code = ResponseCode::OK;
    res.service = message_20::to_underlying_value(service);
    res.service_parameter_list.clear();
    res.service_parameter_list.push_back(set);
    return detail.transitions_to(ev::d20::StateID::ServiceSelection, res);
}
} // namespace

SCENARIO("ISO15118-20 EV ServiceDetail sends a DC ServiceSelectionRequest") {
    const auto requests = first_request(ServiceCategory::DC, 3);
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

SCENARIO("ISO15118-20 EV ServiceDetail sends an AC ServiceSelectionRequest") {
    const auto requests = first_request(ServiceCategory::AC, 3);
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.service_id == ServiceCategory::AC);
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 3);
}

SCENARIO("ISO15118-20 EV ServiceSelection transitions to AC_ChargeParameterDiscovery on OK") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceSelection> primed{callbacks, ServiceCategory::AC, no_seed, uint16_t{1}};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeParameterDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(primed.ctx.selected_service() == ServiceCategory::AC);
}

SCENARIO("ISO15118-20 EV ServiceDetail sends an AC_BPT ServiceSelectionRequest") {
    const auto requests = first_request(ServiceCategory::AC_BPT, 5);
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.service_id == ServiceCategory::AC_BPT);
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 5);
}

SCENARIO("ISO15118-20 EV ServiceSelection transitions to AC_ChargeParameterDiscovery on OK for AC_BPT") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceSelection> primed{callbacks, ServiceCategory::AC_BPT, no_seed, uint16_t{1}};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeParameterDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(primed.ctx.selected_service() == ServiceCategory::AC_BPT);
}

SCENARIO("ISO15118-20 EV ServiceDetail sends a DC_BPT ServiceSelectionRequest") {
    const auto requests = first_request(ServiceCategory::DC_BPT, 7);
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.service_id == ServiceCategory::DC_BPT);
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 7);
}

SCENARIO("ISO15118-20 EV ServiceSelection transitions to DC_ChargeParameterDiscovery on OK for DC_BPT") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceSelection> primed{callbacks, ServiceCategory::DC_BPT, no_seed, uint16_t{1}};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeParameterDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(primed.ctx.selected_service() == ServiceCategory::DC_BPT);
}

SCENARIO("ISO15118-20 EV ServiceDetail sends an MCS ServiceSelectionRequest") {
    const auto requests = first_request(ServiceCategory::MCS, 8);
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.service_id == ServiceCategory::MCS);
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 8);
}

// MCS is a DC service: it must take the DC parameter discovery, not fall through to the
// "not supported by the EV" arm that stops the session.
SCENARIO("ISO15118-20 EV ServiceSelection transitions to DC_ChargeParameterDiscovery on OK for MCS") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceSelection> primed{callbacks, ServiceCategory::MCS, no_seed, uint16_t{1}};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeParameterDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(primed.ctx.selected_service() == ServiceCategory::MCS);
}

SCENARIO("ISO15118-20 EV ServiceSelection transitions to DC_ChargeParameterDiscovery on OK for MCS_BPT") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceSelection> primed{callbacks, ServiceCategory::MCS_BPT, no_seed, uint16_t{1}};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeParameterDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(primed.ctx.selected_service() == ServiceCategory::MCS_BPT);
}

SCENARIO("ISO15118-20 EV ServiceDetail sends an AC_DER_IEC ServiceSelectionRequest") {
    const auto requests = first_request(ServiceCategory::AC_DER_IEC, 5);
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.service_id == ServiceCategory::AC_DER_IEC);
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 5);
}

SCENARIO("ISO15118-20 EV ServiceSelection transitions to AC_DER_IEC_ChargeParameterDiscovery on OK") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceSelection> primed{callbacks, ServiceCategory::AC_DER_IEC, no_seed, uint16_t{1}};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_DER_IEC_ChargeParameterDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(primed.ctx.selected_service() == ServiceCategory::AC_DER_IEC);
}

SCENARIO("ISO15118-20 EV ServiceSelection transitions to AC_DER_SAE_ChargeParameterDiscovery on OK") {
    const ev::feedback::Callbacks callbacks{};
    const auto requests = first_request(ServiceCategory::AC_DER_SAE, 4);
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.service_id == ServiceCategory::AC_DER_SAE);
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 4);

    PrimedState<ev::d20::state::ServiceSelection> primed{callbacks, ServiceCategory::AC_DER_SAE, no_seed, uint16_t{4}};
    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_DER_SAE_ChargeParameterDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV ServiceSelection stops the session on an unsupported service category") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceSelection> primed{callbacks, ServiceCategory::WPT, no_seed, uint16_t{1}};

    expect_stops_session(primed, make_response(SESSION_HEADER, ResponseCode::OK), ev::d20::StateID::ServiceSelection);
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
