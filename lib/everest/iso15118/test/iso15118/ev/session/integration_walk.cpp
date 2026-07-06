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

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/der_control_functions.hpp>

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

// Discharge limits seeded for the AC_BPT walk; asserted on the AC_BPT CPD and
// charge-loop requests.
constexpr float AC_MAX_DISCHARGE_POWER = 15000.0f;
constexpr float AC_MIN_DISCHARGE_POWER = 500.0f;

// DC limits seeded into the DC_BPT fixture; charge fields drive the plain DC
// request slice, discharge fields the BPT extension.
constexpr float DC_MAX_CHARGE_POWER = 50000.0f;
constexpr float DC_MAX_CHARGE_CURRENT = 125.0f;
constexpr float DC_MAX_VOLTAGE = 900.0f;
constexpr float DC_MIN_VOLTAGE = 200.0f;
constexpr float DC_MAX_DISCHARGE_POWER = 30000.0f;
constexpr float DC_MIN_DISCHARGE_POWER = 1000.0f;
constexpr float DC_MAX_DISCHARGE_CURRENT = 80.0f;

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

std::bitset<iso15118::ev::DER_CONTROL_FUNCTION_COUNT>
der_mask(std::initializer_list<iso15118::iec::DERControlName> functions) {
    std::bitset<iso15118::ev::DER_CONTROL_FUNCTION_COUNT> mask;
    for (const auto function : functions) {
        mask.set(static_cast<std::size_t>(function));
    }
    return mask;
}

