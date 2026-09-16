// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <bitset>
#include <string>
#include <utility>
#include <vector>

#include "helper.hpp"

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/ev/d20/state/service_detail.hpp>
#include <iso15118/ev/der_control_functions.hpp>
#include <iso15118/io/log_levels.hpp>
#include <iso15118/io/logging.hpp>
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

// Parameter set with an explicit Connector value, so connector preference can be exercised.
ParameterSet make_param_set(uint16_t id, ControlMode control_mode, int32_t connector) {
    auto set = make_param_set(id, control_mode);
    set.parameter[0] = {"Connector", connector};
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

// A DER parameter set with an explicit Connector value.
ParameterSet make_der_param_set_on(uint16_t id, ControlMode control_mode, int32_t connector,
                                   std::bitset<ev::DER_CONTROL_FUNCTION_COUNT> der_mask) {
    auto set = make_der_param_set(id, control_mode, der_mask);
    set.parameter[0] = {"Connector", connector};
    return set;
}

// Parameter set carrying a raw DERControlFunctions bitmask, so a SECC advertising bits at or
// above the width the EV models can be exercised.
ParameterSet make_der_param_set_raw(uint16_t id, ControlMode control_mode, int32_t der_functions) {
    ParameterSet set{};
    set.id = id;
    set.parameter.push_back({"Connector", static_cast<int32_t>(1)});
    set.parameter.push_back({"ControlMode", static_cast<int32_t>(control_mode)});
    set.parameter.push_back({"EVSENominalVoltage", static_cast<int32_t>(230)});
    set.parameter.push_back({"DERControlFunctions", der_functions});
    return set;
}

// The lowest bit position the EV models no function for.
constexpr int32_t UNKNOWN_FUNCTION_BIT = int32_t{1} << ev::DER_CONTROL_FUNCTION_COUNT;

int32_t raw_der_mask(std::initializer_list<DERControlName> functions) {
    return static_cast<int32_t>(der_mask(functions).to_ulong());
}

// An EV supporting the two DSO setpoint functions, requesting the AC_DER_IEC service.
ev::DerControlFunctions dso_setpoint_support() {
    ev::DerControlFunctions functions{};
    functions.dso_q_setpoint_provision = true;
    functions.dso_cos_phi_setpoint_provision = true;
    return functions;
}

// An EV supporting every IEC DER control function.
ev::DerControlFunctions all_der_support() {
    ev::DerControlFunctions functions{};
    functions.over_frequency_watt_mode = true;
    functions.under_frequency_watt_mode = true;
    functions.volt_watt_mode = true;
    functions.volt_var_mode = true;
    functions.watt_var_mode = true;
    functions.watt_cos_phi_mode = true;
    functions.dso_q_setpoint_provision = true;
    functions.dso_cos_phi_setpoint_provision = true;
    functions.dc_injection_restriction = true;
    functions.zero_current_mode = true;
    functions.over_voltage_fault_ride_through_mode = true;
    functions.under_voltage_fault_ride_through_mode = true;
    return functions;
}

// Seeds an AC session for an EV drawing on \p phase_count lines.
auto seed_ac_lines(uint8_t phase_count) {
    return [phase_count](FsmStateHelper& helper) {
        ev::AcChargeParams params{};
        params.phase_count = phase_count;
        helper.set_ac_params(params);
    };
}

constexpr int32_t SINGLE_PHASE = message_20::to_underlying_value(message_20::datatypes::AcConnector::SinglePhase);
constexpr int32_t THREE_PHASE = message_20::to_underlying_value(message_20::datatypes::AcConnector::ThreePhase);

uint16_t selected_parameter_set_id(ev::d20::MessageExchange& msg_exch) {
    const auto requests = take_all_requests(msg_exch);
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    return request_message->selected_energy_transfer_service.parameter_set_id;
}

// Collects libiso15118 log lines while alive, then restores a no-op callback so a later
// scenario cannot log into freed storage.
class LogCapture {
public:
    LogCapture() {
        io::set_logging_callback(
            [this](LogLevel level, std::string message) { lines.emplace_back(level, std::move(message)); });
    }

    ~LogCapture() {
        io::set_logging_callback([](LogLevel, const std::string&) {});
    }

    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    bool has_warning_containing(const std::string& needle) const {
        for (const auto& [level, message] : lines) {
            if (level == LogLevel::Warning and message.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

private:
    std::vector<std::pair<LogLevel, std::string>> lines;
};
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
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, ServiceCategory::AC, no_seed};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceDetailRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->service == message_20::to_underlying_value(ServiceCategory::AC));
}

SCENARIO("ISO15118-20 EV ServiceDetail selects the Dynamic parameter set for AC") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, ServiceCategory::AC, no_seed};

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

    expect_stops_session(primed,
                         make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::DC,
                                       {make_param_set(5, ControlMode::Scheduled)}),
                         ev::d20::StateID::ServiceDetail);
}

