// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
//
// Scripted FSM-level test driving the EVCC DC branch from DC_ChargeParameterDiscovery through the
// charge loop and an EVSE-initiated Terminate down to SessionStop. The canned SECC responses are
// serialized via message_20::serialize and decoded back through message_20::Variant, i.e. they go
// through the exact same EXI codec that is used on the wire. The harness mirrors ev_handshake.cpp.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <optional>
#include <variant>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/d20/ev/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/d20/ev/states.hpp>
#include <iso15118/d20/timeout.hpp>

#include <iso15118/io/logging.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/dc_welding_detection.hpp>
#include <iso15118/message/payload_type.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/variant.hpp>
#include <iso15118/session/logger.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;
using Event = d20::ev::Event;
using StateID = d20::ev::StateID;

namespace {

const auto ASSIGNED_SESSION_ID = dt::SessionId{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02};

class EvFsmHelper {
public:
    EvFsmHelper(session::EvSessionConfig config, const session::ev::feedback::Callbacks& callbacks) :
        log(this), ctx(callbacks, log, std::move(config), active_control_event, msg_exch, timeouts) {
        session::logging::set_session_log_callback([](std::size_t, const session::logging::Event&) {});
        io::set_logging_callback([](LogLevel, std::string) {});
    }

    d20::ev::Context& get_context() {
        return ctx;
    }

    template <typename Req> std::optional<Req> take_request() {
        return msg_exch.get_response<Req>();
    }

    template <typename Res> void inject_response(const Res& res) {
        msg_exch.check_and_clear_response(); // pretend the pending request has been sent

        std::array<uint8_t, 4096> buffer{};
        io::StreamOutputView view{buffer.data(), buffer.size()};
        const auto len = message_20::serialize(res, view);

        const io::v2gtp::PayloadType payload_type = message_20::PayloadTypeTrait<Res>::type;
        auto variant = std::make_unique<message_20::Variant>(payload_type, io::StreamInputView{buffer.data(), len});
        REQUIRE(variant->get_type() != message_20::Type::None);
        msg_exch.set_request(std::move(variant));
    }

private:
    std::array<uint8_t, 4096> output_buffer{};
    io::StreamOutputView output_view{output_buffer.data(), output_buffer.size()};
    d20::MessageExchange msg_exch{output_view};
    std::optional<d20::ev::ControlEvent> active_control_event{std::nullopt};
    session::SessionLogger log;
    d20::Timeouts timeouts;
    d20::ev::Context ctx;
};

session::EvSessionConfig make_dc_config() {
    session::EvSetupConfig setup;
    setup.evcc_id = "WMIV1234567890ABCDEX";
    setup.supported_energy_services = {dt::ServiceCategory::DC};
    setup.preferred_control_mode = dt::ControlMode::Dynamic;
    setup.supported_auth_options = {dt::Authorization::EIM};

    auto& dc = setup.dc_charge_parameters;
    dc.max_charge_power = dt::from_float(150000.0f);
    dc.min_charge_power = dt::from_float(100.0f);
    dc.max_charge_current = dt::from_float(300.0f);
    dc.min_charge_current = dt::from_float(10.0f);
    dc.max_voltage = dt::from_float(900.0f);
    dc.min_voltage = dt::from_float(10.0f);
    dc.target_voltage = dt::from_float(400.0f);
    dc.target_current = dt::from_float(20.0f);
    dc.energy_capacity = dt::from_float(60000.0f);

    return session::EvSessionConfig(setup);
}

message_20::Header make_header() {
    message_20::Header header;
    header.session_id = ASSIGNED_SESSION_ID;
    header.timestamp = 1691411798;
    return header;
}

} // namespace