// A ServiceDetail parameter set carrying a DERControlFunctions bitmask, as the SECC
// encodes it for AC_DER_IEC sets.
ParameterSet make_der_param_set(uint16_t id, ControlMode control_mode,
                                std::bitset<iso15118::ev::DER_CONTROL_FUNCTION_COUNT> functions_mask) {
    auto set = make_param_set(id, control_mode);
    set.parameter.push_back({"DERControlFunctions", static_cast<int32_t>(functions_mask.to_ulong())});
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

// Walk an AC_DER_IEC-configured ev::Session from start() through the first
// DER_AC_ChargeLoopRequest. The energy service (service id 10) routes through the DER
// parameter-discovery and charge-loop states, past the DC-only path, exactly like
// plain AC: ScheduleExchange -> PowerDelivery(Start) -> AC_DER_IEC_ChargeLoop.
message_20::datatypes::SessionId walk_to_ac_der_iec_charge_loop(SessionFixture& fx) {
    const auto sid = WALK_SESSION_ID;
    using SC = message_20::datatypes::ServiceCategory;

    fx.session.start();
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
    REQUIRE(decode_frame(fx.captured.back()).get_if<message_20::SupportedAppProtocolRequest>() != nullptr);

    inject_then_expect<message_20::SessionSetupRequest>(
        fx, "SAP -> SessionSetup",
        message_20::SupportedAppProtocolResponse{
            message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1},
        PT::SAP);

    message_20::SessionSetupResponse setup_res{};
    setup_res.response_code = ResponseCode::OK_NewSessionEstablished;
    setup_res.header.session_id = sid;
    setup_res.evseid = "DE*PNX*E12345";
    inject_then_expect<message_20::AuthorizationSetupRequest>(fx, "SessionSetup -> AuthorizationSetup", setup_res,
                                                              PT::Part20Main);

    message_20::AuthorizationSetupResponse auth_setup_res{};
    auth_setup_res.header.session_id = sid;
    auth_setup_res.response_code = ResponseCode::OK;
    auth_setup_res.authorization_services = {message_20::datatypes::Authorization::EIM};
    auth_setup_res.certificate_installation_service = false;
    auth_setup_res.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
    inject_then_expect<message_20::AuthorizationRequest>(fx, "AuthorizationSetup -> Authorization", auth_setup_res,
                                                         PT::Part20Main);

    message_20::AuthorizationResponse auth_res{};
    auth_res.header.session_id = sid;
    auth_res.response_code = ResponseCode::OK;
    auth_res.evse_processing = Processing::Finished;
    inject_then_expect<message_20::ServiceDiscoveryRequest>(fx, "Authorization -> ServiceDiscovery", auth_res,
                                                            PT::Part20Main);

    // ServiceDiscoveryResponse offering the AC_DER_IEC service (service id 10) -> ServiceDetailRequest.
    message_20::ServiceDiscoveryResponse discovery_res{};
    discovery_res.header.session_id = sid;
    discovery_res.response_code = ResponseCode::OK;
    discovery_res.energy_transfer_service_list = {{SC::AC_DER_IEC, false}};
    {
        const auto req = inject_then_expect<message_20::ServiceDetailRequest>(fx, "ServiceDiscovery -> ServiceDetail",
                                                                              discovery_res, PT::Part20Main);
        REQUIRE(req.service == message_20::to_underlying_value(SC::AC_DER_IEC));
    }

    // ServiceDetailResponse with a Dynamic parameter set whose DERControlFunctions are a
    // subset of the fixture's supported set (the two DSO setpoints) -> ServiceSelectionRequest.
    message_20::ServiceDetailResponse detail_res{};
    detail_res.header.session_id = sid;
    detail_res.response_code = ResponseCode::OK;
    detail_res.service = message_20::to_underlying_value(SC::AC_DER_IEC);
    detail_res.service_parameter_list = {
        make_der_param_set(10, ControlMode::Dynamic,
                           der_mask({iso15118::iec::DERControlName::DSOQSetpointProvision,
                                     iso15118::iec::DERControlName::DSOCosPhiSetpointProvision}))};
    {
        const auto req = inject_then_expect<message_20::ServiceSelectionRequest>(
            fx, "ServiceDetail -> ServiceSelection", detail_res, PT::Part20Main);
        REQUIRE(req.selected_energy_transfer_service.service_id == SC::AC_DER_IEC);
        REQUIRE(req.selected_energy_transfer_service.parameter_set_id == 10);
    }

    // ServiceSelectionResponse (injected as Part20Main) -> DER_AC_ChargeParameterDiscoveryRequest
    // carrying charge and discharge limits.
    message_20::ServiceSelectionResponse selection_res{};
    selection_res.header.session_id = sid;
    selection_res.response_code = ResponseCode::OK;
    {
        const auto req = inject_then_expect<message_20::DER_AC_ChargeParameterDiscoveryRequest>(
            fx, "ServiceSelection -> AC_DER_IEC_ChargeParameterDiscovery", selection_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        // The emitted DER request rides the DER-IEC payload type on the wire.
        REQUIRE(header_payload_type(fx.captured.back()) == PT::Part20DerIec);
        REQUIRE(message_20::datatypes::from_RationalNumber(req.transfer_mode.max_charge_power) ==
                Catch::Approx(AC_MAX_CHARGE_POWER));
        REQUIRE(message_20::datatypes::from_RationalNumber(req.transfer_mode.max_discharge_power) ==
                Catch::Approx(AC_MAX_CHARGE_POWER));
    }

    // DER_AC_ChargeParameterDiscoveryResponse(OK) -> ScheduleExchangeRequest; fires ac_limits.
    REQUIRE_FALSE(fx.ac_limits);
    message_20::DER_AC_ChargeParameterDiscoveryResponse cpd_res{};
    cpd_res.header.session_id = sid;
    cpd_res.response_code = ResponseCode::OK;
    {
        auto& mode = cpd_res.transfer_mode;
        mode.max_charge_power = message_20::datatypes::from_float(AC_MAX_CHARGE_POWER);
        mode.min_charge_power = message_20::datatypes::from_float(AC_MIN_CHARGE_POWER);
        mode.nominal_frequency = message_20::datatypes::from_float(50.0f);
        mode.nominal_charge_power = message_20::datatypes::from_float(AC_MAX_CHARGE_POWER);
        mode.nominal_discharge_power = message_20::datatypes::from_float(AC_MAX_CHARGE_POWER);
        mode.max_discharge_power = message_20::datatypes::from_float(AC_MAX_CHARGE_POWER);
    }
    {
        const auto req = inject_then_expect<message_20::ScheduleExchangeRequest>(
            fx, "AC_DER_IEC_ChargeParameterDiscovery -> ScheduleExchange", cpd_res, PT::Part20DerIec);
        REQUIRE(req.header.session_id == sid);
    }
    REQUIRE(fx.ac_limits);

    // ScheduleExchangeResponse(OK, Finished) -> PowerDeliveryRequest(Start); fires ev_power_ready.
    // The Start progress (not a DC_CableCheckRequest) is the DER-as-AC routing assertion.
    REQUIRE_FALSE(fx.ev_power_ready);
    message_20::ScheduleExchangeResponse schedule_res{};
    schedule_res.header.session_id = sid;
    schedule_res.response_code = ResponseCode::OK;
    schedule_res.processing = Processing::Finished;
    {
        const auto req = inject_then_expect<message_20::PowerDeliveryRequest>(
            fx, "ScheduleExchange -> PowerDelivery(Start)", schedule_res, PT::Part20Main);
        REQUIRE(req.charge_progress == message_20::datatypes::Progress::Start);
    }
    REQUIRE(fx.ev_power_ready);

    // PowerDeliveryResponse(OK) -> DER_AC_ChargeLoopRequest (Dynamic DER control mode).
    REQUIRE_FALSE(fx.der_control);
    message_20::PowerDeliveryResponse power_delivery_res{};
    power_delivery_res.header.session_id = sid;
    power_delivery_res.response_code = ResponseCode::OK;
    {
        const auto req = inject_then_expect<message_20::DER_AC_ChargeLoopRequest>(
            fx, "PowerDelivery -> AC_DER_IEC_ChargeLoop", power_delivery_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        REQUIRE(std::holds_alternative<message_20::datatypes::DER_Dynamic_AC_CLReqControlMode>(req.control_mode));
        const auto& mode = std::get<message_20::datatypes::DER_Dynamic_AC_CLReqControlMode>(req.control_mode);
        REQUIRE(message_20::datatypes::from_RationalNumber(mode.present_active_power) ==
                Catch::Approx(AC_PRESENT_ACTIVE_POWER));
    }
    REQUIRE_FALSE(fx.der_control);

    return sid;
}

// A DER_AC_ChargeLoopResponse carrying a Dynamic control mode; target_active_power set
// so the der_control feedback observation is meaningful.
message_20::DER_AC_ChargeLoopResponse make_der_loop_res(const message_20::datatypes::SessionId& sid) {
    message_20::DER_AC_ChargeLoopResponse res{};
    res.header.session_id = sid;
    res.response_code = ResponseCode::OK;
    message_20::datatypes::DER_Dynamic_AC_CLResControlMode mode{};
    mode.target_active_power = message_20::datatypes::from_float(AC_TARGET_ACTIVE_POWER);
    mode.max_charge_power = message_20::datatypes::from_float(AC_MAX_CHARGE_POWER);
    mode.max_discharge_power = message_20::datatypes::from_float(AC_MAX_CHARGE_POWER);
    res.control_mode = mode;
    return res;
}

// Walk an AC_DER_IEC-configured ev::Session from start() up to the emitted
ev::AcChargeParams ac_bpt_seed_params() {
    auto p = ac_seed_params();
    p.max_discharge_power = AC_MAX_DISCHARGE_POWER;
    p.min_discharge_power = AC_MIN_DISCHARGE_POWER;
    return p;
}

// DC charge params seeded with charge and discharge fields for the DC_BPT walk.
// target_voltage matches the injected DC_PreChargeResponse present voltage so
// precharge converges in one step, mirroring the plain-DC walk.
ev::DcChargeParams dc_bpt_seed_params() {
    ev::DcChargeParams p{};
    p.max_charge_power = DC_MAX_CHARGE_POWER;
    p.max_charge_current = DC_MAX_CHARGE_CURRENT;
    p.max_voltage = DC_MAX_VOLTAGE;
    p.min_voltage = DC_MIN_VOLTAGE;
    p.max_discharge_power = DC_MAX_DISCHARGE_POWER;
    p.min_discharge_power = DC_MIN_DISCHARGE_POWER;
    p.max_discharge_current = DC_MAX_DISCHARGE_CURRENT;
    p.target_voltage = 400.0f;
    return p;
}

// Walk an AC_BPT-configured ev::Session from start() through the first BPT AC_ChargeLoop
// request. Service id 5 routes through AC parameter-discovery and charge-loop states with
// the BPT request variants, past the DC-only path exactly like plain AC:
// ScheduleExchange -> PowerDelivery(Start) -> AC_ChargeLoop (BPT). Returns the session id.
message_20::datatypes::SessionId walk_to_ac_bpt_charge_loop(SessionFixture& fx) {
    const auto sid = WALK_SESSION_ID;
    using SC = message_20::datatypes::ServiceCategory;

    fx.session.start();
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
    REQUIRE(decode_frame(fx.captured.back()).get_if<message_20::SupportedAppProtocolRequest>() != nullptr);

    inject_then_expect<message_20::SessionSetupRequest>(
        fx, "SAP -> SessionSetup",
        message_20::SupportedAppProtocolResponse{
            message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1},
        PT::SAP);

    message_20::SessionSetupResponse setup_res{};
    setup_res.response_code = ResponseCode::OK_NewSessionEstablished;
    setup_res.header.session_id = sid;
    setup_res.evseid = "DE*PNX*E12345";
    inject_then_expect<message_20::AuthorizationSetupRequest>(fx, "SessionSetup -> AuthorizationSetup", setup_res,
                                                              PT::Part20Main);

    message_20::AuthorizationSetupResponse auth_setup_res{};
    auth_setup_res.header.session_id = sid;
    auth_setup_res.response_code = ResponseCode::OK;
    auth_setup_res.authorization_services = {message_20::datatypes::Authorization::EIM};
    auth_setup_res.certificate_installation_service = false;
    auth_setup_res.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
    inject_then_expect<message_20::AuthorizationRequest>(fx, "AuthorizationSetup -> Authorization", auth_setup_res,
                                                         PT::Part20Main);

    message_20::AuthorizationResponse auth_res{};
    auth_res.header.session_id = sid;
    auth_res.response_code = ResponseCode::OK;
    auth_res.evse_processing = Processing::Finished;
    inject_then_expect<message_20::ServiceDiscoveryRequest>(fx, "Authorization -> ServiceDiscovery", auth_res,
                                                            PT::Part20Main);

    // ServiceDiscoveryResponse offering the AC_BPT service (id 5) -> ServiceDetailRequest.
    message_20::ServiceDiscoveryResponse discovery_res{};
    discovery_res.header.session_id = sid;
    discovery_res.response_code = ResponseCode::OK;
    discovery_res.energy_transfer_service_list = {{SC::AC_BPT, false}};
    {
        const auto req = inject_then_expect<message_20::ServiceDetailRequest>(fx, "ServiceDiscovery -> ServiceDetail",
                                                                              discovery_res, PT::Part20Main);
        REQUIRE(req.service == message_20::to_underlying_value(SC::AC_BPT));
    }

    // ServiceDetailResponse with a Dynamic parameter set -> ServiceSelectionRequest.
    message_20::ServiceDetailResponse detail_res{};
    detail_res.header.session_id = sid;
    detail_res.response_code = ResponseCode::OK;
    detail_res.service = message_20::to_underlying_value(SC::AC_BPT);
    detail_res.service_parameter_list = {make_param_set(5, ControlMode::Dynamic)};
    {
        const auto req = inject_then_expect<message_20::ServiceSelectionRequest>(
            fx, "ServiceDetail -> ServiceSelection", detail_res, PT::Part20Main);
        REQUIRE(req.selected_energy_transfer_service.service_id == SC::AC_BPT);
        REQUIRE(req.selected_energy_transfer_service.parameter_set_id == 5);
    }

    // ServiceSelectionResponse(OK) -> AC_ChargeParameterDiscoveryRequest carrying the BPT
    // request variant with the seeded charge and discharge limits.
    message_20::ServiceSelectionResponse selection_res{};
    selection_res.header.session_id = sid;
    selection_res.response_code = ResponseCode::OK;
    {
        const auto req = inject_then_expect<message_20::AC_ChargeParameterDiscoveryRequest>(
            fx, "ServiceSelection -> AC_ChargeParameterDiscovery", selection_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        const auto* mode = std::get_if<message_20::datatypes::BPT_AC_CPDReqEnergyTransferMode>(&req.transfer_mode);
        REQUIRE(mode != nullptr);
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_charge_power) ==
                Catch::Approx(AC_MAX_CHARGE_POWER));
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_discharge_power) ==
                Catch::Approx(AC_MAX_DISCHARGE_POWER));
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->min_discharge_power) ==
                Catch::Approx(AC_MIN_DISCHARGE_POWER));
    }

    // BPT AC_ChargeParameterDiscoveryResponse(OK) -> ScheduleExchangeRequest; fires ac_bpt_limits.
    REQUIRE_FALSE(fx.ac_bpt_limits);
    message_20::AC_ChargeParameterDiscoveryResponse cpd_res{};
    cpd_res.header.session_id = sid;
    cpd_res.response_code = ResponseCode::OK;
    {
        message_20::datatypes::BPT_AC_CPDResEnergyTransferMode mode{};
        mode.max_charge_power = message_20::datatypes::from_float(AC_MAX_CHARGE_POWER);
        mode.min_charge_power = message_20::datatypes::from_float(AC_MIN_CHARGE_POWER);
        mode.nominal_frequency = message_20::datatypes::from_float(50.0f);
        mode.max_discharge_power = message_20::datatypes::from_float(AC_MAX_DISCHARGE_POWER);
        mode.min_discharge_power = message_20::datatypes::from_float(AC_MIN_DISCHARGE_POWER);
        cpd_res.transfer_mode = mode;
    }
    {
        const auto req = inject_then_expect<message_20::ScheduleExchangeRequest>(
            fx, "AC_ChargeParameterDiscovery -> ScheduleExchange", cpd_res, PT::Part20AC);
        REQUIRE(req.header.session_id == sid);
    }
    REQUIRE(fx.ac_bpt_limits);
    REQUIRE_FALSE(fx.ac_limits);

    // ScheduleExchangeResponse(OK, Finished) -> PowerDeliveryRequest(Start); the Start
    // progress (not a DC_CableCheckRequest) is the AC routing assertion.
    message_20::ScheduleExchangeResponse schedule_res{};
    schedule_res.header.session_id = sid;
    schedule_res.response_code = ResponseCode::OK;
    schedule_res.processing = Processing::Finished;
    {
        const auto req = inject_then_expect<message_20::PowerDeliveryRequest>(
            fx, "ScheduleExchange -> PowerDelivery(Start)", schedule_res, PT::Part20Main);
        REQUIRE(req.charge_progress == message_20::datatypes::Progress::Start);
    }

    // PowerDeliveryResponse(OK) -> AC_ChargeLoopRequest carrying the BPT Dynamic control mode.
    message_20::PowerDeliveryResponse power_delivery_res{};
    power_delivery_res.header.session_id = sid;
    power_delivery_res.response_code = ResponseCode::OK;
    {
        const auto req = inject_then_expect<message_20::AC_ChargeLoopRequest>(fx, "PowerDelivery -> AC_ChargeLoop",
                                                                              power_delivery_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        const auto* mode = std::get_if<message_20::datatypes::BPT_Dynamic_AC_CLReqControlMode>(&req.control_mode);
        REQUIRE(mode != nullptr);
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->present_active_power) ==
                Catch::Approx(AC_PRESENT_ACTIVE_POWER));
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_discharge_power) ==
                Catch::Approx(AC_MAX_DISCHARGE_POWER));
    }

    return sid;
}

