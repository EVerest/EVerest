// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Session-level FSM-walk integration test.
//
// This drives an ev::Session purely by injecting canned, EXI-encoded V2GTP
// response frames via session.on_bytes_received(...) and asserting the request
// frame the Session emits at each step. No fsm.feed() and no manual create_state<>()
// are used: the only input is bytes, the only output is captured frames. This is
// the proof that fsm::v2::FSM<d20::StateBase> drives the real EV states
// unchanged through the implemented entry sequence.
//
// Clean DC walk to a graceful SessionStop (each arrow = one injected response ->
// one emitted request):
//
//   start()                              -> SupportedAppProtocolRequest
//   SupportedAppProtocolResponse         -> SessionSetupRequest
//   SessionSetupResponse                 -> AuthorizationSetupRequest
//   AuthorizationSetupResponse           -> AuthorizationRequest
//   AuthorizationResponse                -> ServiceDiscoveryRequest
//   ServiceDiscoveryResponse             -> ServiceDetailRequest
//   ServiceDetailResponse                -> ServiceSelectionRequest
//   ServiceSelectionResponse             -> DC_ChargeParameterDiscoveryRequest
//   DC_ChargeParameterDiscoveryResponse  -> ScheduleExchangeRequest
//   ScheduleExchangeResponse(Finished)   -> DC_CableCheckRequest
//   DC_CableCheckResponse(Finished)      -> DC_PreChargeRequest
//   DC_PreChargeResponse(OK)             -> PowerDeliveryRequest(Start)
//   PowerDeliveryResponse(OK)            -> DC_ChargeLoopRequest
//   DC_ChargeLoopResponse(OK)            -> DC_ChargeLoopRequest (loop continues)
//   DC_ChargeLoopResponse(OK, Terminate) -> PowerDeliveryRequest(Stop)
//   PowerDeliveryResponse(OK)            -> DC_WeldingDetectionRequest
//   DC_WeldingDetectionResponse(OK)      -> SessionStopRequest
//   SessionStopResponse(OK)              -> session finishes cleanly
//
// The DC_ChargeLoop iterates: a plain OK response re-emits a loop request; only the
// SECC's Terminate notification breaks the loop, firing the stop_from_charger feedback
// and driving PowerDelivery(Stop) -> DC_WeldingDetection -> SessionStop. The
// SessionStopResponse ends the session cleanly (is_finished, not timed_out).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
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

// AC constants seeded into the fixture: MAX/MIN asserted on the AC_CPD request,
// PRESENT_ACTIVE_POWER asserted on the AC_ChargeLoopRequest, TARGET_ACTIVE_POWER
// injected in the response and observed via the ac_target_power flag.
constexpr float AC_MAX_CHARGE_POWER = 22000.0f;
constexpr float AC_MIN_CHARGE_POWER = 1000.0f;
constexpr float AC_PRESENT_ACTIVE_POWER = 5000.0f;
constexpr float AC_TARGET_ACTIVE_POWER = 7000.0f;

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

