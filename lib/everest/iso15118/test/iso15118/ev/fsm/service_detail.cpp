// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <bitset>

#include "helper.hpp"

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/ev/d20/state/service_detail.hpp>
#include <iso15118/ev/der_control_functions.hpp>
#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/service_selection.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::ControlMode;
using message_20::datatypes::ParameterSet;
using message_20::datatypes::ResponseCode;
using message_20::datatypes::ServiceCategory;

ParameterSet make_param_set(uint16_t id, ControlMode control_mode) {
    ParameterSet set{};
    set.id = id;
    set.parameter.push_back({"Connector", static_cast<int32_t>(1)});
    set.parameter.push_back({"ControlMode", static_cast<int32_t>(control_mode)});
    set.parameter.push_back({"EVSENominalVoltage", static_cast<int32_t>(230)});
    return set;
}

// Mirrors an EXI decode where the SECC encoded ControlMode in a narrow width
// (byteValue/shortValue rather than intValue), yielding int8_t/int16_t.
template <typename NarrowInt> ParameterSet make_param_set_narrow(uint16_t id, ControlMode control_mode) {
    ParameterSet set{};
    set.id = id;
    set.parameter.push_back({"Connector", static_cast<int32_t>(1)});
    set.parameter.push_back({"ControlMode", static_cast<NarrowInt>(control_mode)});
    set.parameter.push_back({"EVSENominalVoltage", static_cast<int32_t>(230)});
    return set;
}

message_20::ServiceDetailResponse make_response(const message_20::Header& header, ResponseCode code,
                                                ServiceCategory service,
                                                const std::vector<ParameterSet>& parameter_sets) {
    message_20::ServiceDetailResponse res{};
    res.header = header;
    res.response_code = code;
    res.service = message_20::to_underlying_value(service);
    res.service_parameter_list.clear();
    for (const auto& set : parameter_sets) {
        res.service_parameter_list.push_back(set);
    }
    return res;
}

// A DC response that offers a Scheduled set and a Dynamic set; honest selection must
// pick the Dynamic one (id 7).
message_20::ServiceDetailResponse make_dc_response(const message_20::Header& header, ResponseCode code) {
    return make_response(header, code, ServiceCategory::DC,
                         {make_param_set(5, ControlMode::Scheduled), make_param_set(7, ControlMode::Dynamic)});
}

const auto seed_ac = [](FsmStateHelper& helper) { helper.get_context().set_selected_service(ServiceCategory::AC); };

using iso15118::iec::DERControlName;

// Parameter set carrying a DERControlFunctions bitmask, encoded at the requested integer
// width so a narrow EXI encoding (int8_t/int16_t) can be exercised too.
template <typename Int = int32_t>
ParameterSet make_der_param_set(uint16_t id, ControlMode control_mode,
                                std::bitset<ev::DER_CONTROL_FUNCTION_COUNT> der_mask) {
    ParameterSet set{};
    set.id = id;
    set.parameter.push_back({"Connector", static_cast<int32_t>(1)});
    set.parameter.push_back({"ControlMode", static_cast<int32_t>(control_mode)});
    set.parameter.push_back({"EVSENominalVoltage", static_cast<int32_t>(230)});
    set.parameter.push_back({"DERControlFunctions", static_cast<Int>(der_mask.to_ulong())});
    return set;
}

std::bitset<ev::DER_CONTROL_FUNCTION_COUNT> der_mask(std::initializer_list<DERControlName> functions) {
    std::bitset<ev::DER_CONTROL_FUNCTION_COUNT> mask;
    for (const auto function : functions) {
        mask.set(static_cast<std::size_t>(function));
    }
    return mask;
}

// An EV supporting the two DSO setpoint functions, requesting the AC_DER_IEC service.
ev::DerControlFunctions dso_setpoint_support() {
    ev::DerControlFunctions functions{};
    functions.dso_q_setpoint_provision = true;
    functions.dso_cos_phi_setpoint_provision = true;
    return functions;
}
} // namespace

SCENARIO("ISO15118-20 EV ServiceDetail transitions to ServiceSelection with the Dynamic parameter set") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, no_seed};

    primed.handle_response(make_dc_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->selected_energy_transfer_service.service_id == ServiceCategory::DC);
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 7);
}

SCENARIO("ISO15118-20 EV ServiceDetail emits a DC ServiceDetailRequest on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, no_seed};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceDetailRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->service == message_20::to_underlying_value(ServiceCategory::DC));
}

SCENARIO("ISO15118-20 EV ServiceDetail requests the configured AC service on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, seed_ac};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceDetailRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->service == message_20::to_underlying_value(ServiceCategory::AC));
}

SCENARIO("ISO15118-20 EV ServiceDetail selects the Dynamic parameter set for AC") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, seed_ac};

    primed.handle_response(
        make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC,
                      {make_param_set(2, ControlMode::Scheduled), make_param_set(9, ControlMode::Dynamic)}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.service_id == ServiceCategory::AC);
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 9);
}