// A BPT AC_ChargeLoopResponse carrying a BPT Dynamic control mode with a target set so the
// ac_target_power feedback (reading the base Dynamic slice) is meaningful.
message_20::AC_ChargeLoopResponse make_ac_bpt_loop_res(const message_20::datatypes::SessionId& sid) {
    message_20::AC_ChargeLoopResponse res{};
    res.header.session_id = sid;
    res.response_code = ResponseCode::OK;
    message_20::datatypes::BPT_Dynamic_AC_CLResControlMode mode{};
    mode.target_active_power = message_20::datatypes::from_float(AC_TARGET_ACTIVE_POWER);
    res.control_mode = mode;
    return res;
}

// Walk a DC_BPT-configured ev::Session from start() through the first BPT DC_ChargeLoop
// request. Service id 6 uses the BPT request variants for parameter discovery and the
// charge loop; cable-check and precharge stay the plain DC requests: ScheduleExchange ->
// DC_CableCheck -> DC_PreCharge -> PowerDelivery(Start) -> DC_ChargeLoop (BPT).
message_20::datatypes::SessionId walk_to_dc_bpt_charge_loop(SessionFixture& fx) {
    const auto sid = WALK_SESSION_ID;
    using SC = message_20::datatypes::ServiceCategory;

    fx.session.start();
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
    REQUIRE(decode_frame(fx.captured.back()).get_if<message_20::SupportedAppProtocolRequest>() != nullptr);

    inject_then_expect<message_20::SessionSetupRequest>(
        fx, "SAP -> SessionSetup",
        message_20::SupportedAppProtocolResponse{
            message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1},
        PT::SAP);

    message_20::SessionSetupResponse setup_res{};
    setup_res.response_code = ResponseCode::OK_NewSessionEstablished;
    setup_res.header.session_id = sid;
    setup_res.evseid = "DE*PNX*E12345";
    inject_then_expect<message_20::AuthorizationSetupRequest>(fx, "SessionSetup -> AuthorizationSetup", setup_res,
                                                              PT::Part20Main);

    message_20::AuthorizationSetupResponse auth_setup_res{};
    auth_setup_res.header.session_id = sid;
    auth_setup_res.response_code = ResponseCode::OK;
    auth_setup_res.authorization_services = {message_20::datatypes::Authorization::EIM};
    auth_setup_res.certificate_installation_service = false;
    auth_setup_res.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
    inject_then_expect<message_20::AuthorizationRequest>(fx, "AuthorizationSetup -> Authorization", auth_setup_res,
                                                         PT::Part20Main);

    message_20::AuthorizationResponse auth_res{};
    auth_res.header.session_id = sid;
    auth_res.response_code = ResponseCode::OK;
    auth_res.evse_processing = Processing::Finished;
    inject_then_expect<message_20::ServiceDiscoveryRequest>(fx, "Authorization -> ServiceDiscovery", auth_res,
                                                            PT::Part20Main);

    // ServiceDiscoveryResponse offering the DC_BPT service (id 6) -> ServiceDetailRequest.
    message_20::ServiceDiscoveryResponse discovery_res{};
    discovery_res.header.session_id = sid;
    discovery_res.response_code = ResponseCode::OK;
    discovery_res.energy_transfer_service_list = {{SC::DC_BPT, false}};
    {
        const auto req = inject_then_expect<message_20::ServiceDetailRequest>(fx, "ServiceDiscovery -> ServiceDetail",
                                                                              discovery_res, PT::Part20Main);
        REQUIRE(req.service == message_20::to_underlying_value(SC::DC_BPT));
    }

    // ServiceDetailResponse with a Dynamic parameter set -> ServiceSelectionRequest.
    message_20::ServiceDetailResponse detail_res{};
    detail_res.header.session_id = sid;
    detail_res.response_code = ResponseCode::OK;
    detail_res.service = message_20::to_underlying_value(SC::DC_BPT);
    detail_res.service_parameter_list = {make_param_set(6, ControlMode::Dynamic)};
    {
        const auto req = inject_then_expect<message_20::ServiceSelectionRequest>(
            fx, "ServiceDetail -> ServiceSelection", detail_res, PT::Part20Main);
        REQUIRE(req.selected_energy_transfer_service.service_id == SC::DC_BPT);
        REQUIRE(req.selected_energy_transfer_service.parameter_set_id == 6);
    }

    // ServiceSelectionResponse(OK) -> DC_ChargeParameterDiscoveryRequest carrying the BPT
    // request variant with the seeded charge and discharge limits.
    message_20::ServiceSelectionResponse selection_res{};
    selection_res.header.session_id = sid;
    selection_res.response_code = ResponseCode::OK;
    {
        const auto req = inject_then_expect<message_20::DC_ChargeParameterDiscoveryRequest>(
            fx, "ServiceSelection -> DC_ChargeParameterDiscovery", selection_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        const auto* mode = std::get_if<message_20::datatypes::BPT_DC_CPDReqEnergyTransferMode>(&req.transfer_mode);
        REQUIRE(mode != nullptr);
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_charge_power) ==
                Catch::Approx(DC_MAX_CHARGE_POWER));
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_charge_current) ==
                Catch::Approx(DC_MAX_CHARGE_CURRENT));
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_voltage) == Catch::Approx(DC_MAX_VOLTAGE));
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->min_voltage) == Catch::Approx(DC_MIN_VOLTAGE));
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_discharge_power) ==
                Catch::Approx(DC_MAX_DISCHARGE_POWER));
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->min_discharge_power) ==
                Catch::Approx(DC_MIN_DISCHARGE_POWER));
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_discharge_current) ==
                Catch::Approx(DC_MAX_DISCHARGE_CURRENT));
    }

    // BPT DC_ChargeParameterDiscoveryResponse(OK) -> ScheduleExchangeRequest; fires dc_bpt_limits.
    REQUIRE_FALSE(fx.dc_bpt_limits);
    message_20::DC_ChargeParameterDiscoveryResponse cpd_res{};
    cpd_res.header.session_id = sid;
    cpd_res.response_code = ResponseCode::OK;
    {
        message_20::datatypes::BPT_DC_CPDResEnergyTransferMode mode{};
        mode.max_charge_power = message_20::datatypes::from_float(DC_MAX_CHARGE_POWER);
        mode.min_charge_power = message_20::datatypes::from_float(0.0f);
        mode.max_charge_current = message_20::datatypes::from_float(DC_MAX_CHARGE_CURRENT);
        mode.min_charge_current = message_20::datatypes::from_float(0.0f);
        mode.max_voltage = message_20::datatypes::from_float(DC_MAX_VOLTAGE);
        mode.min_voltage = message_20::datatypes::from_float(DC_MIN_VOLTAGE);
        mode.max_discharge_power = message_20::datatypes::from_float(DC_MAX_DISCHARGE_POWER);
        mode.min_discharge_power = message_20::datatypes::from_float(DC_MIN_DISCHARGE_POWER);
        mode.max_discharge_current = message_20::datatypes::from_float(DC_MAX_DISCHARGE_CURRENT);
        mode.min_discharge_current = message_20::datatypes::from_float(0.0f);
        cpd_res.transfer_mode = mode;
    }
    {
        const auto req = inject_then_expect<message_20::ScheduleExchangeRequest>(
            fx, "DC_ChargeParameterDiscovery -> ScheduleExchange", cpd_res, PT::Part20DC);
        REQUIRE(req.header.session_id == sid);
    }
    REQUIRE(fx.dc_bpt_limits);
    REQUIRE_FALSE(fx.ac_bpt_limits);

    // ScheduleExchangeResponse(OK, Finished) -> DC_CableCheckRequest.
    message_20::ScheduleExchangeResponse schedule_res{};
    schedule_res.header.session_id = sid;
    schedule_res.response_code = ResponseCode::OK;
    schedule_res.processing = Processing::Finished;
    {
        const auto req = inject_then_expect<message_20::DC_CableCheckRequest>(fx, "ScheduleExchange -> DC_CableCheck",
                                                                              schedule_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
    }

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

    // DC_PreChargeResponse(OK, in-tolerance) -> PowerDeliveryRequest(Start).
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

    // PowerDeliveryResponse(OK) -> DC_ChargeLoopRequest carrying the BPT Dynamic control mode.
    message_20::PowerDeliveryResponse power_delivery_res{};
    power_delivery_res.header.session_id = sid;
    power_delivery_res.response_code = ResponseCode::OK;
    {
        const auto req = inject_then_expect<message_20::DC_ChargeLoopRequest>(fx, "PowerDelivery -> DC_ChargeLoop",
                                                                              power_delivery_res, PT::Part20Main);
        REQUIRE(req.header.session_id == sid);
        const auto* mode = std::get_if<message_20::datatypes::BPT_Dynamic_DC_CLReqControlMode>(&req.control_mode);
        REQUIRE(mode != nullptr);
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_discharge_power) ==
                Catch::Approx(DC_MAX_DISCHARGE_POWER));
        REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_discharge_current) ==
                Catch::Approx(DC_MAX_DISCHARGE_CURRENT));
    }

    return sid;
}

