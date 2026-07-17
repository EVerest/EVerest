// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
//
// Scripted FSM-level test driving the EVCC ISO 15118-2 DC branch from SessionSetup through the
// CurrentDemand loop and an EVSE-initiated StopCharging down to SessionStop. The canned SECC responses
// are serialized via message_2::serialize and decoded back through message_2::Variant, i.e. they go
// through the exact same EXI codec used on the wire. Mirrors ev_dc_charge.cpp.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/d2/context.hpp>
#include <iso15118/d2/ev/state/session_setup.hpp>
#include <iso15118/d2/ev/states.hpp>
#include <iso15118/d20/timeout.hpp>

#include <iso15118/io/logging.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/cable_check.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/current_demand.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/power_delivery.hpp>
#include <iso15118/message_2/pre_charge.hpp>
#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/session_setup.hpp>
#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/message_2/variant.hpp>
#include <iso15118/message_2/welding_detection.hpp>
#include <iso15118/session/logger.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;
using Event = d2::ev::Event;
using StateID = d2::ev::StateID;

namespace {

const auto ASSIGNED_SESSION_ID = dt::SessionId{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02};

class EvFsmHelper {
public:
    EvFsmHelper(d2::ev::EvSessionConfig config, const session::ev::feedback::Callbacks& callbacks) :
        log(this), ctx(callbacks, log, std::move(config), active_control_event, msg_exch, timeouts) {
        session::logging::set_session_log_callback([](std::size_t, const session::logging::Event&) {});
        io::set_logging_callback([](LogLevel, std::string) {});
    }

    d2::ev::Context& get_context() {
        return ctx;
    }

    template <typename Req> std::optional<Req> take_request() {
        return msg_exch.get_response<Req>();
    }

    template <typename Res> void inject_response(const Res& res) {
        msg_exch.check_and_clear_response(); // pretend the pending request has been sent

        std::array<uint8_t, 4096> buffer{};
        io::StreamOutputView view{buffer.data(), buffer.size()};
        const auto len = message_2::serialize(res, view);

        auto variant = std::make_unique<message_2::Variant>(io::StreamInputView{buffer.data(), len});
        REQUIRE(variant->get_type() != message_2::Type::None);
        msg_exch.set_request(std::move(variant));
    }

    void set_active_control_event(const std::optional<d2::ev::ControlEvent>& event) {
        active_control_event = event;
    }

private:
    std::array<uint8_t, 4096> output_buffer{};
    io::StreamOutputView output_view{output_buffer.data(), output_buffer.size()};
    d2::MessageExchange msg_exch{output_view};
    std::optional<d2::ev::ControlEvent> active_control_event{std::nullopt};
    session::SessionLogger log;
    d20::Timeouts timeouts;
    d2::ev::Context ctx;
};

d2::ev::EvSessionConfig make_dc_config() {
    d2::ev::EvSessionConfig config;
    config.requested_energy_transfer_mode = dt::EnergyTransferMode::DC_extended;
    config.dc_target_voltage = 400.0f;
    config.dc_target_current = 20.0f;
    config.dc_ev_max_voltage = 900.0f;
    config.dc_ev_max_current = 300.0f;
    return config;
}

message_2::Header make_header() {
    message_2::Header header;
    header.session_id = ASSIGNED_SESSION_ID;
    return header;
}

dt::DC_EVSEStatus make_evse_status(dt::EVSENotification notification = dt::EVSENotification::None) {
    dt::DC_EVSEStatus status;
    status.notification_max_delay = 0;
    status.notification = notification;
    status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    status.isolation_status = dt::IsolationLevel::Valid;
    return status;
}

dt::DC_EVSEChargeParameter make_dc_evse_charge_parameter() {
    dt::DC_EVSEChargeParameter dc;
    dc.dc_evse_status = make_evse_status();
    dc.evse_maximum_current_limit = dt::to_physical_value(400.0, dt::Unit::A);
    dc.evse_maximum_power_limit = dt::to_physical_value(360000.0, dt::Unit::W);
    dc.evse_maximum_voltage_limit = dt::to_physical_value(920.0, dt::Unit::V);
    dc.evse_minimum_current_limit = dt::to_physical_value(0.0, dt::Unit::A);
    dc.evse_minimum_voltage_limit = dt::to_physical_value(0.0, dt::Unit::V);
    dc.evse_peak_current_ripple = dt::to_physical_value(1.0, dt::Unit::A);
    return dc;
}

message_2::ChargeParameterDiscoveryResponse make_cpd_response(dt::EVSEProcessing processing) {
    message_2::ChargeParameterDiscoveryResponse res;
    res.header = make_header();
    res.response_code = dt::ResponseCode::OK;
    res.evse_processing = processing;
    if (processing == dt::EVSEProcessing::Finished) {
        dt::SAScheduleTuple tuple;
        tuple.sa_schedule_tuple_id = 1;
        dt::PMaxScheduleEntry entry;
        entry.start = 0;
        entry.p_max = dt::to_physical_value(360000.0, dt::Unit::W);
        tuple.pmax_schedule.push_back(entry);
        dt::SAScheduleList list;
        list.push_back(tuple);
        res.sa_schedule_list = list;
        res.dc_evse_charge_parameter = make_dc_evse_charge_parameter();
    }
    return res;
}

message_2::CurrentDemandResponse make_current_demand_response(dt::EVSENotification notification) {
    message_2::CurrentDemandResponse res;
    res.header = make_header();
    res.response_code = dt::ResponseCode::OK;
    res.dc_evse_status = make_evse_status(notification);
    res.evse_present_voltage = dt::to_physical_value(400.0, dt::Unit::V);
    res.evse_present_current = dt::to_physical_value(125.0, dt::Unit::A);
    res.evse_current_limit_achieved = false;
    res.evse_voltage_limit_achieved = false;
    res.evse_power_limit_achieved = false;
    res.evse_id = "everest se";
    res.sa_schedule_tuple_id = 1;
    return res;
}

} // namespace