SCENARIO("ISO15118-20 EV ServiceDetail finds Dynamic set encoded as narrow int8_t") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::DC,
                                         {make_param_set_narrow<int8_t>(5, ControlMode::Scheduled),
                                          make_param_set_narrow<int8_t>(7, ControlMode::Dynamic)}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 7);
}

SCENARIO("ISO15118-20 EV ServiceDetail finds Dynamic set encoded as narrow int16_t") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::DC,
                                         {make_param_set_narrow<int16_t>(5, ControlMode::Scheduled),
                                          make_param_set_narrow<int16_t>(7, ControlMode::Dynamic)}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 7);
}

SCENARIO("ISO15118-20 EV ServiceDetail stops session when only Scheduled offered") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::DC,
                                         {make_param_set(5, ControlMode::Scheduled)}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceDetail);
    REQUIRE(primed.ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV ServiceDetail stops session on empty parameter list") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, no_seed};

    auto res = make_dc_response(SESSION_HEADER, ResponseCode::OK);
    res.service_parameter_list.clear();
    primed.handle_response(res);
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceDetail);
    REQUIRE(primed.ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV ServiceDetail rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ServiceDetail>()};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_dc_response(header, ResponseCode::OK); };
    check_rejection_paths(callbacks, ev::d20::StateID::ServiceDetail, make_fsm, make_ok,
                          message_20::ServiceSelectionResponse{});
}

SCENARIO("ISO15118-20 EV ServiceDetail selects the first AC_DER_IEC set whose functions are a subset") {
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks,
                          {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                          ServiceCategory::AC_DER_IEC,
                          dso_setpoint_support(),
                          true};
    auto& ctx = helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    auto fsm = fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ServiceDetail>()};

    // id 5 demands VoltWattMode (not supported); id 7 demands only DSOQSetpointProvision (subset).
    helper.handle_response(make_response(
        SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC_DER_IEC,
        {make_der_param_set(5, ControlMode::Dynamic, der_mask({DERControlName::VoltWattMode})),
         make_der_param_set(7, ControlMode::Dynamic, der_mask({DERControlName::DSOQSetpointProvision}))}));
    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(ctx.is_session_stopped() == false);
    REQUIRE(ctx.der_negotiated_functions().test(static_cast<size_t>(DERControlName::DSOQSetpointProvision)));

    const auto requests = take_all_requests(helper.get_message_exchange());
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.service_id == ServiceCategory::AC_DER_IEC);
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 7);
}

SCENARIO("ISO15118-20 EV ServiceDetail stops the session when no AC_DER_IEC set is a subset and strict") {
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks,
                          {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                          ServiceCategory::AC_DER_IEC,
                          dso_setpoint_support(),
                          true};
    auto& ctx = helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    auto fsm = fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ServiceDetail>()};

    helper.handle_response(
        make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC_DER_IEC,
                      {make_der_param_set(5, ControlMode::Dynamic, der_mask({DERControlName::VoltWattMode}))}));
    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceDetail);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV ServiceDetail selects the first Dynamic set on unsupported functions when not strict") {
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks,
                          {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                          ServiceCategory::AC_DER_IEC,
                          dso_setpoint_support(),
                          false};
    auto& ctx = helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    auto fsm = fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ServiceDetail>()};

    helper.handle_response(make_response(
        SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC_DER_IEC,
        {make_der_param_set(5, ControlMode::Dynamic,
                            der_mask({DERControlName::VoltWattMode, DERControlName::DSOQSetpointProvision}))}));
    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(ctx.is_session_stopped() == false);
    // Only the supported bit of the offered mask is recorded as negotiated.
    REQUIRE(ctx.der_negotiated_functions().test(static_cast<size_t>(DERControlName::DSOQSetpointProvision)));
    REQUIRE_FALSE(ctx.der_negotiated_functions().test(static_cast<size_t>(DERControlName::VoltWattMode)));

    const auto requests = take_all_requests(helper.get_message_exchange());
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 5);
}

SCENARIO("ISO15118-20 EV ServiceDetail matches an AC_DER_IEC mask encoded as a narrow int8_t") {
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks,
                          {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                          ServiceCategory::AC_DER_IEC,
                          dso_setpoint_support(),
                          true};
    auto& ctx = helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    auto fsm = fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ServiceDetail>()};

    // Both masks narrow-encoded; id 5 incompatible, id 7 compatible (subset). The masks
    // stay within a signed 8-bit value (bit positions <= 6) so the narrow encoding is faithful.
    helper.handle_response(make_response(
        SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC_DER_IEC,
        {make_der_param_set<int8_t>(5, ControlMode::Dynamic, der_mask({DERControlName::VoltWattMode})),
         make_der_param_set<int8_t>(7, ControlMode::Dynamic, der_mask({DERControlName::DSOQSetpointProvision}))}));
    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(ctx.is_session_stopped() == false);

    const auto requests = take_all_requests(helper.get_message_exchange());
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 7);
}