// A BPT DC_ChargeLoopResponse carrying a BPT Dynamic control mode. The DC loop fires no
// per-response feedback; the variant match is what advances the walk.
message_20::DC_ChargeLoopResponse make_dc_bpt_loop_res(const message_20::datatypes::SessionId& sid) {
    message_20::DC_ChargeLoopResponse res{};
    res.header.session_id = sid;
    res.response_code = ResponseCode::OK;
    message_20::datatypes::BPT_Dynamic_DC_CLResControlMode mode{};
    mode.max_charge_power = message_20::datatypes::from_float(DC_MAX_CHARGE_POWER);
    mode.min_charge_power = message_20::datatypes::from_float(0.0f);
    mode.max_charge_current = message_20::datatypes::from_float(DC_MAX_CHARGE_CURRENT);
    mode.max_voltage = message_20::datatypes::from_float(DC_MAX_VOLTAGE);
    mode.min_voltage = message_20::datatypes::from_float(DC_MIN_VOLTAGE);
    mode.max_discharge_power = message_20::datatypes::from_float(DC_MAX_DISCHARGE_POWER);
    mode.min_discharge_power = message_20::datatypes::from_float(DC_MIN_DISCHARGE_POWER);
    mode.max_discharge_current = message_20::datatypes::from_float(DC_MAX_DISCHARGE_CURRENT);
    res.control_mode = mode;
    return res;
}

