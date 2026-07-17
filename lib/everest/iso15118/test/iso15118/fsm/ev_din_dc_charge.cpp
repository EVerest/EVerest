// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
//
// Scripted FSM-level test driving the EVCC DIN SPEC 70121 DC branch from SessionSetup through the
// charge loop down to SessionStop. The canned SECC responses are serialized via message_din::serialize
// and decoded back through message_din::Variant, i.e. they go through the exact same EXI codec that is
// used on the wire. The harness mirrors ev_dc_charge.cpp.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/din/context.hpp>
#include <iso15118/din/ev/config.hpp>
#include <iso15118/din/ev/context.hpp>
#include <iso15118/din/ev/state/session_setup.hpp>
#include <iso15118/din/ev/states.hpp>
#include <iso15118/d20/ev/control_event.hpp>
#include <iso15118/d20/timeout.hpp>

#include <iso15118/io/logging.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message_din/cable_check.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/message_din/contract_authentication.hpp>
#include <iso15118/message_din/current_demand.hpp>
#include <iso15118/message_din/power_delivery.hpp>
#include <iso15118/message_din/pre_charge.hpp>
#include <iso15118/message_din/service_discovery.hpp>
#include <iso15118/message_din/service_payment_selection.hpp>
#include <iso15118/message_din/session_setup.hpp>
#include <iso15118/message_din/session_stop.hpp>
#include <iso15118/message_din/type.hpp>
#include <iso15118/message_din/variant.hpp>
#include <iso15118/message_din/welding_detection.hpp>
#include <iso15118/session/logger.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;
using Event = din::ev::Event;
using StateID = din::ev::StateID;

namespace {

const auto ASSIGNED_SESSION_ID = dt::SessionId{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02};
constexpr uint16_t CHARGE_SERVICE_ID = 1;

class EvFsmHelper {
public:
    EvFsmHelper(din::ev::EvSessionConfig config, const session::ev::feedback::Callbacks& callbacks) :
        log(this), ctx(callbacks, log, std::move(config), active_control_event, msg_exch, timeouts) {
        session::logging::set_session_log_callback([](std::size_t, const session::logging::Event&) {});
        io::set_logging_callback([](LogLevel, std::string) {});
    }

    din::ev::Context& get_context() {
        return ctx;
    }

    template <typename Req> std::optional<Req> take_request() {
        return msg_exch.get_response<Req>();
    }

    template <typename Res> void inject_response(const Res& res) {
        msg_exch.check_and_clear_response(); // pretend the pending request has been sent

        std::array<uint8_t, 4096> buffer{};
        io::StreamOutputView view{buffer.data(), buffer.size()};
        const auto len = message_din::serialize(res, view);

        auto variant = std::make_unique<message_din::Variant>(io::StreamInputView{buffer.data(), len});
        REQUIRE(variant->get_type() != message_din::Type::None);
        msg_exch.set_request(std::move(variant));
    }

    void set_control_event(const d20::ev::ControlEvent& event) {
        active_control_event = event;
    }

    void clear_control_event() {
        active_control_event.reset();
    }

private:
    std::array<uint8_t, 4096> output_buffer{};
    io::StreamOutputView output_view{output_buffer.data(), output_buffer.size()};
    din::MessageExchange msg_exch{output_view};
    std::optional<d20::ev::ControlEvent> active_control_event{std::nullopt};
    session::SessionLogger log;
    d20::Timeouts timeouts;
    din::ev::Context ctx;
};

din::ev::EvSessionConfig make_dc_config() {
    din::ev::EvSessionConfig config;
    config.requested_energy_transfer_type = dt::EnergyTransferMode::DC_extended;
    config.dc.max_current_limit = 300.0;
    config.dc.max_voltage_limit = 900.0;
    config.dc.max_power_limit = 150000.0;
    config.dc.target_voltage = 400.0;
    config.dc.target_current = 125.0;
    config.dc.energy_capacity = 60000.0;
    return config;
}

message_din::Header make_header() {
    message_din::Header header;
    header.session_id = ASSIGNED_SESSION_ID;
    return header;
}

message_din::SessionSetupResponse make_session_setup_res(dt::ResponseCode code) {
    message_din::SessionSetupResponse res;
    res.header = make_header();
    res.response_code = code;
    res.evse_id = {0xAB, 0xCD};
    return res;
}

message_din::CurrentDemandResponse make_current_demand_res(dt::EvseNotification notification) {
    message_din::CurrentDemandResponse res;
    res.header = make_header();
    res.response_code = dt::ResponseCode::OK;
    res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.dc_evse_status.evse_notification = notification;
    res.evse_present_voltage = 400.0;
    res.evse_present_current = 125.0;
    return res;
}

// Drives the FSM from SessionSetup through the handshake until a ChargeParameterDiscoveryReq is pending
// (after one EVSEProcessing-Ongoing repeat). Leaves the FSM in ChargeParameterDiscovery.
void run_handshake_to_charge_parameter_discovery(fsm::v2::FSM<din::ev::StateBase>& fsm, EvFsmHelper& helper) {
    // --- SessionSetup ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_din::SessionSetupRequest>().has_value());
    {
        helper.inject_response(make_session_setup_res(dt::ResponseCode::OK_NewSessionEstablished));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ServiceDiscovery);
    }

