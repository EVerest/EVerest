// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Session-level FSM walk: the only input is injected EXI response frames via
// session.on_bytes_received, the only output is the captured request frames.
// Deliberately no fsm.feed() and no manual create_state<>(), which is what makes
// this a proof that the real EV states drive the sequence.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <bitset>
#include <chrono>
#include <cstdint>
#include <vector>

#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_iec_charge_loop.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/dc_welding_detection.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/service_selection.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/type.hpp>

#include <iso15118/ev/d20/control_event.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using namespace iso15118::ev::test;

namespace {

using message_20::datatypes::ControlMode;
using message_20::datatypes::ParameterSet;
using message_20::datatypes::Processing;
using message_20::datatypes::ResponseCode;
using PT = io::v2gtp::PayloadType;

constexpr message_20::datatypes::SessionId WALK_SESSION_ID{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

// A ServiceDetail parameter set carrying a named ControlMode, mirroring the SECC's
// EXI encoding the EV honest-selects on. The ServiceDetail state requires a Dynamic
// set to advance.
ParameterSet make_param_set(uint16_t id, ControlMode control_mode) {
    ParameterSet set{};
    set.id = id;
    set.parameter.push_back({"Connector", static_cast<int32_t>(1)});
    set.parameter.push_back({"ControlMode", static_cast<int32_t>(control_mode)});
    set.parameter.push_back({"EVSENominalVoltage", static_cast<int32_t>(230)});
    return set;
}

// Walk an ev::Session from start() through the first DC_CableCheckRequest by injecting
// canned response frames, asserting the request emitted at each step and the feedback
// fired along the way. Returns the established session id.
message_20::datatypes::SessionId walk_to_dc_cable_check(SessionFixture& fx) {
    const auto sid = WALK_SESSION_ID;

    // start() -> SupportedAppProtocolRequest
    fx.session.start();
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
    REQUIRE(decode_frame(fx.captured.back()).get_if<message_20::SupportedAppProtocolRequest>() != nullptr);

    // SupportedAppProtocolResponse -> SessionSetupRequest
    inject_then_expect<message_20::SessionSetupRequest>(
        fx, "SAP -> SessionSetup",
        message_20::SupportedAppProtocolResponse{
            message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1},
        PT::SAP);

    // SessionSetupResponse -> AuthorizationSetupRequest. The real session_setup aborts on
    // an all-zero returned session_id or an empty evseid, so a valid establishment carries both.
    auto setup_res = ok_res<message_20::SessionSetupResponse>(sid, ResponseCode::OK_NewSessionEstablished);
    setup_res.evseid = "DE*PNX*E12345";
    inject_then_expect<message_20::AuthorizationSetupRequest>(fx, "SessionSetup -> AuthorizationSetup", setup_res,
                                                              PT::Part20Main);

    // AuthorizationSetupResponse(OK, {EIM}) -> AuthorizationRequest (selects EIM).
    auto auth_setup_res = ok_res<message_20::AuthorizationSetupResponse>(sid);
    auth_setup_res.authorization_services = {message_20::datatypes::Authorization::EIM};
    auth_setup_res.certificate_installation_service = false;
    auth_setup_res.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
    {
        const auto req = inject_then_expect<message_20::AuthorizationRequest>(fx, "AuthorizationSetup -> Authorization",
                                                                              auth_setup_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.selected_authorization_service == message_20::datatypes::Authorization::EIM);
        REQUIRE(std::holds_alternative<message_20::datatypes::EIM_ASReqAuthorizationMode>(req.authorization_mode));
    }

    // AuthorizationResponse(OK, Finished) -> ServiceDiscoveryRequest.
    auto auth_res = ok_res<message_20::AuthorizationResponse>(sid);
    auth_res.evse_processing = Processing::Finished;
    {
        const auto req = inject_then_expect<message_20::ServiceDiscoveryRequest>(
            fx, "Authorization -> ServiceDiscovery", auth_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
    }

    // ServiceDiscoveryResponse(OK, offering DC) -> ServiceDetailRequest.
    auto discovery_res = ok_res<message_20::ServiceDiscoveryResponse>(sid);
    discovery_res.energy_transfer_service_list = {{message_20::datatypes::ServiceCategory::DC, false}};
    {
        const auto req = inject_then_expect<message_20::ServiceDetailRequest>(fx, "ServiceDiscovery -> ServiceDetail",
                                                                              discovery_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.service == message_20::to_underlying_value(message_20::datatypes::ServiceCategory::DC));
    }

    // ServiceDetailResponse(OK, one param set) -> ServiceSelectionRequest.
    auto detail_res = ok_res<message_20::ServiceDetailResponse>(sid);
    detail_res.service = message_20::to_underlying_value(message_20::datatypes::ServiceCategory::DC);
    detail_res.service_parameter_list = {make_param_set(1, ControlMode::Dynamic)};
    {
        const auto req = inject_then_expect<message_20::ServiceSelectionRequest>(
            fx, "ServiceDetail -> ServiceSelection", detail_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.selected_energy_transfer_service.service_id == message_20::datatypes::ServiceCategory::DC);
        REQUIRE(req.selected_energy_transfer_service.parameter_set_id == 1);
    }

    // ServiceSelectionResponse(OK) -> DC_ChargeParameterDiscoveryRequest.
    auto selection_res = ok_res<message_20::ServiceSelectionResponse>(sid);
    {
        const auto req = inject_then_expect<message_20::DC_ChargeParameterDiscoveryRequest>(
            fx, "ServiceSelection -> DC_ChargeParameterDiscovery", selection_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(std::holds_alternative<message_20::datatypes::DC_CPDReqEnergyTransferMode>(req.transfer_mode));
    }

    // DC_ChargeParameterDiscoveryResponse(OK) -> ScheduleExchangeRequest.
    auto cpd_res = ok_res<message_20::DC_ChargeParameterDiscoveryResponse>(sid);
    cpd_res.transfer_mode = message_20::datatypes::DC_CPDResEnergyTransferMode{};
    {
        const auto req = inject_then_expect<message_20::ScheduleExchangeRequest>(
            fx, "DC_ChargeParameterDiscovery -> ScheduleExchange", cpd_res, PT::Part20DC);
        REQUIRE(req.header.session_id == sid);
    }

    // ScheduleExchangeResponse(OK, Finished) -> DC_CableCheckRequest; fires ev_power_ready.
    REQUIRE_FALSE(fx.ev_power_ready);
    auto schedule_res = ok_res<message_20::ScheduleExchangeResponse>(sid);
    schedule_res.processing = Processing::Finished;
    {
        const auto req = inject_then_expect<message_20::DC_CableCheckRequest>(fx, "ScheduleExchange -> DC_CableCheck",
                                                                              schedule_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
    }
    REQUIRE(fx.ev_power_ready);

    return sid;
}

// Continue from the first DC_CableCheckRequest through the first DC_ChargeLoopRequest.
// Shared by the DC scenarios; returns the established session id.
message_20::datatypes::SessionId walk_to_dc_charge_loop(SessionFixture& fx) {
    const auto sid = walk_to_dc_cable_check(fx);

    // DC_CableCheckResponse(OK, Finished) -> DC_PreChargeRequest(Ongoing).
    auto cable_check_res = ok_res<message_20::DC_CableCheckResponse>(sid);
    cable_check_res.processing = Processing::Finished;
    {
        const auto req = inject_then_expect<message_20::DC_PreChargeRequest>(fx, "DC_CableCheck -> DC_PreCharge",
                                                                             cable_check_res, PT::Part20DC);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.processing == Processing::Ongoing);
    }

    // DC_PreChargeResponse(OK, in-tolerance) -> DC_PreChargeRequest(Finished). While
    // EVProcessing is Ongoing the SECC accepts only another DC_PreChargeReq
    // [V2G20-2005]; Finished is what permits PowerDeliveryReq [V2G20-2006].
    REQUIRE_FALSE(fx.dc_power_on);
    auto pre_charge_res = ok_res<message_20::DC_PreChargeResponse>(sid);
    pre_charge_res.present_voltage = message_20::datatypes::from_float(400.0f);
    {
        const auto req = inject_then_expect<message_20::DC_PreChargeRequest>(
            fx, "DC_PreCharge(in tolerance) -> DC_PreCharge(Finished)", pre_charge_res, PT::Part20DC);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.processing == Processing::Finished);
    }
    REQUIRE_FALSE(fx.dc_power_on);

    // The response to that closing request is an acknowledgement, not another voltage
    // reading, so an out-of-tolerance present voltage here must still advance: the
    // converter may have settled away from the target by now.
    auto pre_charge_ack = ok_res<message_20::DC_PreChargeResponse>(sid);
    pre_charge_ack.present_voltage = message_20::datatypes::from_float(250.0f);
    {
        const auto req = inject_then_expect<message_20::PowerDeliveryRequest>(
            fx, "DC_PreCharge(Finished) -> PowerDelivery(Start)", pre_charge_ack, PT::Part20DC);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.charge_progress == message_20::datatypes::Progress::Start);
    }
    REQUIRE(fx.dc_power_on);

    // PowerDeliveryResponse(OK) -> DC_ChargeLoopRequest (Dynamic control mode).
    auto power_delivery_res = ok_res<message_20::PowerDeliveryResponse>(sid);
    {
        const auto req = inject_then_expect<message_20::DC_ChargeLoopRequest>(fx, "PowerDelivery -> DC_ChargeLoop",
                                                                              power_delivery_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_DC_CLReqControlMode>(req.control_mode));
    }

    return sid;
}

// Drive DC_WeldingDetection -> SessionStop -> clean finish from an emitted
// PowerDeliveryRequest(Stop). Shared by both scenarios' tail.
void walk_stop_to_finish(SessionFixture& fx, const message_20::datatypes::SessionId& sid) {
    auto power_delivery_res = ok_res<message_20::PowerDeliveryResponse>(sid);
    {
        const auto req = inject_then_expect<message_20::DC_WeldingDetectionRequest>(
            fx, "PowerDelivery(Stop) -> DC_WeldingDetection", power_delivery_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
    }

    auto welding_res = ok_res<message_20::DC_WeldingDetectionResponse>(sid);
    welding_res.present_voltage = message_20::datatypes::from_float(0.0f);
    {
        // The SECC leaves welding detection on EVProcessing=Finished, so the Ongoing request is
        // answered with the closing one before SessionStop.
        const auto req = inject_then_expect<message_20::DC_WeldingDetectionRequest>(
            fx, "DC_WeldingDetection -> closing Finished", welding_res, PT::Part20DC);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.processing == message_20::datatypes::Processing::Finished);
    }

    {
        const auto req = inject_then_expect<message_20::SessionStopRequest>(fx, "DC_WeldingDetection -> SessionStop",
                                                                            welding_res, PT::Part20DC);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.charging_session == message_20::datatypes::ChargingSession::Terminate);
    }

    // SessionStopResponse(OK) -> session finishes cleanly (no watchdog park).
    auto stop_res = ok_res<message_20::SessionStopResponse>(sid);
    fx.session.on_bytes_received(frame_payload(PT::Part20Main, serialize_msg(stop_res)));
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.session.is_finished(); }, 1s));
    REQUIRE(fx.session.is_finished());
    REQUIRE_FALSE(fx.timed_out);
}