// Walk an ev::Session from start() through the first DC_ChargeLoopRequest by injecting
// canned response frames, asserting the request emitted at each step and the feedback
// fired along the way. Shared by both scenarios; returns the established session id.
message_20::datatypes::SessionId walk_to_dc_charge_loop(SessionFixture& fx) {
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
    message_20::SessionSetupResponse setup_res{};
    setup_res.response_code = ResponseCode::OK_NewSessionEstablished;
    setup_res.header.session_id = sid;
    setup_res.evseid = "DE*PNX*E12345";
    inject_then_expect<message_20::AuthorizationSetupRequest>(fx, "SessionSetup -> AuthorizationSetup", setup_res,
                                                              PT::Part20Main);

    // AuthorizationSetupResponse(OK, {EIM}) -> AuthorizationRequest (selects EIM).
    message_20::AuthorizationSetupResponse auth_setup_res{};
    auth_setup_res.header.session_id = sid;
    auth_setup_res.response_code = ResponseCode::OK;
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
    message_20::AuthorizationResponse auth_res{};
    auth_res.header.session_id = sid;
    auth_res.response_code = ResponseCode::OK;
    auth_res.evse_processing = Processing::Finished;
    {
        const auto req = inject_then_expect<message_20::ServiceDiscoveryRequest>(
            fx, "Authorization -> ServiceDiscovery", auth_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
    }

    // ServiceDiscoveryResponse(OK, offering DC) -> ServiceDetailRequest.
    message_20::ServiceDiscoveryResponse discovery_res{};
    discovery_res.header.session_id = sid;
    discovery_res.response_code = ResponseCode::OK;
    discovery_res.energy_transfer_service_list = {{message_20::datatypes::ServiceCategory::DC, false}};
    {
        const auto req = inject_then_expect<message_20::ServiceDetailRequest>(fx, "ServiceDiscovery -> ServiceDetail",
                                                                              discovery_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.service == message_20::to_underlying_value(message_20::datatypes::ServiceCategory::DC));
    }

    // ServiceDetailResponse(OK, one param set) -> ServiceSelectionRequest.
    message_20::ServiceDetailResponse detail_res{};
    detail_res.header.session_id = sid;
    detail_res.response_code = ResponseCode::OK;
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
    message_20::ServiceSelectionResponse selection_res{};
    selection_res.header.session_id = sid;
    selection_res.response_code = ResponseCode::OK;
    {
        const auto req = inject_then_expect<message_20::DC_ChargeParameterDiscoveryRequest>(
            fx, "ServiceSelection -> DC_ChargeParameterDiscovery", selection_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(std::holds_alternative<message_20::datatypes::DC_CPDReqEnergyTransferMode>(req.transfer_mode));
    }

    // DC_ChargeParameterDiscoveryResponse(OK) -> ScheduleExchangeRequest.
    message_20::DC_ChargeParameterDiscoveryResponse cpd_res{};
    cpd_res.header.session_id = sid;
    cpd_res.response_code = ResponseCode::OK;
    cpd_res.transfer_mode = message_20::datatypes::DC_CPDResEnergyTransferMode{};
    {
        const auto req = inject_then_expect<message_20::ScheduleExchangeRequest>(
            fx, "DC_ChargeParameterDiscovery -> ScheduleExchange", cpd_res, PT::Part20DC);
        REQUIRE(req.header.session_id == sid);
    }

    // ScheduleExchangeResponse(OK, Finished) -> DC_CableCheckRequest; fires ev_power_ready.
    REQUIRE_FALSE(fx.ev_power_ready);
    message_20::ScheduleExchangeResponse schedule_res{};
    schedule_res.header.session_id = sid;
    schedule_res.response_code = ResponseCode::OK;
    schedule_res.processing = Processing::Finished;
    {
        const auto req = inject_then_expect<message_20::DC_CableCheckRequest>(fx, "ScheduleExchange -> DC_CableCheck",
                                                                              schedule_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
    }
    REQUIRE(fx.ev_power_ready);

    // DC_CableCheckResponse(OK, Finished) -> DC_PreChargeRequest(Ongoing).
    message_20::DC_CableCheckResponse cable_check_res{};
    cable_check_res.header.session_id = sid;
    cable_check_res.response_code = ResponseCode::OK;
    cable_check_res.processing = Processing::Finished;
    {
        const auto req = inject_then_expect<message_20::DC_PreChargeRequest>(fx, "DC_CableCheck -> DC_PreCharge",
                                                                             cable_check_res, PT::Part20DC);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.processing == Processing::Ongoing);
    }

    // DC_PreChargeResponse(OK, in-tolerance) -> PowerDeliveryRequest(Start); fires dc_power_on.
    REQUIRE_FALSE(fx.dc_power_on);
    message_20::DC_PreChargeResponse pre_charge_res{};
    pre_charge_res.header.session_id = sid;
    pre_charge_res.response_code = ResponseCode::OK;
    pre_charge_res.present_voltage = message_20::datatypes::from_float(400.0f);
    {
        const auto req = inject_then_expect<message_20::PowerDeliveryRequest>(
            fx, "DC_PreCharge -> PowerDelivery(Start)", pre_charge_res, PT::Part20DC);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.charge_progress == message_20::datatypes::Progress::Start);
    }
    REQUIRE(fx.dc_power_on);

    // PowerDeliveryResponse(OK) -> DC_ChargeLoopRequest (Dynamic control mode).
    message_20::PowerDeliveryResponse power_delivery_res{};
    power_delivery_res.header.session_id = sid;
    power_delivery_res.response_code = ResponseCode::OK;
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
    message_20::PowerDeliveryResponse power_delivery_res{};
    power_delivery_res.header.session_id = sid;
    power_delivery_res.response_code = ResponseCode::OK;
    {
        const auto req = inject_then_expect<message_20::DC_WeldingDetectionRequest>(
            fx, "PowerDelivery(Stop) -> DC_WeldingDetection", power_delivery_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
    }

    message_20::DC_WeldingDetectionResponse welding_res{};
    welding_res.header.session_id = sid;
    welding_res.response_code = ResponseCode::OK;
    welding_res.present_voltage = message_20::datatypes::from_float(0.0f);
    {
        const auto req = inject_then_expect<message_20::SessionStopRequest>(fx, "DC_WeldingDetection -> SessionStop",
                                                                            welding_res, PT::Part20DC);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.charging_session == message_20::datatypes::ChargingSession::Terminate);
    }

    // SessionStopResponse(OK) -> session finishes cleanly (no watchdog park).
    message_20::SessionStopResponse stop_res{};
    stop_res.header.session_id = sid;
    stop_res.response_code = ResponseCode::OK;
    fx.session.on_bytes_received(frame_payload(PT::Part20Main, serialize_msg(stop_res)));
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.session.is_finished(); }, 1s));
    REQUIRE(fx.session.is_finished());
    REQUIRE_FALSE(fx.timed_out);
}