    // --- ServiceDiscovery ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_din::ServiceDiscoveryRequest>().has_value());
    {
        message_din::ServiceDiscoveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.charge_service.service_tag.service_id = CHARGE_SERVICE_ID;
        res.charge_service.energy_transfer_type = dt::SupportedEnergyTransferMode::DC_extended;
        res.payment_options = {dt::PaymentOption::ExternalPayment};
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ServicePaymentSelection);
    }

    // --- ServicePaymentSelection ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_din::ServicePaymentSelectionRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->selected_payment_option == dt::PaymentOption::ExternalPayment);
        REQUIRE(req->selected_service_list.size() == 1);
        REQUIRE(req->selected_service_list.front().service_id == CHARGE_SERVICE_ID);
    }
    {
        message_din::ServicePaymentSelectionResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ContractAuthentication);
    }

    // --- ContractAuthentication (Ongoing then Finished) ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_din::ContractAuthenticationRequest>().has_value());
    {
        message_din::ContractAuthenticationResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Ongoing;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ContractAuthentication);
        REQUIRE(helper.take_request<message_din::ContractAuthenticationRequest>().has_value());
    }
    {
        message_din::ContractAuthenticationResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ChargeParameterDiscovery);
    }

    // --- ChargeParameterDiscovery (Ongoing) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_din::ChargeParameterDiscoveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->ev_requested_energy_transfer_type == dt::EnergyTransferMode::DC_extended);
        REQUIRE(req->dc_ev_charge_parameter.has_value());
    }
    {
        message_din::ChargeParameterDiscoveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Ongoing;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(helper.take_request<message_din::ChargeParameterDiscoveryRequest>().has_value());
    }
}

// Drives the FSM from SessionSetup up to (and into) the first CurrentDemand exchange. Includes an
// EVSEProcessing-Ongoing repeat in both ContractAuthentication and ChargeParameterDiscovery, and an
// Ongoing repeat in CableCheck. Leaves the FSM in CurrentDemand with a request pending.
void run_handshake_to_current_demand(fsm::v2::FSM<din::ev::StateBase>& fsm, EvFsmHelper& helper) {
    run_handshake_to_charge_parameter_discovery(fsm, helper);

    // --- ChargeParameterDiscovery (Finished) ---
    {
        message_din::ChargeParameterDiscoveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        dt::DcEvseChargeParameter param;
        param.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        param.evse_maximum_current_limit = 400.0;
        param.evse_maximum_voltage_limit = 920.0;
        param.evse_maximum_power_limit = 360000.0;
        res.dc_evse_charge_parameter = param;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::CableCheck);
    }

    // --- CableCheck (Ongoing then Finished) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_din::CableCheckRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->dc_ev_status.ev_ready);
    }
    {
        message_din::CableCheckResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Ongoing;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_IsolationMonitoringActive;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(helper.take_request<message_din::CableCheckRequest>().has_value());
    }
    {
        message_din::CableCheckResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PreCharge);
    }

    // --- PreCharge (converge immediately) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_din::PreChargeRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->ev_target_voltage == 400.0);
        REQUIRE(req->ev_target_current == 0.0);
    }
    {
        message_din::PreChargeResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.evse_present_voltage = 400.0;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    }

    // --- PowerDelivery(Start) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_din::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->ready_to_charge_state);
    }
    {
        message_din::PowerDeliveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::CurrentDemand);
    }

    // --- CurrentDemand (first request emitted) ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_din::CurrentDemandRequest>().has_value());
}

