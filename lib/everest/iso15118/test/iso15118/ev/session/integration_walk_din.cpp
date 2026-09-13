// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Session-level FSM-walk integration test for the DIN SPEC 70121 EV engine.
//
// Like integration_walk.cpp, the only input is bytes and the only output is captured frames: the
// SupportedAppProtocol handshake hands over to the DIN engine, and every following step injects a
// canned EXI-encoded DIN response and asserts the request the Session emits next. Every DIN frame
// travels under V2GTP payload type SAP (0x8001).
//
// DC walk (each arrow = one injected response -> one emitted request):
//
//   start()                              -> SupportedAppProtocolRequest (-20 DC, -2, DIN)
//   SupportedAppProtocolResponse(3)      -> SessionSetupRequest           (DIN engine)
//   SessionSetupResponse                 -> ServiceDiscoveryRequest
//   ServiceDiscoveryResponse             -> ServicePaymentSelectionRequest
//   ServicePaymentSelectionResponse      -> ContractAuthenticationRequest
//   ContractAuthenticationRes(Ongoing)   -> ContractAuthenticationRequest
//   ContractAuthenticationRes(Finished)  -> ChargeParameterDiscoveryRequest
//   ChargeParameterDiscoveryRes(Fin.)    -> CableCheckRequest
//   CableCheckResponse(Finished)         -> PreChargeRequest
//   PreChargeResponse(in tolerance)      -> PowerDeliveryRequest(ready)
//   PowerDeliveryResponse                -> CurrentDemandRequest
//   CurrentDemandResponse                -> CurrentDemandRequest (loop continues)
//   CurrentDemandResponse                -> PowerDeliveryRequest(not ready)  (StopCharging delivered)
//   PowerDeliveryResponse                -> WeldingDetectionRequest
//   WeldingDetectionResponse(0 V)        -> SessionStopRequest
//   SessionStopResponse                  -> session finishes, DLINK_TERMINATE
//
// DIN has no pause on the wire: a pause is a SessionStop whose session id is re-joined later. With
// CP-state feedback configured, CableCheck holds its first request until CP state C/D [V2G-DC-547].

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <iso15118/message_din/cable_check.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/message_din/common_types.hpp>
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

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/sap_offer.hpp>
#include <iso15118/ev/session_params.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using namespace iso15118::ev::test;

namespace {

namespace dt = message_din::datatypes;
using PT = io::v2gtp::PayloadType;

constexpr dt::SessionId DIN_SID{0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18};
constexpr std::array<uint8_t, 6> EVCC_MAC{0x02, 0xDE, 0xAD, 0xBE, 0xEF, 0x02};

constexpr float TARGET_VOLTAGE = 400.0f;
constexpr float TARGET_CURRENT = 100.0f;
constexpr float MAX_CHARGE_CURRENT = 150.0f;
constexpr float MAX_VOLTAGE = 900.0f;
constexpr double PRESENT_SOC = 42.0;

constexpr double EVSE_MAX_VOLTAGE = 920.0;
constexpr double EVSE_MAX_CURRENT = 200.0;
constexpr double EVSE_MAX_POWER = 150000.0;

// The DIN engine paces requests at 100 ms (EvseV2G MAX_RES_TIME parity).
constexpr auto MIN_REQUEST_INTERVAL = 100ms;

template <typename Msg> std::vector<uint8_t> serialize_din(const Msg& msg) {
    uint8_t buffer[2048];
    io::StreamOutputView out({buffer, sizeof(buffer)});
    const auto size = message_din::serialize(msg, out);
    return std::vector<uint8_t>(buffer, buffer + size);
}

message_din::Variant decode_din(const std::vector<uint8_t>& frame) {
    uint32_t len_be;
    std::memcpy(&len_be, frame.data() + 4, sizeof(len_be));
    return message_din::Variant{io::StreamInputView{frame.data() + io::SdpPacket::V2GTP_HEADER_SIZE, ntohl(len_be)}};
}

// One walk step: inject a DIN response frame, run until the Session emits the next request, and assert
// it decodes as ExpectedReq. `label` localizes failures.
template <typename ExpectedReq, typename ResponseMsg>
ExpectedReq step(SessionFixture& fx, const char* label, const ResponseMsg& response) {
    INFO("walk step: " << label);
    const auto before = fx.captured.size();
    fx.session.on_bytes_received(frame_payload(PT::SAP, serialize_din(response)));
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() > before; }, 1s));
    auto variant = decode_din(fx.captured.back());
    const auto* request = variant.get_if<ExpectedReq>();
    REQUIRE(request != nullptr);
    return *request;
}

