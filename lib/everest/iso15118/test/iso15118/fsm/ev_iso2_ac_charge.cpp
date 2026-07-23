// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
//
// Scripted FSM-level test driving the EVCC ISO 15118-2 AC branch from SessionSetup through the
// ChargingStatus loop. Two scenarios: a full charge ended by the EVSE (StopCharging), and an
// EV-initiated pause that ends in SessionStop(Pause). Responses go through the real message_2 EXI
// codec. Mirrors ev_ac_charge.cpp.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/d2/context.hpp>
#include <iso15118/d2/ev/control_event.hpp>
#include <iso15118/d2/ev/state/session_setup.hpp>
#include <iso15118/d2/ev/states.hpp>
#include <iso15118/d20/timeout.hpp>

#include <iso15118/io/logging.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/charging_status.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/power_delivery.hpp>
#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/session_setup.hpp>
#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/message_2/variant.hpp>
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
        msg_exch.check_and_clear_response();

        std::array<uint8_t, 4096> buffer{};
        io::StreamOutputView view{buffer.data(), buffer.size()};
        const auto len = message_2::serialize(res, view);

        auto variant = std::make_unique<message_2::Variant>(io::StreamInputView{buffer.data(), len});
        REQUIRE(variant->get_type() != message_2::Type::None);
        msg_exch.set_request(std::move(variant));
    }

    void feed_control_event(fsm::v2::FSM<d2::ev::StateBase>& fsm, const d2::ev::ControlEvent& event) {
        active_control_event = event;
        fsm.feed(Event::CONTROL_MESSAGE);
        active_control_event.reset();
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

d2::ev::EvSessionConfig make_ac_config() {
    d2::ev::EvSessionConfig config;
    config.requested_energy_transfer_mode = dt::EnergyTransferMode::AC_three_phase_core;
    return config;
}

message_2::Header make_header() {
    message_2::Header header;
    header.session_id = ASSIGNED_SESSION_ID;
    return header;
}

dt::AC_EVSEStatus make_ac_status(dt::EVSENotification notification = dt::EVSENotification::None) {
    dt::AC_EVSEStatus status;
    status.notification_max_delay = 0;
    status.notification = notification;
    status.rcd = false;
    return status;
}

// Drive the shared handshake up to and including the transition into ChargingStatus.
void drive_to_charging_status(EvFsmHelper& helper, fsm::v2::FSM<d2::ev::StateBase>& fsm) {
    // SessionSetup
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_2::SessionSetupRequest>().has_value());
    {
        message_2::SessionSetupResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK_NewSessionEstablished;
        res.evse_id = "everest se";
        helper.inject_response(res);
        REQUIRE(fsm.feed(Event::V2GTP_MESSAGE).transitioned());
    }

    // ServiceDiscovery
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
        res.charge_service.supported_energy_transfer_mode.push_back(dt::EnergyTransferMode::AC_three_phase_core);
        helper.inject_response(res);
        REQUIRE(fsm.feed(Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PaymentServiceSelection);
    }

    // PaymentServiceSelection
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_2::PaymentServiceSelectionRequest>().has_value());
    {
        message_2::PaymentServiceSelectionResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        REQUIRE(fsm.feed(Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::Authorization);
    }

    // Authorization
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_2::AuthorizationRequest>().has_value());
    {
        message_2::AuthorizationResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EVSEProcessing::Finished;
        helper.inject_response(res);
        REQUIRE(fsm.feed(Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ChargeParameterDiscovery);
    }

    // ChargeParameterDiscovery (AC)
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_2::ChargeParameterDiscoveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->ac_ev_charge_parameter.has_value());
    }
    {
        message_2::ChargeParameterDiscoveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EVSEProcessing::Finished;
        dt::SAScheduleTuple tuple;
        tuple.sa_schedule_tuple_id = 1;
        dt::PMaxScheduleEntry entry;
        entry.start = 0;
        entry.p_max = dt::to_physical_value(22000.0, dt::Unit::W);
        tuple.pmax_schedule.push_back(entry);
        dt::SAScheduleList list;
        list.push_back(tuple);
        res.sa_schedule_list = list;
        dt::AC_EVSEChargeParameter ac;
        ac.ac_evse_status = make_ac_status();
        ac.evse_nominal_voltage = dt::to_physical_value(230.0, dt::Unit::V);
        ac.evse_max_current = dt::to_physical_value(32.0, dt::Unit::A);
        res.ac_evse_charge_parameter = ac;
        helper.inject_response(res);
        REQUIRE(fsm.feed(Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    }

    // PowerDelivery(Start) with mandatory ChargingProfile
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_2::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::ChargeProgress::Start);
        REQUIRE(req->sa_schedule_tuple_id == 1);
        REQUIRE(req->charging_profile.has_value());
        REQUIRE(req->charging_profile->profile_entry.size() == 1);
    }
    {
        message_2::PowerDeliveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        REQUIRE(fsm.feed(Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ChargingStatus);
    }
}