// Walk an AC-configured ev::Session from start() through the first AC_ChargeLoopRequest,
// asserting the emitted request at each step and the feedback fired along the way. The
// energy service routes past the DC-only states: ScheduleExchange -> PowerDelivery(Start)
} // namespace

SCENARIO("ISO15118-20 EV Session drives the states byte-by-byte through a full DC session to SessionStop") {

    GIVEN("A Session bound to a reactor with short send-delay and watchdog timers") {
        SessionFixture fx;

        WHEN("the session is walked to an active DC_ChargeLoop and the SECC then signals Terminate") {
            const auto sid = walk_to_dc_charge_loop(fx);

            // The walk reached the charge loop without finishing or timing out.
            REQUIRE(fx.captured.size() == 14);
            REQUIRE_FALSE(fx.session.is_finished());
            REQUIRE_FALSE(fx.timed_out);

            // DC_ChargeLoopResponse(OK, no Terminate) -> DC_ChargeLoopRequest (loop continues).
            REQUIRE_FALSE(fx.stop_from_charger);
            auto loop_ok = ok_res<message_20::DC_ChargeLoopResponse>(sid);
            loop_ok.control_mode = message_20::datatypes::Dynamic_DC_CLResControlMode{};
            inject_then_expect<message_20::DC_ChargeLoopRequest>(fx, "DC_ChargeLoop OK -> DC_ChargeLoop", loop_ok,
                                                                 PT::Part20DC);
            REQUIRE_FALSE(fx.stop_from_charger);

            // DC_ChargeLoopResponse(OK, Terminate) -> PowerDeliveryRequest(Stop); fires stop_from_charger.
            auto loop_terminate = ok_res<message_20::DC_ChargeLoopResponse>(sid);
            loop_terminate.status =
                message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Terminate};
            loop_terminate.control_mode = message_20::datatypes::Dynamic_DC_CLResControlMode{};
            {
                const auto req = inject_then_expect<message_20::PowerDeliveryRequest>(
                    fx, "DC_ChargeLoop Terminate -> PowerDelivery(Stop)", loop_terminate, PT::Part20DC);
                REQUIRE(req.header.session_id == sid);
                REQUIRE(req.charge_progress == message_20::datatypes::Progress::Stop);
            }
            REQUIRE(fx.stop_from_charger);

            THEN("PowerDelivery(Stop) walks through DC_WeldingDetection to a clean SessionStop") {
                walk_stop_to_finish(fx, sid);
                REQUIRE(fx.stop_from_charger);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Session drives a graceful EV-initiated stop from an active DC_ChargeLoop") {
    // Proves the delivery path request_stop() marshals: a StopCharging control event
    // delivered through Session::deliver_control_event is recorded on the Context and, on the
    // next DC_ChargeLoopResponse, drives PowerDelivery(Stop) -> DC_WeldingDetection ->
    // SessionStop without any SECC Terminate notification.
    GIVEN("A Session walked to an active DC_ChargeLoop") {
        SessionFixture fx;
        const auto sid = walk_to_dc_charge_loop(fx);

        WHEN("a StopCharging control event is delivered and the next OK loop response arrives") {
            // The EV requests a stop mid-loop; the SECC has NOT sent Terminate.
            fx.session.deliver_control_event(ev::d20::StopCharging{true});

            auto loop_ok = ok_res<message_20::DC_ChargeLoopResponse>(sid);
            loop_ok.control_mode = message_20::datatypes::Dynamic_DC_CLResControlMode{};
            {
                const auto req = inject_then_expect<message_20::PowerDeliveryRequest>(
                    fx, "EV-initiated stop -> PowerDelivery(Stop)", loop_ok, PT::Part20DC);
                REQUIRE(req.charge_progress == message_20::datatypes::Progress::Stop);
            }
            // The EV drove the stop; the SECC-Terminate feedback must NOT have fired.
            REQUIRE_FALSE(fx.stop_from_charger);

            THEN("the loop breaks into PowerDelivery(Stop) and walks to a clean SessionStop") {
                walk_stop_to_finish(fx, sid);
                REQUIRE_FALSE(fx.stop_from_charger);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Session: EV stop requested during DC_CableCheck sends SessionStopReq next") {
    // [V2G20-2644]: before PowerDeliveryReq(Start) an EV-side stop goes straight to SessionStop.
    // No precharge, no dc_power_on, and no PowerDelivery(Stop)/welding-detection walk.
    GIVEN("A Session walked to the first DC_CableCheckRequest") {
        SessionFixture fx;
        const auto sid = walk_to_dc_cable_check(fx);

        WHEN("a StopCharging control event is delivered and the CableCheck OK Finished response arrives") {
            fx.session.deliver_control_event(ev::d20::StopCharging{true});

            auto cable_check_res = ok_res<message_20::DC_CableCheckResponse>(sid);
            cable_check_res.processing = Processing::Finished;
            {
                const auto req = inject_then_expect<message_20::SessionStopRequest>(
                    fx, "EV stop during DC_CableCheck -> SessionStop", cable_check_res, PT::Part20DC);
                REQUIRE(req.header.session_id == sid);
                REQUIRE(req.charging_session == message_20::datatypes::ChargingSession::Terminate);
            }
            REQUIRE_FALSE(fx.dc_power_on);
            REQUIRE_FALSE(fx.stop_from_charger);

            THEN("the SessionStopResponse finishes the session cleanly") {
                auto stop_res = ok_res<message_20::SessionStopResponse>(sid);
                fx.session.on_bytes_received(frame_payload(PT::Part20Main, serialize_msg(stop_res)));
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.session.is_finished(); }, 1s));
                REQUIRE_FALSE(fx.timed_out);
                REQUIRE_FALSE(fx.dc_power_on);
            }
        }
    }
}