// Walk an AC-configured ev::Session from start() through the first AC_ChargeLoopRequest,
// asserting the emitted request at each step and the feedback fired along the way. The
// energy service routes past the DC-only states: ScheduleExchange -> PowerDelivery(Start)
// -> AC_ChargeLoop, never DC_CableCheck/DC_PreCharge. Returns the established session id.
message_20::datatypes::SessionId walk_to_ac_charge_loop(SessionFixture& fx) {
    const auto sid = WALK_SESSION_ID;

    // start() -> SupportedAppProtocolRequest advertising the -20:AC entry (schema id 1).
    fx.session.start();
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
    REQUIRE(decode_frame(fx.captured.back()).get_if<message_20::SupportedAppProtocolRequest>() != nullptr);

    // SupportedAppProtocolResponse(schema 1) -> SessionSetupRequest.
    inject_then_expect<message_20::SessionSetupRequest>(
        fx, "SAP -> SessionSetup",
        message_20::SupportedAppProtocolResponse{
            message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1},
        PT::SAP);

    // SessionSetupResponse -> AuthorizationSetupRequest.
    message_20::SessionSetupResponse setup_res{};
    setup_res.response_code = ResponseCode::OK_NewSessionEstablished;
    setup_res.header.session_id = sid;
    setup_res.evseid = "DE*PNX*E12345";
    inject_then_expect<message_20::AuthorizationSetupRequest>(fx, "SessionSetup -> AuthorizationSetup", setup_res,
                                                              PT::Part20Main);

    // AuthorizationSetupResponse(OK, {EIM}) -> AuthorizationRequest (selects EIM).
    message_20::AuthorizationSetupResponse auth_setup_res{};
    auth_setup_res.header.session_id = sid;
    auth_setup_res.response_code = ResponseCode::OK;
    auth_setup_res.authorization_services = {message_20::datatypes::Authorization::EIM};
    auth_setup_res.certificate_installation_service = false;
    auth_setup_res.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
    {
        const auto req = inject_then_expect<message_20::AuthorizationRequest>(fx, "AuthorizationSetup -> Authorization",
                                                                              auth_setup_res, PT::Part20Main);
        REQUIRE(req.selected_authorization_service == message_20::datatypes::Authorization::EIM);
        REQUIRE(std::holds_alternative<message_20::datatypes::EIM_ASReqAuthorizationMode>(req.authorization_mode));
    }

    // AuthorizationResponse(OK, Finished) -> ServiceDiscoveryRequest.
    message_20::AuthorizationResponse auth_res{};
    auth_res.header.session_id = sid;
    auth_res.response_code = ResponseCode::OK;
    auth_res.evse_processing = Processing::Finished;
    inject_then_expect<message_20::ServiceDiscoveryRequest>(fx, "Authorization -> ServiceDiscovery", auth_res,
                                                            PT::Part20Main);

    // ServiceDiscoveryResponse(OK, offering AC service id 1) -> ServiceDetailRequest(service=AC).
    message_20::ServiceDiscoveryResponse discovery_res{};
    discovery_res.header.session_id = sid;
    discovery_res.response_code = ResponseCode::OK;
    discovery_res.energy_transfer_service_list = {{message_20::datatypes::ServiceCategory::AC, false}};
    {
        const auto req = inject_then_expect<message_20::ServiceDetailRequest>(fx, "ServiceDiscovery -> ServiceDetail",
                                                                              discovery_res, PT::Part20Main);
        REQUIRE(req.service == message_20::to_underlying_value(message_20::datatypes::ServiceCategory::AC));
    }

    // ServiceDetailResponse(OK, a Dynamic parameter set) -> ServiceSelectionRequest.
    message_20::ServiceDetailResponse detail_res{};
    detail_res.header.session_id = sid;
    detail_res.response_code = ResponseCode::OK;
    detail_res.service = message_20::to_underlying_value(message_20::datatypes::ServiceCategory::AC);
    detail_res.service_parameter_list = {make_param_set(9, ControlMode::Dynamic)};
    {
        const auto req = inject_then_expect<message_20::ServiceSelectionRequest>(
            fx, "ServiceDetail -> ServiceSelection", detail_res, PT::Part20Main);
        REQUIRE(req.selected_energy_transfer_service.service_id == message_20::datatypes::ServiceCategory::AC);
        REQUIRE(req.selected_energy_transfer_service.parameter_set_id == 9);
    }

    // ServiceSelectionResponse(OK) -> AC_ChargeParameterDiscoveryRequest carrying the seeded limits.
    message_20::ServiceSelectionResponse selection_res{};
    selection_res.header.session_id = sid;
    selection_res.response_code = ResponseCode::OK;
    {
        const auto req = inject_then_expect<message_20::AC_ChargeParameterDiscoveryRequest>(
            fx, "ServiceSelection -> AC_ChargeParameterDiscovery", selection_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        const auto* mode = std::get_if<message_20::datatypes::AC_CPDReqEnergyTransferMode>(&req.transfer_mode);
        REQUIRE(mode != nullptr);
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_charge_power) ==
                Catch::Approx(AC_MAX_CHARGE_POWER));
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->min_charge_power) ==
                Catch::Approx(AC_MIN_CHARGE_POWER));
    }

    // AC_ChargeParameterDiscoveryResponse(OK) -> ScheduleExchangeRequest; fires ac_limits.
    REQUIRE_FALSE(fx.ac_limits);
    message_20::AC_ChargeParameterDiscoveryResponse cpd_res{};
    cpd_res.header.session_id = sid;
    cpd_res.response_code = ResponseCode::OK;
    {
        message_20::datatypes::AC_CPDResEnergyTransferMode mode{};
        mode.max_charge_power = message_20::datatypes::from_float(AC_MAX_CHARGE_POWER);
        mode.min_charge_power = message_20::datatypes::from_float(AC_MIN_CHARGE_POWER);
        mode.nominal_frequency = message_20::datatypes::from_float(50.0f);
        cpd_res.transfer_mode = mode;
    }
    {
        const auto req = inject_then_expect<message_20::ScheduleExchangeRequest>(
            fx, "AC_ChargeParameterDiscovery -> ScheduleExchange", cpd_res, PT::Part20AC);
        REQUIRE(req.header.session_id == sid);
    }
    REQUIRE(fx.ac_limits);

    // ScheduleExchangeResponse(OK, Finished) -> PowerDeliveryRequest(Start); fires ev_power_ready.
    // The Start progress (not a DC_CableCheckRequest) is the AC routing assertion.
    REQUIRE_FALSE(fx.ev_power_ready);
    message_20::ScheduleExchangeResponse schedule_res{};
    schedule_res.header.session_id = sid;
    schedule_res.response_code = ResponseCode::OK;
    schedule_res.processing = Processing::Finished;
    {
        const auto req = inject_then_expect<message_20::PowerDeliveryRequest>(
            fx, "ScheduleExchange -> PowerDelivery(Start)", schedule_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.charge_progress == message_20::datatypes::Progress::Start);
    }
    REQUIRE(fx.ev_power_ready);

    // PowerDeliveryResponse(OK) -> AC_ChargeLoopRequest (Dynamic control mode). No target
    // setpoint has arrived yet, so ac_target_power has not fired.
    REQUIRE_FALSE(fx.ac_target_power);
    message_20::PowerDeliveryResponse power_delivery_res{};
    power_delivery_res.header.session_id = sid;
    power_delivery_res.response_code = ResponseCode::OK;
    {
        const auto req = inject_then_expect<message_20::AC_ChargeLoopRequest>(fx, "PowerDelivery -> AC_ChargeLoop",
                                                                              power_delivery_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_AC_CLReqControlMode>(req.control_mode));
        const auto& mode = std::get<message_20::datatypes::Dynamic_AC_CLReqControlMode>(req.control_mode);
        REQUIRE(message_20::datatypes::from_RationalNumber(mode.present_active_power) ==
                Catch::Approx(AC_PRESENT_ACTIVE_POWER));
    }
    REQUIRE_FALSE(fx.ac_target_power);

    return sid;
}

