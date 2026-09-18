// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "helper.hpp"

#include <iso15118/ev/d20/state/service_detail.hpp>
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

SCENARIO("ISO15118-20 EV ServiceDetail falls back to the Scheduled set when no Dynamic set is offered") {
    const LogCapture logs{};
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::DC,
                                         {make_param_set(5, ControlMode::Scheduled)}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(selected_parameter_set_id(primed.helper.get_message_exchange()) == 5);
    REQUIRE(primed.ctx.selected_control_mode() == ControlMode::Scheduled);
    REQUIRE(logs.has_warning_containing("preferred Dynamic control mode"));
}

SCENARIO("ISO15118-20 EV ServiceDetail stops session when no set carries a readable control mode") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, no_seed};

    auto set = make_param_set(5, ControlMode::Dynamic);
    set.parameter[1] = {"ControlMode", std::string{"Dynamic"}};
    expect_stops_session(primed, make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::DC, {set}),
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

namespace {
ev::d20::SessionOptions prefer(ControlMode mode) {
    ev::d20::SessionOptions options{};
    options.control_mode = mode;
    return options;
}
} // namespace

SCENARIO("ISO15118-20 EV ServiceDetail selects the Scheduled set when Scheduled is preferred") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, ServiceCategory::DC, prefer(ControlMode::Scheduled),
                                                      no_seed};

    primed.handle_response(make_dc_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceSelection);
    REQUIRE(selected_parameter_set_id(primed.helper.get_message_exchange()) == 5);
    REQUIRE(primed.ctx.selected_control_mode() == ControlMode::Scheduled);
}

SCENARIO("ISO15118-20 EV ServiceDetail falls back to the Dynamic set when Scheduled is preferred but not offered") {
    const LogCapture logs{};
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDetail> primed{callbacks, ServiceCategory::DC, prefer(ControlMode::Scheduled),
                                                      no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::DC,
                                         {make_param_set(7, ControlMode::Dynamic)}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(selected_parameter_set_id(primed.helper.get_message_exchange()) == 7);
    REQUIRE(primed.ctx.selected_control_mode() == ControlMode::Dynamic);
    REQUIRE(logs.has_warning_containing("preferred Scheduled control mode"));
}