SCENARIO("ISO15118-20 EV ServiceDetail stops session on empty parameter list") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, no_seed};

    auto res = make_dc_response(SESSION_HEADER, ResponseCode::OK);
    res.service_parameter_list.clear();
    expect_stops_session(primed, res, ev::d20::StateID::ServiceDetail);
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

    expect_stops_session(
        helper, fsm,
        make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC_DER_IEC,
                      {make_der_param_set(5, ControlMode::Dynamic, der_mask({DERControlName::VoltWattMode}))}),
        ev::d20::StateID::ServiceDetail);
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

SCENARIO("ISO15118-20 EV ServiceDetail stops on AC_DER_IEC functions above the supported width when strict") {
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks,
                          {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                          ServiceCategory::AC_DER_IEC,
                          dso_setpoint_support(),
                          true};
    auto& ctx = helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    auto fsm = fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ServiceDetail>()};

    // The only Dynamic set demands a function the EV models no bit for.
    expect_stops_session(helper, fsm,
                         make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC_DER_IEC,
                                       {make_der_param_set_raw(5, ControlMode::Dynamic, UNKNOWN_FUNCTION_BIT)}),
                         ev::d20::StateID::ServiceDetail);
}

SCENARIO("ISO15118-20 EV ServiceDetail warns on AC_DER_IEC functions above the supported width when not strict") {
    const LogCapture logs{};
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks,
                          {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                          ServiceCategory::AC_DER_IEC,
                          dso_setpoint_support(),
                          false};
    auto& ctx = helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    auto fsm = fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ServiceDetail>()};

    helper.handle_response(
        make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC_DER_IEC,
                      {make_der_param_set_raw(5, ControlMode::Dynamic,
                                              UNKNOWN_FUNCTION_BIT | raw_der_mask({DERControlName::VoltWattMode}))}));
    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(ctx.is_session_stopped() == false);
    REQUIRE(logs.has_warning_containing("unknown function bits"));
    REQUIRE(ctx.der_negotiated_functions().none());

    const auto requests = take_all_requests(helper.get_message_exchange());
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 5);
}

SCENARIO("ISO15118-20 EV ServiceDetail skips an AC_DER_IEC set with functions above the supported width") {
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks,
                          {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                          ServiceCategory::AC_DER_IEC,
                          dso_setpoint_support(),
                          true};
    auto& ctx = helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    auto fsm = fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ServiceDetail>()};

    // id 5 pairs a supported function with a bit the EV models nothing for; id 7 is a clean subset.
    helper.handle_response(make_response(
        SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC_DER_IEC,
        {make_der_param_set_raw(5, ControlMode::Dynamic,
                                UNKNOWN_FUNCTION_BIT | raw_der_mask({DERControlName::DSOQSetpointProvision})),
         make_der_param_set_raw(7, ControlMode::Dynamic, raw_der_mask({DERControlName::DSOCosPhiSetpointProvision}))}));
    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(ctx.is_session_stopped() == false);
    REQUIRE(ctx.der_negotiated_functions().test(static_cast<size_t>(DERControlName::DSOCosPhiSetpointProvision)));

    const auto requests = take_all_requests(helper.get_message_exchange());
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 7);
}

SCENARIO("ISO15118-20 EV ServiceDetail treats a non-integer DERControlFunctions value as unknown functions") {
    const LogCapture logs{};
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks,
                          {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                          ServiceCategory::AC_DER_IEC,
                          dso_setpoint_support(),
                          true};
    auto& ctx = helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    auto fsm = fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ServiceDetail>()};

    // The only Dynamic set carries a DERControlFunctions value that is not an integer.
    auto set = make_param_set(5, ControlMode::Dynamic);
    set.parameter.push_back({"DERControlFunctions", true});
    helper.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC_DER_IEC, {set}));
    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceDetail);
    REQUIRE(ctx.is_session_stopped() == true);
    REQUIRE(logs.has_warning_containing("non-integer DERControlFunctions"));
}