// ServiceDetailRequest, so a scenario can then inject a tailored ServiceDetailResponse.
message_20::datatypes::SessionId walk_to_ac_der_iec_service_detail(SessionFixture& fx) {
    const auto sid = WALK_SESSION_ID;
    using SC = message_20::datatypes::ServiceCategory;

    fx.session.start();
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
    REQUIRE(decode_frame(fx.captured.back()).get_if<message_20::SupportedAppProtocolRequest>() != nullptr);

    inject_then_expect<message_20::SessionSetupRequest>(
        fx, "SAP -> SessionSetup",
        message_20::SupportedAppProtocolResponse{
            message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1},
        PT::SAP);

    message_20::SessionSetupResponse setup_res{};
    setup_res.response_code = ResponseCode::OK_NewSessionEstablished;
    setup_res.header.session_id = sid;
    setup_res.evseid = "DE*PNX*E12345";
    inject_then_expect<message_20::AuthorizationSetupRequest>(fx, "SessionSetup -> AuthorizationSetup", setup_res,
                                                              PT::Part20Main);

    message_20::AuthorizationSetupResponse auth_setup_res{};
    auth_setup_res.header.session_id = sid;
    auth_setup_res.response_code = ResponseCode::OK;
    auth_setup_res.authorization_services = {message_20::datatypes::Authorization::EIM};
    auth_setup_res.certificate_installation_service = false;
    auth_setup_res.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
    inject_then_expect<message_20::AuthorizationRequest>(fx, "AuthorizationSetup -> Authorization", auth_setup_res,
                                                         PT::Part20Main);

    message_20::AuthorizationResponse auth_res{};
    auth_res.header.session_id = sid;
    auth_res.response_code = ResponseCode::OK;
    auth_res.evse_processing = Processing::Finished;
    inject_then_expect<message_20::ServiceDiscoveryRequest>(fx, "Authorization -> ServiceDiscovery", auth_res,
                                                            PT::Part20Main);

    message_20::ServiceDiscoveryResponse discovery_res{};
    discovery_res.header.session_id = sid;
    discovery_res.response_code = ResponseCode::OK;
    discovery_res.energy_transfer_service_list = {{SC::AC_DER_IEC, false}};
    {
        const auto req = inject_then_expect<message_20::ServiceDetailRequest>(fx, "ServiceDiscovery -> ServiceDetail",
                                                                              discovery_res, PT::Part20Main);
        REQUIRE(req.service == message_20::to_underlying_value(SC::AC_DER_IEC));
    }

    return sid;
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

SCENARIO("ISO15118-20 EV Session drives a full AC_DER_IEC session through the DER charge loop to SessionStop") {

    GIVEN("An AC_DER_IEC-configured Session bound to a reactor with short send-delay and watchdog timers") {
        SessionFixture fx{"EVTESTID01",
                          ev::SessionTiming{5ms, 100ms},
                          ev::DcChargeParams{},
                          default_advertised_ac_app_protocols(),
                          message_20::datatypes::ServiceCategory::AC_DER_IEC,
                          ac_seed_params()};

        WHEN("the session is walked to an active AC_DER_IEC_ChargeLoop and the SECC then signals Terminate") {
            const auto sid = walk_to_ac_der_iec_charge_loop(fx);

            REQUIRE_FALSE(fx.session.is_finished());
            REQUIRE_FALSE(fx.timed_out);

            // DER_AC_ChargeLoopResponse(OK, Dynamic) -> DER_AC_ChargeLoopRequest; fires der_control.
            REQUIRE_FALSE(fx.der_control);
            inject_then_expect<message_20::DER_AC_ChargeLoopRequest>(
                fx, "AC_DER_IEC_ChargeLoop OK -> AC_DER_IEC_ChargeLoop", make_der_loop_res(sid), PT::Part20DerIec);
            REQUIRE(fx.der_control);
            REQUIRE_FALSE(fx.stop_from_charger);

            // DER_AC_ChargeLoopResponse(OK, Terminate) -> PowerDeliveryRequest(Stop); fires stop_from_charger.
            auto loop_terminate = make_der_loop_res(sid);
            loop_terminate.status =
                message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Terminate};
            {
                const auto req = inject_then_expect<message_20::PowerDeliveryRequest>(
                    fx, "AC_DER_IEC_ChargeLoop Terminate -> PowerDelivery(Stop)", loop_terminate, PT::Part20DerIec);
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

SCENARIO("ISO15118-20 EV Session stops cleanly when the only AC_DER_IEC set demands an unsupported function") {

    GIVEN("An AC_DER_IEC-configured strict Session (supporting only the two DSO setpoints)") {
        SessionFixture fx{"EVTESTID01",
                          ev::SessionTiming{5ms, 100ms},
                          ev::DcChargeParams{},
                          default_advertised_ac_app_protocols(),
                          message_20::datatypes::ServiceCategory::AC_DER_IEC,
                          ac_seed_params(),
                          default_der_control_functions(),
                          true};

        WHEN("the ServiceDetailResponse offers only a set demanding VoltWattMode") {
            const auto sid = walk_to_ac_der_iec_service_detail(fx);
            const auto before = fx.captured.size();

            message_20::ServiceDetailResponse detail_res{};
            detail_res.header.session_id = sid;
            detail_res.response_code = ResponseCode::OK;
            detail_res.service = message_20::to_underlying_value(message_20::datatypes::ServiceCategory::AC_DER_IEC);
            detail_res.service_parameter_list = {
                make_der_param_set(10, ControlMode::Dynamic, der_mask({iso15118::iec::DERControlName::VoltWattMode}))};
            fx.session.on_bytes_received(frame_payload(PT::Part20Main, serialize_msg(detail_res)));

            THEN("the session finishes without emitting a ServiceSelectionRequest and without timing out") {
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.session.is_finished(); }, 1s));
                REQUIRE(fx.session.is_finished());
                REQUIRE_FALSE(fx.timed_out);
                REQUIRE(fx.captured.size() == before);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Session drives a full AC_BPT session through the BPT charge loop to SessionStop") {

    GIVEN("An AC_BPT-configured Session bound to a reactor with short send-delay and watchdog timers") {
        SessionFixture fx{"EVTESTID01",
                       ev::SessionTiming{5ms, 100ms},
                       ev::DcChargeParams{},
                       default_advertised_ac_app_protocols(),
                       message_20::datatypes::ServiceCategory::AC_BPT,
                       ac_bpt_seed_params()};

        WHEN("the session is walked to an active BPT AC_ChargeLoop and the SECC then signals Terminate") {
            const auto sid = walk_to_ac_bpt_charge_loop(fx);

            REQUIRE(fx.captured.size() == 11);
            REQUIRE_FALSE(fx.session.is_finished());
            REQUIRE_FALSE(fx.timed_out);

            // BPT AC_ChargeLoopResponse(OK, Dynamic target) -> AC_ChargeLoopRequest; fires ac_target_power.
            REQUIRE_FALSE(fx.ac_target_power);
            inject_then_expect<message_20::AC_ChargeLoopRequest>(fx, "AC_ChargeLoop OK -> AC_ChargeLoop",
                                                                 make_ac_bpt_loop_res(sid), PT::Part20AC);
            REQUIRE(fx.ac_target_power);
            REQUIRE_FALSE(fx.stop_from_charger);

            // BPT AC_ChargeLoopResponse(OK, Terminate) -> PowerDeliveryRequest(Stop); fires stop_from_charger.
            auto loop_terminate = make_ac_bpt_loop_res(sid);
            loop_terminate.status =
                message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Terminate};
            {
                const auto req = inject_then_expect<message_20::PowerDeliveryRequest>(
                    fx, "AC_ChargeLoop Terminate -> PowerDelivery(Stop)", loop_terminate, PT::Part20AC);
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

SCENARIO("ISO15118-20 EV Session drives a full DC_BPT session through the BPT charge loop to SessionStop") {

    GIVEN("A DC_BPT-configured Session bound to a reactor with short send-delay and watchdog timers") {
        SessionFixture fx{"EVTESTID01", ev::SessionTiming{5ms, 100ms}, dc_bpt_seed_params(),
                       default_advertised_app_protocols(), message_20::datatypes::ServiceCategory::DC_BPT};

        WHEN("the session is walked to an active BPT DC_ChargeLoop and the SECC then signals Terminate") {
            const auto sid = walk_to_dc_bpt_charge_loop(fx);

            REQUIRE(fx.captured.size() == 13);
            REQUIRE_FALSE(fx.session.is_finished());
            REQUIRE_FALSE(fx.timed_out);

            // BPT DC_ChargeLoopResponse(OK, no Terminate) -> DC_ChargeLoopRequest (loop continues).
            REQUIRE_FALSE(fx.stop_from_charger);
            inject_then_expect<message_20::DC_ChargeLoopRequest>(fx, "DC_ChargeLoop OK -> DC_ChargeLoop",
                                                                 make_dc_bpt_loop_res(sid), PT::Part20DC);
            REQUIRE_FALSE(fx.stop_from_charger);

            // BPT DC_ChargeLoopResponse(OK, Terminate) -> PowerDeliveryRequest(Stop); fires stop_from_charger.
            auto loop_terminate = make_dc_bpt_loop_res(sid);
            loop_terminate.status =
                message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Terminate};
            {
                const auto req = inject_then_expect<message_20::PowerDeliveryRequest>(
                    fx, "DC_ChargeLoop Terminate -> PowerDelivery(Stop)", loop_terminate, PT::Part20DC);
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