template <typename Res> Res ok_din(dt::ResponseCode code = dt::ResponseCode::OK) {
    Res res{};
    res.header.session_id = DIN_SID;
    res.response_code = code;
    return res;
}

message_20::SupportedAppProtocolResponse sap_response(uint8_t schema_id) {
    return {message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, schema_id};
}

ev::DcChargeParams walk_dc_params() {
    ev::DcChargeParams params{};
    params.max_charge_power = 60000.0f;
    params.max_charge_current = MAX_CHARGE_CURRENT;
    params.max_voltage = MAX_VOLTAGE;
    params.min_voltage = 200.0f;
    params.energy_capacity = 80000.0f;
    params.target_voltage = TARGET_VOLTAGE;
    params.target_current = TARGET_CURRENT;
    params.present_soc = PRESENT_SOC;
    params.present_voltage = TARGET_VOLTAGE;
    return params;
}

ev::EvSessionParams walk_params() {
    ev::EvSessionParams params;
    params.evcc_mac = EVCC_MAC;
    params.energy_transfer_mode = dt::EnergyTransferMode::DC_extended;
    return params;
}

std::unique_ptr<SessionFixture> make_fixture(bool has_cp_state_feedback = false,
                                             std::optional<ProtocolId> resume = std::nullopt,
                                             std::optional<dt::SessionId> resumed_session_id = std::nullopt) {
    ev::SapOfferInput input;
    input.supported_protocols = {ProtocolId::ISO15118_20, ProtocolId::ISO15118_2, ProtocolId::DIN70121};
    input.energy_service = message_20::datatypes::ServiceCategory::DC;
    input.resume_protocol = resume;
    const auto offer = build_sap_offer(input);

    std::vector<message_20::SupportedAppProtocol> advertised;
    for (const auto& entry : offer) {
        advertised.push_back(entry.entry);
    }

    ev::d20::SessionOptions options;
    options.offered_protocols = offer;
    options.has_cp_state_feedback = has_cp_state_feedback;
    options.resumed_session_id = resumed_session_id;

    return std::make_unique<SessionFixture>("EVTESTID01", ev::SessionTiming{5ms, 1000ms}, walk_dc_params(), advertised,
                                            message_20::datatypes::ServiceCategory::DC, ev::AcChargeParams{},
                                            default_der_control_functions(), true, std::move(options), walk_params());
}

// start() -> SupportedAppProtocolRequest -> (SECC picks @p schema_id) -> DIN SessionSetupRequest.
message_din::SessionSetupRequest walk_sap(SessionFixture& fx, uint8_t schema_id, std::size_t offer_size) {
    fx.session.start();
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
    auto sap_variant = decode_frame(fx.captured.back());
    const auto* sap_req = sap_variant.get_if<message_20::SupportedAppProtocolRequest>();
    REQUIRE(sap_req != nullptr);
    REQUIRE(sap_req->app_protocol.size() == offer_size);

    const auto before = fx.captured.size();
    fx.session.on_bytes_received(frame_payload(PT::SAP, serialize_msg(sap_response(schema_id))));
    REQUIRE(fx.selected_protocol == ProtocolId::DIN70121);
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() > before; }, 1s));
    auto variant = decode_din(fx.captured.back());
    const auto* request = variant.get_if<message_din::SessionSetupRequest>();
    REQUIRE(request != nullptr);
    return *request;
}

message_din::SessionSetupResponse
session_setup_res(dt::ResponseCode code = dt::ResponseCode::OK_NewSessionEstablished) {
    auto res = ok_din<message_din::SessionSetupResponse>(code);
    res.evse_id = {0x2A, 0x3B};
    return res;
}

message_din::ServiceDiscoveryResponse service_discovery_res() {
    auto res = ok_din<message_din::ServiceDiscoveryResponse>();
    res.payment_options = {dt::PaymentOption::ExternalPayment};
    res.charge_service.service_tag.service_id = 1;
    res.charge_service.service_tag.service_category = dt::ServiceCategory::EVCharging;
    res.charge_service.free_service = false;
    res.charge_service.energy_transfer_type = dt::SupportedEnergyTransferMode::DC_extended;
    return res;
}

message_din::ContractAuthenticationResponse contract_authentication_res(dt::EvseProcessing processing) {
    auto res = ok_din<message_din::ContractAuthenticationResponse>();
    res.evse_processing = processing;
    return res;
}