SCENARIO("EVCC DC FSM drives the full DC charging flow against canned SECC responses") {

    // Track the feedback ordering contract [flow spec §4].
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
    bool charge_loop_started = false;
    callbacks.signal = [&](session::ev::feedback::Signal signal) {
        if (signal == session::ev::feedback::Signal::CHARGE_LOOP_STARTED) {
            charge_loop_started = true;
        }
    };

    EvFsmHelper helper(make_dc_config(), callbacks);
    auto& ctx = helper.get_context();

    // Pre-set the state that the handshake would have negotiated (covered by ev_handshake.cpp).
    ctx.set_session_id(ASSIGNED_SESSION_ID);
    ctx.evse_info.selected_energy_service = dt::ServiceCategory::DC;
    ctx.evse_info.selected_control_mode = dt::ControlMode::Dynamic;
    ctx.evse_info.selected_parameter_set_id = 1;

    fsm::v2::FSM<d20::ev::StateBase> fsm{ctx.create_state<d20::ev::state::DC_ChargeParameterDiscovery>()};

    // --- DC_ChargeParameterDiscovery ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::DC_ChargeParameterDiscoveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(std::holds_alternative<dt::DC_CPDReqEnergyTransferMode>(req->transfer_mode));
    }
    {
        message_20::DC_ChargeParameterDiscoveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        auto& mode = res.transfer_mode.emplace<dt::DC_CPDResEnergyTransferMode>();
        mode.max_charge_power = dt::from_float(360000.0f);
        mode.max_charge_current = dt::from_float(400.0f);
        mode.max_voltage = dt::from_float(920.0f);
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ScheduleExchange);
    }
    REQUIRE(present_limits_seen);

    // --- ScheduleExchange ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::ScheduleExchangeRequest>();
        REQUIRE(req.has_value());
        REQUIRE(std::holds_alternative<dt::Dynamic_SEReqControlMode>(req->control_mode));
        const auto& mode = std::get<dt::Dynamic_SEReqControlMode>(req->control_mode);
        REQUIRE(mode.departure_time == 7200);
    }
    {
        message_20::ScheduleExchangeResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.processing = dt::Processing::Finished;
        res.control_mode.emplace<dt::Dynamic_SEResControlMode>();
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::DC_CableCheck);
    }

    // --- DC_CableCheck (ongoing then finished) ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_20::DC_CableCheckRequest>().has_value());
    {
        message_20::DC_CableCheckResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.processing = dt::Processing::Ongoing;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::DC_CableCheck);
        REQUIRE(helper.take_request<message_20::DC_CableCheckRequest>().has_value());
    }
    {
        message_20::DC_CableCheckResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.processing = dt::Processing::Finished;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::DC_PreCharge);
    }

    // --- DC_PreCharge (converge -> send Finished -> advance) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::DC_PreChargeRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->processing == dt::Processing::Ongoing);
    }
    {
        // Present voltage already at 400 V (within +/- 10 % of the 400 V target): converged.
        message_20::DC_PreChargeResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.present_voltage = dt::from_float(400.0f);
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        // The EV now resends with processing=Finished.
        const auto req = helper.take_request<message_20::DC_PreChargeRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->processing == dt::Processing::Finished);
    }
    {
        message_20::DC_PreChargeResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.present_voltage = dt::from_float(400.0f);
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    }

    // --- PowerDelivery(Start) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::Progress::Start);
    }
    {
        message_20::PowerDeliveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::DC_ChargeLoop);
    }

    // --- DC_ChargeLoop (a few loops, then an EVSE Terminate notification) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::DC_ChargeLoopRequest>();
        REQUIRE(req.has_value());
        REQUIRE(std::holds_alternative<dt::Dynamic_DC_CLReqControlMode>(req->control_mode));
    }
    for (int i = 0; i < 3; ++i) {
        message_20::DC_ChargeLoopResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.present_voltage = dt::from_float(400.0f);
        res.present_current = dt::from_float(125.0f);
        auto& mode = res.control_mode.emplace<dt::Dynamic_DC_CLResControlMode>();
        mode.max_charge_power = dt::from_float(360000.0f);
        mode.min_charge_power = dt::from_float(0.0f);
        mode.max_charge_current = dt::from_float(400.0f);
        mode.max_voltage = dt::from_float(920.0f);
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::DC_ChargeLoop);
        REQUIRE(helper.take_request<message_20::DC_ChargeLoopRequest>().has_value());
    }
    REQUIRE(charge_loop_started);
    {
        message_20::DC_ChargeLoopResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.present_voltage = dt::from_float(400.0f);
        res.present_current = dt::from_float(125.0f);
        res.status = dt::EvseStatus{0, dt::EvseNotification::Terminate};
        auto& mode = res.control_mode.emplace<dt::Dynamic_DC_CLResControlMode>();
        mode.max_charge_power = dt::from_float(360000.0f);
        mode.min_charge_power = dt::from_float(0.0f);
        mode.max_charge_current = dt::from_float(400.0f);
        mode.max_voltage = dt::from_float(920.0f);
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    }

    // --- PowerDelivery(Stop) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::Progress::Stop);
    }
    {
        message_20::PowerDeliveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::DC_WeldingDetection);
    }

    // --- DC_WeldingDetection (3 Ongoing then Finished) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::DC_WeldingDetectionRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->processing == dt::Processing::Ongoing);
    }
    // Three Ongoing exchanges keep the state; the request stays Ongoing for the first two resends and
    // becomes Finished once the cycle budget is reached.
    for (int i = 0; i < 3; ++i) {
        message_20::DC_WeldingDetectionResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.present_voltage = dt::from_float(400.0f);
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::DC_WeldingDetection);
        REQUIRE(helper.take_request<message_20::DC_WeldingDetectionRequest>().has_value());
    }
    {
        // The last resent request must now be Finished.
        const auto req = helper.take_request<message_20::DC_WeldingDetectionRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->processing == dt::Processing::Finished);

        message_20::DC_WeldingDetectionResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.present_voltage = dt::from_float(400.0f);
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::SessionStop);
    }

    // --- SessionStop ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::SessionStopRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charging_session == dt::ChargingSession::Terminate);
    }
    {
        message_20::SessionStopResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(ctx.session_stopped);
    }

    // Feedback ordering: ev_power_ready -> dc_power_on -> stop_from_charger [flow spec §4].
    REQUIRE(feedback_order == std::vector<std::string>{"ev_power_ready", "dc_power_on", "stop_from_charger"});
}