// Drive PowerDelivery(Stop) -> SessionStop -> clean finish from an emitted
// PowerDeliveryRequest(Stop). AC skips WeldingDetection, so the request expected
// after the PowerDelivery(Stop) response is SessionStop, not DC_WeldingDetection.
void walk_ac_stop_to_finish(SessionFixture& fx, const message_20::datatypes::SessionId& sid) {
    message_20::PowerDeliveryResponse power_delivery_res{};
    power_delivery_res.header.session_id = sid;
    power_delivery_res.response_code = ResponseCode::OK;
    {
        const auto req = inject_then_expect<message_20::SessionStopRequest>(fx, "PowerDelivery(Stop) -> SessionStop",
                                                                            power_delivery_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(req.charging_session == message_20::datatypes::ChargingSession::Terminate);
    }

    message_20::SessionStopResponse stop_res{};
    stop_res.header.session_id = sid;
    stop_res.response_code = ResponseCode::OK;
    fx.session.on_bytes_received(frame_payload(PT::Part20Main, serialize_msg(stop_res)));
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.session.is_finished(); }, 1s));
    REQUIRE(fx.session.is_finished());
    REQUIRE_FALSE(fx.timed_out);
}

// Seeded AC charge params the AC_CPD/AC_ChargeLoop requests are built from.
ev::AcChargeParams ac_seed_params() {
    ev::AcChargeParams p{};
    p.max_charge_power = AC_MAX_CHARGE_POWER;
    p.min_charge_power = AC_MIN_CHARGE_POWER;
    p.present_active_power = AC_PRESENT_ACTIVE_POWER;
    return p;
}

} // namespace