message_din::ChargeParameterDiscoveryResponse cpd_res() {
    auto res = ok_din<message_din::ChargeParameterDiscoveryResponse>();
    res.evse_processing = dt::EvseProcessing::Finished;

    dt::DcEvseChargeParameter evse_param{};
    evse_param.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    evse_param.dc_evse_status.evse_notification = dt::EvseNotification::None;
    evse_param.evse_maximum_current_limit = EVSE_MAX_CURRENT;
    evse_param.evse_maximum_voltage_limit = EVSE_MAX_VOLTAGE;
    evse_param.evse_maximum_power_limit = EVSE_MAX_POWER;
    evse_param.evse_minimum_current_limit = 0.0;
    evse_param.evse_minimum_voltage_limit = 150.0;
    evse_param.evse_peak_current_ripple = 1.0;
    res.dc_evse_charge_parameter = evse_param;
    return res;
}

message_din::CableCheckResponse cable_check_res(dt::EvseProcessing processing) {
    auto res = ok_din<message_din::CableCheckResponse>();
    res.evse_processing = processing;
    res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
    return res;
}

message_din::PreChargeResponse pre_charge_res() {
    auto res = ok_din<message_din::PreChargeResponse>();
    res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.evse_present_voltage = TARGET_VOLTAGE;
    return res;
}

message_din::PowerDeliveryResponse power_delivery_res() {
    auto res = ok_din<message_din::PowerDeliveryResponse>();
    dt::DcEvseStatus status{};
    status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.dc_evse_status = status;
    return res;
}

message_din::CurrentDemandResponse current_demand_res(dt::EvseNotification notification = dt::EvseNotification::None) {
    auto res = ok_din<message_din::CurrentDemandResponse>();
    res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.dc_evse_status.evse_notification = notification;
    res.evse_present_voltage = TARGET_VOLTAGE;
    res.evse_present_current = TARGET_CURRENT;
    res.evse_maximum_voltage_limit = EVSE_MAX_VOLTAGE;
    res.evse_maximum_current_limit = EVSE_MAX_CURRENT;
    res.evse_maximum_power_limit = EVSE_MAX_POWER;
    return res;
}

message_din::WeldingDetectionResponse welding_detection_res() {
    auto res = ok_din<message_din::WeldingDetectionResponse>();
    res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.evse_present_voltage = 0.0;
    return res;
}

// SessionSetup -> ServiceDiscovery -> ServicePaymentSelection -> ContractAuthentication -> the first
// ChargeParameterDiscoveryRequest.
void walk_to_charge_parameter_discovery(SessionFixture& fx) {
    {
        const auto req =
            step<message_din::ServiceDiscoveryRequest>(fx, "SessionSetup -> ServiceDiscovery", session_setup_res());
        REQUIRE(req.header.session_id == DIN_SID);
    }
    REQUIRE(fx.evse_id == "2A3B");

    {
        const auto req = step<message_din::ServicePaymentSelectionRequest>(
            fx, "ServiceDiscovery -> ServicePaymentSelection", service_discovery_res());
        REQUIRE(req.selected_payment_option == dt::PaymentOption::ExternalPayment);
        REQUIRE(req.selected_service_list.size() == 1);
        REQUIRE(req.selected_service_list[0].service_id == 1);
    }

    step<message_din::ContractAuthenticationRequest>(fx, "ServicePaymentSelection -> ContractAuthentication",
                                                     ok_din<message_din::ServicePaymentSelectionResponse>());

    step<message_din::ContractAuthenticationRequest>(fx, "ContractAuthentication(Ongoing) -> ContractAuthentication",
                                                     contract_authentication_res(dt::EvseProcessing::Ongoing));

    const auto req = step<message_din::ChargeParameterDiscoveryRequest>(
        fx, "ContractAuthentication(Finished) -> ChargeParameterDiscovery",
        contract_authentication_res(dt::EvseProcessing::Finished));
    REQUIRE(req.ev_requested_energy_transfer_type == dt::EnergyTransferMode::DC_extended);
    REQUIRE(req.dc_ev_charge_parameter.has_value());
    REQUIRE(req.dc_ev_charge_parameter->dc_ev_status.ev_ress_soc == static_cast<int8_t>(PRESENT_SOC));
    REQUIRE(req.dc_ev_charge_parameter->ev_maximum_current_limit == Catch::Approx(MAX_CHARGE_CURRENT));
    REQUIRE(req.dc_ev_charge_parameter->ev_maximum_voltage_limit == Catch::Approx(MAX_VOLTAGE));
}