SCENARIO("ISO15118-20 EV ServiceDetail selects the first Dynamic set on a non-integer DERControlFunctions value when "
         "not strict") {
    const LogCapture logs{};
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks,
                          {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                          ServiceCategory::AC_DER_IEC,
                          dso_setpoint_support(),
                          false};
    auto& ctx = helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    auto fsm = fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ServiceDetail>()};

    auto set = make_param_set(5, ControlMode::Dynamic);
    set.parameter.push_back({"DERControlFunctions", true});
    helper.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC_DER_IEC, {set}));
    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(ctx.is_session_stopped() == false);
    REQUIRE(logs.has_warning_containing("non-integer DERControlFunctions"));
    REQUIRE(ctx.der_negotiated_functions().none());

    const auto requests = take_all_requests(helper.get_message_exchange());
    const auto request_message = requests.get<message_20::ServiceSelectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->selected_energy_transfer_service.parameter_set_id == 5);
}

SCENARIO("ISO15118-20 EV ServiceDetail leaves the AC connector unset for DC and keeps set order") {
    const LogCapture logs{};
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, no_seed};

    // DC sets carry a "Connector" too (DcConnector::Core = 1, Dual2 = 3); it is not an AC connector.
    primed.handle_response(
        make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::DC,
                      {make_param_set(1, ControlMode::Dynamic, 1), make_param_set(2, ControlMode::Dynamic, 3)}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(selected_parameter_set_id(primed.helper.get_message_exchange()) == 1);
    REQUIRE_FALSE(primed.ctx.selected_ac_connector().has_value());
    REQUIRE_FALSE(logs.has_warning_containing("preferred"));
}

SCENARIO("ISO15118-20 EV ServiceDetail prefers the ThreePhase set for a three-line EV") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, ServiceCategory::AC, seed_ac_lines(3)};

    primed.handle_response(make_response(
        SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC,
        {make_param_set(1, ControlMode::Dynamic, SINGLE_PHASE), make_param_set(2, ControlMode::Dynamic, THREE_PHASE)}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(selected_parameter_set_id(primed.helper.get_message_exchange()) == 2);
    REQUIRE(primed.ctx.selected_ac_connector() == message_20::datatypes::AcConnector::ThreePhase);
}

SCENARIO("ISO15118-20 EV ServiceDetail prefers the SinglePhase set for a one-line EV") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, ServiceCategory::AC, seed_ac_lines(1)};

    primed.handle_response(make_response(
        SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC,
        {make_param_set(1, ControlMode::Dynamic, SINGLE_PHASE), make_param_set(2, ControlMode::Dynamic, THREE_PHASE)}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(selected_parameter_set_id(primed.helper.get_message_exchange()) == 1);
    REQUIRE(primed.ctx.selected_ac_connector() == message_20::datatypes::AcConnector::SinglePhase);
}

SCENARIO("ISO15118-20 EV ServiceDetail falls back to SinglePhase with a warning when ThreePhase is not offered") {
    const LogCapture logs{};
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, ServiceCategory::AC, seed_ac_lines(3)};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC,
                                         {make_param_set(1, ControlMode::Dynamic, SINGLE_PHASE)}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(selected_parameter_set_id(primed.helper.get_message_exchange()) == 1);
    REQUIRE(primed.ctx.selected_ac_connector() == message_20::datatypes::AcConnector::SinglePhase);
    REQUIRE(logs.has_warning_containing("preferred ThreePhase"));
}

SCENARIO("ISO15118-20 EV ServiceDetail records the DER mask of the set it selects") {
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks,
                          {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                          ServiceCategory::AC_DER_IEC,
                          all_der_support(),
                          true};
    auto& ctx = helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    ev::AcChargeParams params{};
    params.phase_count = 3;
    helper.set_ac_params(params);
    auto fsm = fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ServiceDetail>()};

    // Both sets are acceptable and neither is on the preferred connector, so the first one is
    // selected; its mask, not the last one scanned, is what was negotiated.
    const std::bitset<ev::DER_CONTROL_FUNCTION_COUNT> first_mask{0b11};
    const std::bitset<ev::DER_CONTROL_FUNCTION_COUNT> second_mask{0b1};
    helper.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC_DER_IEC,
                                         {make_der_param_set_on(1, ControlMode::Dynamic, SINGLE_PHASE, first_mask),
                                          make_der_param_set_on(2, ControlMode::Dynamic, SINGLE_PHASE, second_mask)}));
    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(selected_parameter_set_id(helper.get_message_exchange()) == 1);
    REQUIRE(ctx.der_negotiated_functions() == first_mask);
    REQUIRE(ctx.selected_ac_connector() == message_20::datatypes::AcConnector::SinglePhase);
}