// Drives PowerDelivery(Stop) -> WeldingDetection (3 cycles) -> SessionStop and returns.
void run_teardown(fsm::v2::FSM<din::ev::StateBase>& fsm, EvFsmHelper& helper) {
    // --- PowerDelivery(Stop) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_din::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(not req->ready_to_charge_state);
    }
    {
        message_din::PowerDeliveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::WeldingDetection);
    }

    // --- WeldingDetection (3 cycles then SessionStop) ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_din::WeldingDetectionRequest>().has_value());
    for (int i = 0; i < 2; ++i) {
        message_din::WeldingDetectionResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.evse_present_voltage = 400.0;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(helper.take_request<message_din::WeldingDetectionRequest>().has_value());
    }
    {
        message_din::WeldingDetectionResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.evse_present_voltage = 400.0;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::SessionStop);
    }

    // --- SessionStop ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_din::SessionStopRequest>().has_value());
    {
        message_din::SessionStopResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        fsm.feed(Event::V2GTP_MESSAGE);
    }
}

} // namespace

SCENARIO("EVCC DIN FSM drives the full DC charging flow with an EV-initiated stop mid-loop") {
    std::vector<std::string> feedback_order;
    session::ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&](bool ready) {
        if (ready) {
            feedback_order.emplace_back("ev_power_ready");
        }
    };
    callbacks.dc_power_on = [&]() { feedback_order.emplace_back("dc_power_on"); };
    bool present_limits_seen = false;
    callbacks.dc_evse_present_limits = [&](const session::ev::feedback::DcMaximumLimits&) { present_limits_seen = true; };
    bool charge_loop_started = false;
    std::string evse_id_seen;
    callbacks.evse_id = [&](const std::string& id) { evse_id_seen = id; };
    callbacks.signal = [&](session::ev::feedback::Signal signal) {
        if (signal == session::ev::feedback::Signal::CHARGE_LOOP_STARTED) {
            charge_loop_started = true;
        }
    };

    EvFsmHelper helper(make_dc_config(), callbacks);
    auto& ctx = helper.get_context();

    fsm::v2::FSM<din::ev::StateBase> fsm{ctx.create_state<din::ev::state::SessionSetup>()};

    run_handshake_to_current_demand(fsm, helper);

    REQUIRE(present_limits_seen);
    REQUIRE(evse_id_seen == "ABCD");
    REQUIRE(ctx.get_session_id() == ASSIGNED_SESSION_ID);

    // Two continue loops.
    for (int i = 0; i < 2; ++i) {
        helper.inject_response(make_current_demand_res(dt::EvseNotification::None));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::CurrentDemand);
        REQUIRE(helper.take_request<message_din::CurrentDemandRequest>().has_value());
    }
    REQUIRE(charge_loop_started);

    // EV-initiated stop: deliver a StopCharging control event, then the next response leaves the loop.
    helper.set_control_event(d20::ev::StopCharging{true});
    fsm.feed(Event::CONTROL_MESSAGE);
    helper.clear_control_event();
    {
        helper.inject_response(make_current_demand_res(dt::EvseNotification::None));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    }

    run_teardown(fsm, helper);

    REQUIRE(ctx.session_stopped);
    REQUIRE(not ctx.session_paused);
    REQUIRE(feedback_order == std::vector<std::string>{"ev_power_ready", "dc_power_on"});
}

SCENARIO("EVCC DIN FSM stops on an SECC-initiated StopCharging notification") {
    session::ev::feedback::Callbacks callbacks{};
    bool stop_from_charger_seen = false;
    callbacks.stop_from_charger = [&]() { stop_from_charger_seen = true; };

    EvFsmHelper helper(make_dc_config(), callbacks);
    auto& ctx = helper.get_context();
    fsm::v2::FSM<din::ev::StateBase> fsm{ctx.create_state<din::ev::state::SessionSetup>()};

    run_handshake_to_current_demand(fsm, helper);

    // SECC signals a stop via EVSENotification StopCharging.
    helper.inject_response(make_current_demand_res(dt::EvseNotification::StopCharging));
    const auto result = fsm.feed(Event::V2GTP_MESSAGE);
    REQUIRE(result.transitioned());
    REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    REQUIRE(stop_from_charger_seen);

    run_teardown(fsm, helper);
    REQUIRE(ctx.session_stopped);
    REQUIRE(not ctx.session_paused);
}