// SAP through the first CurrentDemandRequest.
void walk_to_current_demand(SessionFixture& fx) {
    const auto setup_req = walk_sap(fx, 3, 3);
    REQUIRE(setup_req.evcc_id == std::vector<uint8_t>(EVCC_MAC.begin(), EVCC_MAC.end()));
    REQUIRE(setup_req.header.session_id == dt::SessionId{});

    walk_to_charge_parameter_discovery(fx);

    REQUIRE_FALSE(fx.ev_power_ready);
    {
        const auto req = step<message_din::CableCheckRequest>(fx, "ChargeParameterDiscovery -> CableCheck", cpd_res());
        REQUIRE(req.dc_ev_status.ev_ready);
    }
    REQUIRE(fx.ev_power_ready);
    REQUIRE(fx.dc_present_limits.has_value());
    REQUIRE(fx.dc_present_limits->voltage == Catch::Approx(EVSE_MAX_VOLTAGE));
    REQUIRE(fx.dc_present_limits->power == Catch::Approx(EVSE_MAX_POWER));

    {
        const auto req = step<message_din::PreChargeRequest>(fx, "CableCheck -> PreCharge",
                                                             cable_check_res(dt::EvseProcessing::Finished));
        REQUIRE(req.ev_target_voltage == Catch::Approx(TARGET_VOLTAGE));
        REQUIRE(req.ev_target_current == Catch::Approx(0.0));
    }

    REQUIRE_FALSE(fx.dc_power_on);
    {
        const auto req =
            step<message_din::PowerDeliveryRequest>(fx, "PreCharge -> PowerDelivery(ready)", pre_charge_res());
        REQUIRE(req.ready_to_charge_state);
        REQUIRE(req.dc_ev_power_delivery_parameter.has_value());
        REQUIRE_FALSE(req.dc_ev_power_delivery_parameter->charging_complete);
    }
    REQUIRE(fx.dc_power_on);

    const auto req =
        step<message_din::CurrentDemandRequest>(fx, "PowerDelivery -> CurrentDemand", power_delivery_res());
    REQUIRE(req.header.session_id == DIN_SID);
    REQUIRE(req.ev_target_voltage == Catch::Approx(TARGET_VOLTAGE));
    REQUIRE(req.ev_target_current == Catch::Approx(TARGET_CURRENT));
    REQUIRE(req.dc_ev_status.ev_ress_soc == static_cast<int8_t>(PRESENT_SOC));
}

// PowerDelivery(not ready) -> WeldingDetection -> SessionStop -> clean finish.
void walk_stop_to_finish(SessionFixture& fx) {
    step<message_din::WeldingDetectionRequest>(fx, "PowerDelivery(Stop) -> WeldingDetection", power_delivery_res());

    {
        const auto req =
            step<message_din::SessionStopRequest>(fx, "WeldingDetection -> SessionStop", welding_detection_res());
        REQUIRE(req.header.session_id == DIN_SID);
    }

    fx.session.on_bytes_received(frame_payload(PT::SAP, serialize_din(ok_din<message_din::SessionStopResponse>())));
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.session.is_finished(); }, 1s));
    REQUIRE_FALSE(fx.timed_out);
}

} // namespace

SCENARIO("DIN SPEC 70121 EV Session walks a DC session to a graceful stop") {
    GIVEN("a Session offering ISO 15118-20 DC, ISO 15118-2 and DIN SPEC 70121") {
        auto fx = make_fixture();

        WHEN("the SECC selects the DIN schema id and answers every request") {
            walk_to_current_demand(*fx);

            THEN("the charge loop paces requests and a StopCharging ends the session cleanly") {
                const auto first_request = std::chrono::steady_clock::now();
                step<message_din::CurrentDemandRequest>(*fx, "CurrentDemand -> CurrentDemand", current_demand_res());
                const auto second_request = std::chrono::steady_clock::now();
                REQUIRE(second_request - first_request >= MIN_REQUEST_INTERVAL);

                fx->session.deliver_control_event(ev::d20::ControlEvent{ev::d20::StopCharging{true}});

                const auto req = step<message_din::PowerDeliveryRequest>(*fx, "CurrentDemand -> PowerDelivery(Stop)",
                                                                         current_demand_res());
                REQUIRE_FALSE(req.ready_to_charge_state);

                walk_stop_to_finish(*fx);

                REQUIRE(fx->session.is_finished());
                REQUIRE_FALSE(fx->session.is_paused());
                REQUIRE(fx->session.session_id() == DIN_SID);
                REQUIRE(fx->session.selected_protocol() == ProtocolId::DIN70121);
                REQUIRE(fx->signals.back() == ev::feedback::Signal::DLINK_TERMINATE);
            }
        }
    }
}