message_2::ChargingStatusResponse make_charging_status_response(dt::EVSENotification notification) {
    message_2::ChargingStatusResponse res;
    res.header = make_header();
    res.response_code = dt::ResponseCode::OK;
    res.evse_id = "everest se";
    res.sa_schedule_tuple_id = 1;
    res.evse_max_current = dt::to_physical_value(32.0, dt::Unit::A);
    res.ac_evse_status = make_ac_status(notification);
    return res;
}

} // namespace

SCENARIO("EVCC ISO-2 AC FSM drives the AC charging flow ended by the EVSE") {

    bool power_ready = false;
    bool target_power_seen = false;
    bool stop_from_charger_seen = false;
    session::ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&](bool ready) { power_ready = power_ready or ready; };
    callbacks.ac_evse_target_power = [&](const d20::AcTargetPower& target) {
        if (target.target_active_power.has_value()) {
            target_power_seen = true;
        }
    };
    callbacks.stop_from_charger = [&]() { stop_from_charger_seen = true; };
    callbacks.selected_protocol = [&](const std::string&) {};

    EvFsmHelper helper(make_ac_config(), callbacks);
    auto& ctx = helper.get_context();
    fsm::v2::FSM<d2::ev::StateBase> fsm{ctx.create_state<d2::ev::state::SessionSetup>()};

    drive_to_charging_status(helper, fsm);
    REQUIRE(power_ready);

    // ChargingStatus loop
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_2::ChargingStatusRequest>().has_value());
    for (int i = 0; i < 2; ++i) {
        helper.inject_response(make_charging_status_response(dt::EVSENotification::None));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ChargingStatus);
        REQUIRE(helper.take_request<message_2::ChargingStatusRequest>().has_value());
    }
    REQUIRE(target_power_seen);
    {
        helper.inject_response(make_charging_status_response(dt::EVSENotification::StopCharging));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    }
    REQUIRE(stop_from_charger_seen);

    // PowerDelivery(Stop) -> SessionStop (AC has no welding detection)
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_2::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::ChargeProgress::Stop);
    }
    {
        message_2::PowerDeliveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        REQUIRE(fsm.feed(Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::SessionStop);
    }

    // SessionStop (Terminate)
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
}

SCENARIO("EVCC ISO-2 AC FSM handles an EV-initiated pause") {

    session::ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&](bool) {};
    callbacks.ac_evse_target_power = [&](const d20::AcTargetPower&) {};
    callbacks.selected_protocol = [&](const std::string&) {};

    EvFsmHelper helper(make_ac_config(), callbacks);
    auto& ctx = helper.get_context();
    fsm::v2::FSM<d2::ev::StateBase> fsm{ctx.create_state<d2::ev::state::SessionSetup>()};

    drive_to_charging_status(helper, fsm);

    // ChargingStatus loop, then an EV pause request.
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_2::ChargingStatusRequest>().has_value());
    {
        helper.inject_response(make_charging_status_response(dt::EVSENotification::None));
        REQUIRE(not fsm.feed(Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(helper.take_request<message_2::ChargingStatusRequest>().has_value());
    }

    // The EV requests a pause; it takes effect on the next ChargingStatusRes.
    helper.feed_control_event(fsm, d2::ev::PauseCharging{true});
    {
        helper.inject_response(make_charging_status_response(dt::EVSENotification::None));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    }

    // PowerDelivery(Stop) -> SessionStop
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_2::PowerDeliveryRequest>().has_value());
    {
        message_2::PowerDeliveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        REQUIRE(fsm.feed(Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::SessionStop);
    }

    // SessionStop must carry ChargingSession=Pause and mark the session paused.
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_2::SessionStopRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charging_session == dt::ChargingSession::Pause);
    }
    {
        message_2::SessionStopResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(ctx.session_paused);
        REQUIRE(not ctx.session_stopped);
    }
}
