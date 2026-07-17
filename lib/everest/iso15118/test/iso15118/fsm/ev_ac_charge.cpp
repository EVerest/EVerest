// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
//
// Scripted FSM-level test driving the EVCC AC branch (AC_BPT Dynamic) from AC_ChargeParameterDiscovery
// through the charge loop and an EVSE-initiated Terminate down to SessionStop. The canned SECC
// responses are serialized via message_20::serialize and decoded back through message_20::Variant, i.e.
// they go through the exact same EXI codec that is used on the wire. Mirrors ev_dc_charge.cpp.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <optional>
#include <variant>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/d20/ev/state/ac_charge_parameter_discovery.hpp>
#include <iso15118/d20/ev/states.hpp>
#include <iso15118/d20/timeout.hpp>

#include <iso15118/io/logging.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
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

session::EvSessionConfig make_ac_bpt_config() {
    session::EvSetupConfig setup;
    setup.evcc_id = "WMIV1234567890ABCDEX";
    setup.supported_energy_services = {dt::ServiceCategory::AC_BPT, dt::ServiceCategory::AC};
    setup.preferred_control_mode = dt::ControlMode::Dynamic;
    setup.supported_auth_options = {dt::Authorization::EIM};

    auto& ac = setup.ac_charge_parameters;
    ac.max_charge_power = dt::from_float(11000.0f);
    ac.min_charge_power = dt::from_float(100.0f);
    ac.max_discharge_power = dt::from_float(11000.0f);
    ac.min_discharge_power = dt::from_float(100.0f);

    return session::EvSessionConfig(setup);
}

message_20::Header make_header() {
    message_20::Header header;
    header.session_id = ASSIGNED_SESSION_ID;
    header.timestamp = 1691411798;
    return header;
}

message_20::AC_ChargeLoopResponse
make_charge_loop_res(std::optional<dt::EvseNotification> notification = std::nullopt) {
    message_20::AC_ChargeLoopResponse res;
    res.header = make_header();
    res.response_code = dt::ResponseCode::OK;
    auto& mode = res.control_mode.emplace<dt::BPT_Dynamic_AC_CLResControlMode>();
    mode.target_active_power = dt::from_float(7000.0f);
    if (notification.has_value()) {
        res.status = dt::EvseStatus{0, notification.value()};
    }
    return res;
}

} // namespace

SCENARIO("EVCC AC FSM drives the full AC_BPT Dynamic charging flow against canned SECC responses") {

    // Track the feedback ordering contract [flow spec §4].
    std::vector<std::string> feedback_order;
    int target_power_count = 0;
    std::optional<float> last_target_power;

    session::ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&](bool ready) {
        if (ready) {
            feedback_order.emplace_back("ev_power_ready");
        }
    };
    callbacks.ac_evse_target_power = [&](const d20::AcTargetPower& target) {
        ++target_power_count;
        if (target.target_active_power.has_value()) {
            last_target_power = dt::from_RationalNumber(target.target_active_power.value());
        }
    };
    callbacks.stop_from_charger = [&]() { feedback_order.emplace_back("stop_from_charger"); };
    bool charge_loop_started = false;
    callbacks.signal = [&](session::ev::feedback::Signal signal) {
        if (signal == session::ev::feedback::Signal::CHARGE_LOOP_STARTED) {
            charge_loop_started = true;
        }
    };

    EvFsmHelper helper(make_ac_bpt_config(), callbacks);
    auto& ctx = helper.get_context();

    // Pre-set the state that the handshake would have negotiated (covered by ev_handshake.cpp).
    ctx.set_session_id(ASSIGNED_SESSION_ID);
    ctx.evse_info.selected_energy_service = dt::ServiceCategory::AC_BPT;
    ctx.evse_info.selected_control_mode = dt::ControlMode::Dynamic;
    ctx.evse_info.selected_parameter_set_id = 1;

    fsm::v2::FSM<d20::ev::StateBase> fsm{ctx.create_state<d20::ev::state::AC_ChargeParameterDiscovery>()};

    // --- AC_ChargeParameterDiscovery ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::AC_ChargeParameterDiscoveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(std::holds_alternative<dt::BPT_AC_CPDReqEnergyTransferMode>(req->transfer_mode));
    }
    {
        message_20::AC_ChargeParameterDiscoveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        auto& mode = res.transfer_mode.emplace<dt::BPT_AC_CPDResEnergyTransferMode>();
        mode.max_charge_power = dt::from_float(22000.0f);
        mode.min_charge_power = dt::from_float(100.0f);
        mode.nominal_frequency = dt::from_float(50.0f);
        mode.max_discharge_power = dt::from_float(11000.0f);
        mode.min_discharge_power = dt::from_float(100.0f);
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ScheduleExchange);
    }
    REQUIRE(ctx.evse_info.ac_present_limits.has_value());
    REQUIRE(ctx.evse_info.ac_present_limits->charge_power == 22000.0f);

    // --- ScheduleExchange ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::ScheduleExchangeRequest>();
        REQUIRE(req.has_value());
        REQUIRE(std::holds_alternative<dt::Dynamic_SEReqControlMode>(req->control_mode));
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
        // AC branch goes straight to PowerDelivery(Start), no cable check / pre-charge.
        REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    }
    // ev_power_ready must be published before any PowerDelivery [flow spec §4.1].
    REQUIRE(feedback_order == std::vector<std::string>{"ev_power_ready"});

    // --- PowerDelivery(Start) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::Progress::Start);
        // BPT charge channel selection, never Standby.
        REQUIRE(req->channel_selection.has_value());
        REQUIRE(req->channel_selection.value() == dt::ChannelSelection::Charge);
    }
    {
        message_20::PowerDeliveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::AC_ChargeLoop);
    }

    // --- AC_ChargeLoop (a few loops, then an EVSE Terminate notification) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::AC_ChargeLoopRequest>();
        REQUIRE(req.has_value());
        REQUIRE(std::holds_alternative<dt::BPT_Dynamic_AC_CLReqControlMode>(req->control_mode));
    }
    for (int i = 0; i < 3; ++i) {
        helper.inject_response(make_charge_loop_res());
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::AC_ChargeLoop);
        REQUIRE(helper.take_request<message_20::AC_ChargeLoopRequest>().has_value());
    }
    REQUIRE(charge_loop_started);
    // ac_evse_target_power fired on every response so far.
    REQUIRE(target_power_count == 3);
    REQUIRE(last_target_power.has_value());
    REQUIRE(last_target_power.value() == 7000.0f);

    {
        helper.inject_response(make_charge_loop_res(dt::EvseNotification::Terminate));
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        // AC stop path: no welding detection, straight to PowerDelivery(Stop).
        REQUIRE(fsm.get_current_state_id() == StateID::PowerDelivery);
    }
    // The terminating response also fired ac_evse_target_power.
    REQUIRE(target_power_count == 4);

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
        // AC: PowerDelivery(Stop) -> SessionStop directly (no DC_WeldingDetection).
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

    // Feedback ordering: ev_power_ready -> stop_from_charger [flow spec §4]. No dc_power_on for AC.
    REQUIRE(feedback_order == std::vector<std::string>{"ev_power_ready", "stop_from_charger"});
}