SCENARIO("DIN SPEC 70121 EV Session follows a charger-initiated stop") {
    GIVEN("a DIN Session in the CurrentDemand loop") {
        auto fx = make_fixture();
        walk_to_current_demand(*fx);

        WHEN("the SECC answers the charge loop with StopCharging") {
            REQUIRE_FALSE(fx->stop_from_charger);
            const auto req =
                step<message_din::PowerDeliveryRequest>(*fx, "CurrentDemand(StopCharging) -> PowerDelivery(Stop)",
                                                        current_demand_res(dt::EvseNotification::StopCharging));

            THEN("stop_from_charger fires and the session terminates") {
                REQUIRE(fx->stop_from_charger);
                REQUIRE_FALSE(req.ready_to_charge_state);
                walk_stop_to_finish(*fx);
                REQUIRE_FALSE(fx->session.is_paused());
            }
        }
    }
}

SCENARIO("DIN SPEC 70121 EV Session pauses and re-joins the paused session id") {
    GIVEN("a DIN Session in the CurrentDemand loop") {
        auto fx = make_fixture();
        walk_to_current_demand(*fx);

        WHEN("a PauseCharging is delivered in the loop") {
            fx->session.deliver_control_event(ev::d20::ControlEvent{ev::d20::PauseCharging{true}});
            const auto req = step<message_din::PowerDeliveryRequest>(*fx, "CurrentDemand -> PowerDelivery(Stop)",
                                                                     current_demand_res());
            REQUIRE_FALSE(req.ready_to_charge_state);

            THEN("SessionStop parks the session as paused") {
                walk_stop_to_finish(*fx);
                REQUIRE(fx->session.is_paused());
                REQUIRE(fx->session.session_id() == DIN_SID);
                REQUIRE(fx->signals.back() == ev::feedback::Signal::DLINK_PAUSE);
            }
        }
    }

    GIVEN("a second Session constructed with the paused session id and an offer constrained to DIN") {
        auto fx = make_fixture(false, ProtocolId::DIN70121, DIN_SID);

        WHEN("the SECC accepts the resumed session") {
            const auto setup_req = walk_sap(*fx, 1, 1);

            THEN("SessionSetupReq carries the id and OK_OldSessionJoined is accepted") {
                REQUIRE(setup_req.header.session_id == DIN_SID);
                const auto req = step<message_din::ServiceDiscoveryRequest>(
                    *fx, "SessionSetup(OldSessionJoined) -> ServiceDiscovery",
                    session_setup_res(dt::ResponseCode::OK_OldSessionJoined));
                REQUIRE(req.header.session_id == DIN_SID);
                REQUIRE_FALSE(fx->session.is_finished());
            }
        }
    }
}

SCENARIO("DIN SPEC 70121 EV Session holds CableCheck until CP state C or D") {
    GIVEN("a DIN Session configured with CP state feedback") {
        auto fx = make_fixture(true);
        walk_sap(*fx, 3, 3);
        walk_to_charge_parameter_discovery(*fx);

        WHEN("ChargeParameterDiscovery finishes without a CP state event") {
            const auto captured_before = fx->captured.size();
            fx->session.on_bytes_received(frame_payload(PT::SAP, serialize_din(cpd_res())));

            THEN("no CableCheckReq goes out until CpState C/D is delivered") {
                REQUIRE_FALSE(run_reactor_until(
                    fx->reactor, [&]() { return fx->captured.size() > captured_before; }, 300ms));
                REQUIRE(fx->ev_power_ready);
                REQUIRE_FALSE(fx->session.is_finished());

                fx->session.deliver_control_event(ev::d20::ControlEvent{ev::d20::CpState{true}});
                REQUIRE(run_reactor_until(
                    fx->reactor, [&]() { return fx->captured.size() > captured_before; }, 1s));
                auto variant = decode_din(fx->captured.back());
                REQUIRE(variant.get_if<message_din::CableCheckRequest>() != nullptr);
            }
        }
    }
}