SCENARIO("EVCC DIN FSM pauses on an EV-initiated PauseCharging control event") {
    session::ev::feedback::Callbacks callbacks{};

    EvFsmHelper helper(make_dc_config(), callbacks);
    auto& ctx = helper.get_context();
    fsm::v2::FSM<din::ev::StateBase> fsm{ctx.create_state<din::ev::state::SessionSetup>()};

    run_handshake_to_current_demand(fsm, helper);

    helper.set_control_event(d20::ev::PauseCharging{true});
    fsm.feed(Event::CONTROL_MESSAGE);
    helper.clear_control_event();
    {
        helper.inject_response(make_current_demand_res(dt::EvseNotification::None));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    }

    run_teardown(fsm, helper);

    REQUIRE(ctx.session_paused);
    REQUIRE(not ctx.session_stopped);
}

SCENARIO("EVCC DIN FSM diverts to SessionStop when ChargeParameterDiscovery finishes with StopCharging") {
    session::ev::feedback::Callbacks callbacks{};
    bool ev_power_ready_seen = false;
    callbacks.ev_power_ready = [&](bool ready) {
        if (ready) {
            ev_power_ready_seen = true;
        }
    };

    EvFsmHelper helper(make_dc_config(), callbacks);
    auto& ctx = helper.get_context();
    fsm::v2::FSM<din::ev::StateBase> fsm{ctx.create_state<din::ev::state::SessionSetup>()};

    run_handshake_to_charge_parameter_discovery(fsm, helper);

    // EvseV2G no-energy-pause path: Finished with EVSENotification=StopCharging. The EVCC must go to
    // SessionStop (not CableCheck) and must not signal ev_power_ready.
    {
        message_din::ChargeParameterDiscoveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        dt::DcEvseChargeParameter param;
        param.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        param.dc_evse_status.evse_notification = dt::EvseNotification::StopCharging;
        param.evse_maximum_current_limit = 400.0;
        param.evse_maximum_voltage_limit = 920.0;
        res.dc_evse_charge_parameter = param;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::SessionStop);
    }
    REQUIRE(not ev_power_ready_seen);

    // --- SessionStop ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_din::SessionStopRequest>().has_value());
    {
        message_din::SessionStopResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        fsm.feed(Event::V2GTP_MESSAGE);
    }
    REQUIRE(ctx.session_stopped);
    REQUIRE(not ctx.session_paused);
}

SCENARIO("EVCC DIN FSM WeldingDetection exits early once the EVSE voltage is safe") {
    session::ev::feedback::Callbacks callbacks{};

    EvFsmHelper helper(make_dc_config(), callbacks);
    auto& ctx = helper.get_context();
    fsm::v2::FSM<din::ev::StateBase> fsm{ctx.create_state<din::ev::state::SessionSetup>()};

    run_handshake_to_current_demand(fsm, helper);

    // EV-initiated stop to reach the teardown path.
    helper.set_control_event(d20::ev::StopCharging{true});
    fsm.feed(Event::CONTROL_MESSAGE);
    helper.clear_control_event();
    {
        helper.inject_response(make_current_demand_res(dt::EvseNotification::None));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    }

    // --- PowerDelivery(Stop) ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_din::PowerDeliveryRequest>().has_value());
    {
        message_din::PowerDeliveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::WeldingDetection);
    }

    // --- WeldingDetection: a single response below the safe threshold exits immediately ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_din::WeldingDetectionRequest>().has_value());
    {
        message_din::WeldingDetectionResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.evse_present_voltage = 40.0; // below the 60 V safe threshold
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::SessionStop);
    }
}

SCENARIO("EVCC DIN SessionSetup accepts OK_OldSessionJoined for a resumed session") {
    session::ev::feedback::Callbacks callbacks{};
    auto config = make_dc_config();
    config.resumed_session_id = ASSIGNED_SESSION_ID;

    EvFsmHelper helper(std::move(config), callbacks);
    auto& ctx = helper.get_context();
    fsm::v2::FSM<din::ev::StateBase> fsm{ctx.create_state<din::ev::state::SessionSetup>()};

    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_din::SessionSetupRequest>();
        REQUIRE(req.has_value());
        // The resumed session id is carried in the request header.
        REQUIRE(req->header.session_id == ASSIGNED_SESSION_ID);
    }
    {
        helper.inject_response(make_session_setup_res(dt::ResponseCode::OK_OldSessionJoined));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ServiceDiscovery);
        REQUIRE(not ctx.session_stopped);
    }
}