SCENARIO("ISO15118-20 EV Session drives the states byte-by-byte through a full DC session to SessionStop") {

    GIVEN("A Session bound to a reactor with short send-delay and watchdog timers") {
        SessionFixture fx;

        WHEN("the session is walked to an active DC_ChargeLoop and the SECC then signals Terminate") {
            const auto sid = walk_to_dc_charge_loop(fx);

            // The walk reached the charge loop without finishing or timing out.
            REQUIRE(fx.captured.size() == 13);
            REQUIRE_FALSE(fx.session.is_finished());
            REQUIRE_FALSE(fx.timed_out);

            // DC_ChargeLoopResponse(OK, no Terminate) -> DC_ChargeLoopRequest (loop continues).
            REQUIRE_FALSE(fx.stop_from_charger);
            message_20::DC_ChargeLoopResponse loop_ok{};
            loop_ok.header.session_id = sid;
            loop_ok.response_code = ResponseCode::OK;
            loop_ok.control_mode = message_20::datatypes::Dynamic_DC_CLResControlMode{};
            inject_then_expect<message_20::DC_ChargeLoopRequest>(fx, "DC_ChargeLoop OK -> DC_ChargeLoop", loop_ok,
                                                                 PT::Part20DC);
            REQUIRE_FALSE(fx.stop_from_charger);

            // DC_ChargeLoopResponse(OK, Terminate) -> PowerDeliveryRequest(Stop); fires stop_from_charger.
            message_20::DC_ChargeLoopResponse loop_terminate{};
            loop_terminate.header.session_id = sid;
            loop_terminate.response_code = ResponseCode::OK;
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

            message_20::DC_ChargeLoopResponse loop_ok{};
            loop_ok.header.session_id = sid;
            loop_ok.response_code = ResponseCode::OK;
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

SCENARIO("ISO15118-20 EV Session drives the states byte-by-byte through a full AC session to SessionStop") {

    GIVEN("An AC-configured Session bound to a reactor with short send-delay and watchdog timers") {
        SessionFixture fx{"EVTESTID01",
                       ev::SessionTiming{5ms, 100ms},
                       ev::DcChargeParams{},
                       default_advertised_ac_app_protocols(),
                       message_20::datatypes::ServiceCategory::AC,
                       ac_seed_params()};

        WHEN("the session is walked to an active AC_ChargeLoop and the SECC then signals Terminate") {
            const auto sid = walk_to_ac_charge_loop(fx);

            // The walk reached the charge loop past the DC-only states without finishing.
            REQUIRE(fx.captured.size() == 11);
            REQUIRE_FALSE(fx.session.is_finished());
            REQUIRE_FALSE(fx.timed_out);

            // AC_ChargeLoopResponse(OK, Dynamic target) -> AC_ChargeLoopRequest; fires ac_target_power.
            REQUIRE_FALSE(fx.ac_target_power);
            message_20::AC_ChargeLoopResponse loop_ok{};
            loop_ok.header.session_id = sid;
            loop_ok.response_code = ResponseCode::OK;
            {
                message_20::datatypes::Dynamic_AC_CLResControlMode mode{};
                mode.target_active_power = message_20::datatypes::from_float(AC_TARGET_ACTIVE_POWER);
                loop_ok.control_mode = mode;
            }
            inject_then_expect<message_20::AC_ChargeLoopRequest>(fx, "AC_ChargeLoop OK -> AC_ChargeLoop", loop_ok,
                                                                 PT::Part20AC);
            REQUIRE(fx.ac_target_power);
            REQUIRE_FALSE(fx.stop_from_charger);

            // AC_ChargeLoopResponse(OK, Terminate) -> PowerDeliveryRequest(Stop); fires stop_from_charger.
            message_20::AC_ChargeLoopResponse loop_terminate{};
            loop_terminate.header.session_id = sid;
            loop_terminate.response_code = ResponseCode::OK;
            loop_terminate.status =
                message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Terminate};
            loop_terminate.control_mode = message_20::datatypes::Dynamic_AC_CLResControlMode{};
            {
                const auto req = inject_then_expect<message_20::PowerDeliveryRequest>(
                    fx, "AC_ChargeLoop Terminate -> PowerDelivery(Stop)", loop_terminate, PT::Part20AC);
                REQUIRE(req.header.session_id == sid);
                REQUIRE(req.charge_progress == message_20::datatypes::Progress::Stop);
            }
            REQUIRE(fx.stop_from_charger);

            THEN("PowerDelivery(Stop) walks straight to SessionStop, skipping WeldingDetection") {
                walk_ac_stop_to_finish(fx, sid);
                REQUIRE(fx.stop_from_charger);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Session drives a graceful EV-initiated stop from an active AC_ChargeLoop") {
    // A StopCharging control event delivered through Session::deliver_control_event
    // is recorded on the Context and, on the next AC_ChargeLoopResponse, drives
    // PowerDelivery(Stop) -> SessionStop without any SECC Terminate notification.
    GIVEN("An AC-configured Session walked to an active AC_ChargeLoop") {
        SessionFixture fx{"EVTESTID01",
                       ev::SessionTiming{5ms, 100ms},
                       ev::DcChargeParams{},
                       default_advertised_ac_app_protocols(),
                       message_20::datatypes::ServiceCategory::AC,
                       ac_seed_params()};
        const auto sid = walk_to_ac_charge_loop(fx);

        WHEN("a StopCharging control event is delivered and the next OK loop response arrives") {
            // The EV requests a stop mid-loop; the SECC has NOT sent Terminate.
            fx.session.deliver_control_event(ev::d20::StopCharging{true});

            message_20::AC_ChargeLoopResponse loop_ok{};
            loop_ok.header.session_id = sid;
            loop_ok.response_code = ResponseCode::OK;
            {
                message_20::datatypes::Dynamic_AC_CLResControlMode mode{};
                mode.target_active_power = message_20::datatypes::from_float(AC_TARGET_ACTIVE_POWER);
                loop_ok.control_mode = mode;
            }
            {
                const auto req = inject_then_expect<message_20::PowerDeliveryRequest>(
                    fx, "EV-initiated stop -> PowerDelivery(Stop)", loop_ok, PT::Part20AC);
                REQUIRE(req.charge_progress == message_20::datatypes::Progress::Stop);
            }
            // The EV drove the stop; the SECC-Terminate feedback must NOT have fired.
            REQUIRE_FALSE(fx.stop_from_charger);

            THEN("the loop breaks into PowerDelivery(Stop) and walks to a clean SessionStop") {
                walk_ac_stop_to_finish(fx, sid);
                REQUIRE_FALSE(fx.stop_from_charger);
            }
        }
    }
}