SCENARIO("EVCC ISO-2 DC FSM drives the full DC charging flow against canned SECC responses") {

    std::vector<std::string> feedback_order;

    session::ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&](bool ready) {
        if (ready) {
            feedback_order.emplace_back("ev_power_ready");
        }
    };
    callbacks.dc_power_on = [&]() { feedback_order.emplace_back("dc_power_on"); };
    callbacks.stop_from_charger = [&]() { feedback_order.emplace_back("stop_from_charger"); };
    bool present_limits_seen = false;
    callbacks.dc_evse_present_limits = [&](const session::ev::feedback::DcMaximumLimits&) {
        present_limits_seen = true;
    };
    std::string selected_protocol;
    callbacks.selected_protocol = [&](const std::string& p) { selected_protocol = p; };
    bool charge_loop_started = false;
    callbacks.signal = [&](session::ev::feedback::Signal signal) {
        if (signal == session::ev::feedback::Signal::CHARGE_LOOP_STARTED) {
            charge_loop_started = true;
        }
    };
    std::string evse_id;
    callbacks.evse_id = [&](const std::string& id) { evse_id = id; };

    EvFsmHelper helper(make_dc_config(), callbacks);
    auto& ctx = helper.get_context();

    fsm::v2::FSM<d2::ev::StateBase> fsm{ctx.create_state<d2::ev::state::SessionSetup>()};

    // --- SessionSetup ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_2::SessionSetupRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->header.session_id == dt::SessionId{0, 0, 0, 0, 0, 0, 0, 0});
    }
    {
        message_2::SessionSetupResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK_NewSessionEstablished;
        res.evse_id = "everest se";
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ServiceDiscovery);
    }
    REQUIRE(evse_id == "everest se");

    // --- ServiceDiscovery ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_2::ServiceDiscoveryRequest>().has_value());
    {
        message_2::ServiceDiscoveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.payment_option_list.push_back(dt::PaymentOption::ExternalPayment);
        res.charge_service.service_id = 1;
        res.charge_service.service_category = dt::ServiceCategory::EVCharging;
        res.charge_service.free_service = true;
        res.charge_service.supported_energy_transfer_mode.push_back(dt::EnergyTransferMode::DC_extended);
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PaymentServiceSelection);
    }
    REQUIRE(selected_protocol == "ISO15118-2:DC");

    // --- PaymentServiceSelection ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_2::PaymentServiceSelectionRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->selected_payment_option == dt::PaymentOption::ExternalPayment);
    }
    {
        message_2::PaymentServiceSelectionResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::Authorization);
    }

    // --- Authorization (Ongoing then Finished) ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_2::AuthorizationRequest>().has_value());
    {
        message_2::AuthorizationResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EVSEProcessing::Ongoing;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::Authorization);
        REQUIRE(helper.take_request<message_2::AuthorizationRequest>().has_value());
    }
    {
        message_2::AuthorizationResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EVSEProcessing::Finished;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ChargeParameterDiscovery);
    }

    // --- ChargeParameterDiscovery (Ongoing then Finished) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_2::ChargeParameterDiscoveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->dc_ev_charge_parameter.has_value());
    }
    {
        helper.inject_response(make_cpd_response(dt::EVSEProcessing::Ongoing));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ChargeParameterDiscovery);
        REQUIRE(helper.take_request<message_2::ChargeParameterDiscoveryRequest>().has_value());
    }
    {
        helper.inject_response(make_cpd_response(dt::EVSEProcessing::Finished));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::CableCheck);
    }
    REQUIRE(present_limits_seen);

    // --- CableCheck (Ongoing then Finished) ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_2::CableCheckRequest>().has_value());
    {
        message_2::CableCheckResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status = make_evse_status();
        res.evse_processing = dt::EVSEProcessing::Ongoing;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::CableCheck);
    }
    {
        message_2::CableCheckResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status = make_evse_status();
        res.evse_processing = dt::EVSEProcessing::Finished;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PreCharge);
    }

    // --- PreCharge (converge -> advance) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_2::PreChargeRequest>();
        REQUIRE(req.has_value());
        REQUIRE(dt::from_physical_value(req->ev_target_voltage) == 400.0);
    }
    {
        message_2::PreChargeResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status = make_evse_status();
        res.evse_present_voltage = dt::to_physical_value(400.0, dt::Unit::V);
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    }

    // --- PowerDelivery(Start) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_2::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::ChargeProgress::Start);
        REQUIRE(req->sa_schedule_tuple_id == 1);
        REQUIRE(req->dc_ev_power_delivery_parameter.has_value());
    }
    {
        message_2::PowerDeliveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::CurrentDemand);
    }

    // --- CurrentDemand (a few loops, then EVSE StopCharging) ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_2::CurrentDemandRequest>().has_value());
    for (int i = 0; i < 3; ++i) {
        helper.inject_response(make_current_demand_response(dt::EVSENotification::None));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::CurrentDemand);
        REQUIRE(helper.take_request<message_2::CurrentDemandRequest>().has_value());
    }
    REQUIRE(charge_loop_started);
    {
        helper.inject_response(make_current_demand_response(dt::EVSENotification::StopCharging));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    }

    // --- PowerDelivery(Stop) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_2::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::ChargeProgress::Stop);
        REQUIRE(req->dc_ev_power_delivery_parameter->charging_complete);
    }
    {
        message_2::PowerDeliveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::WeldingDetection);
    }

    // --- WeldingDetection (cycles then SessionStop) ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_2::WeldingDetectionRequest>().has_value());
    bool reached_session_stop = false;
    for (int i = 0; i < 5 and not reached_session_stop; ++i) {
        message_2::WeldingDetectionResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status = make_evse_status();
        res.evse_present_voltage = dt::to_physical_value(400.0, dt::Unit::V);
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        if (result.transitioned()) {
            reached_session_stop = true;
        }
    }
    REQUIRE(reached_session_stop);
    REQUIRE(fsm.get_current_state_id() == StateID::SessionStop);

    // --- SessionStop (Terminate) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_2::SessionStopRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charging_session == dt::ChargingSession::Terminate);
    }
    {
        message_2::SessionStopResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(ctx.session_stopped);
        REQUIRE(not ctx.session_paused);
    }

    // Feedback: ev_power_ready (at ChargeParameterDiscovery-Finished) then dc_power_on before the
    // charger-initiated stop (Josev parity ordering).
    REQUIRE(feedback_order == std::vector<std::string>{"ev_power_ready", "dc_power_on", "stop_from_charger"});
}
