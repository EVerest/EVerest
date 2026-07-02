// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Pump-level FSM-walk integration test.
//
// This drives an ev::Session purely by injecting canned, EXI-encoded V2GTP
// response frames via session.on_bytes_received(...) and asserting the request
// frame the pump emits at each step. No fsm.feed() and no manual create_state<>()
// are used: the only input is bytes, the only output is captured frames. This is
// the proof that fsm::v2::FSM<d20::StateBase> drives evio's real EV states
// unchanged through the implemented entry sequence.
//
// Clean DC walk to a graceful SessionStop (each arrow = one injected response →
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
// i.e. SupportedAppProtocol -> SessionSetup -> AuthorizationSetup -> Authorization
// -> ServiceDiscovery -> ServiceDetail -> ServiceSelection -> DC_ChargeParameterDiscovery
// -> ScheduleExchange -> DC_CableCheck -> DC_PreCharge -> PowerDelivery -> DC_ChargeLoop
// -> PowerDelivery -> DC_WeldingDetection -> SessionStop.
//
// The DC_ChargeLoop iterates: a plain OK response re-emits a loop request; only the
// SECC's Terminate notification breaks the loop, firing the stop_from_charger feedback
// and driving PowerDelivery(Stop) -> DC_WeldingDetection -> SessionStop. The
// SessionStopResponse ends the session cleanly (is_finished, not timed_out).

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

#include <arpa/inet.h>

#include <cbv2g/exi_v2gtp.h>

#include <everest/io/event/fd_event_handler.hpp>

#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/io/stream_view.hpp>
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
#include <iso15118/message/variant.hpp>

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/session.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/session/logger.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using namespace iso15118::ev::test;

namespace {

// Walk an ev::Session from start() through the first DC_ChargeLoopRequest by
// injecting canned response frames, exactly as the full-walk scenario does. Used
// by the EV-initiated-stop scenario so it can begin from an active charge loop.
// Returns the established session id.
message_20::datatypes::SessionId walk_to_dc_charge_loop(ev::Session& session,
                                                        everest::lib::io::event::fd_event_handler& reactor,
                                                        std::vector<std::vector<uint8_t>>& captured) {
    const message_20::datatypes::SessionId session_id{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    session.start();
    REQUIRE(pump_until(
        reactor, [&]() { return captured.size() >= 1; }, 1s));

    message_20::SupportedAppProtocolResponse sap_res{
        message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1};
    session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::SAP, serialize_msg(sap_res)));
    REQUIRE(pump_until(
        reactor, [&]() { return captured.size() >= 2; }, 1s));

    message_20::SessionSetupResponse setup_res{};
    setup_res.response_code = message_20::datatypes::ResponseCode::OK_NewSessionEstablished;
    setup_res.header.session_id = session_id;
    setup_res.evseid = "DE*PNX*E12345";
    session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(setup_res)));
    REQUIRE(pump_until(
        reactor, [&]() { return captured.size() >= 3; }, 1s));

    message_20::AuthorizationSetupResponse auth_setup_res{};
    auth_setup_res.header.session_id = session_id;
    auth_setup_res.response_code = message_20::datatypes::ResponseCode::OK;
    auth_setup_res.authorization_services = {message_20::datatypes::Authorization::EIM};
    auth_setup_res.certificate_installation_service = false;
    auth_setup_res.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
    session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(auth_setup_res)));
    REQUIRE(pump_until(
        reactor, [&]() { return captured.size() >= 4; }, 1s));

    message_20::AuthorizationResponse auth_res{};
    auth_res.header.session_id = session_id;
    auth_res.response_code = message_20::datatypes::ResponseCode::OK;
    auth_res.evse_processing = message_20::datatypes::Processing::Finished;
    session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(auth_res)));
    REQUIRE(pump_until(
        reactor, [&]() { return captured.size() >= 5; }, 1s));

    message_20::ServiceDiscoveryResponse discovery_res{};
    discovery_res.header.session_id = session_id;
    discovery_res.response_code = message_20::datatypes::ResponseCode::OK;
    discovery_res.energy_transfer_service_list = {{message_20::datatypes::ServiceCategory::DC, false}};
    session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(discovery_res)));
    REQUIRE(pump_until(
        reactor, [&]() { return captured.size() >= 6; }, 1s));

    message_20::ServiceDetailResponse detail_res{};
    detail_res.header.session_id = session_id;
    detail_res.response_code = message_20::datatypes::ResponseCode::OK;
    detail_res.service = message_20::to_underlying_value(message_20::datatypes::ServiceCategory::DC);
    detail_res.service_parameter_list = {message_20::datatypes::ParameterSet(1)};
    session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(detail_res)));
    REQUIRE(pump_until(
        reactor, [&]() { return captured.size() >= 7; }, 1s));

    message_20::ServiceSelectionResponse selection_res{};
    selection_res.header.session_id = session_id;
    selection_res.response_code = message_20::datatypes::ResponseCode::OK;
    session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(selection_res)));
    REQUIRE(pump_until(
        reactor, [&]() { return captured.size() >= 8; }, 1s));

    message_20::DC_ChargeParameterDiscoveryResponse cpd_res{};
    cpd_res.header.session_id = session_id;
    cpd_res.response_code = message_20::datatypes::ResponseCode::OK;
    cpd_res.transfer_mode = message_20::datatypes::DC_CPDResEnergyTransferMode{};
    session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20DC, serialize_msg(cpd_res)));
    REQUIRE(pump_until(
        reactor, [&]() { return captured.size() >= 9; }, 1s));

    message_20::ScheduleExchangeResponse schedule_res{};
    schedule_res.header.session_id = session_id;
    schedule_res.response_code = message_20::datatypes::ResponseCode::OK;
    schedule_res.processing = message_20::datatypes::Processing::Finished;
    session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(schedule_res)));
    REQUIRE(pump_until(
        reactor, [&]() { return captured.size() >= 10; }, 1s));

    message_20::DC_CableCheckResponse cable_check_res{};
    cable_check_res.header.session_id = session_id;
    cable_check_res.response_code = message_20::datatypes::ResponseCode::OK;
    cable_check_res.processing = message_20::datatypes::Processing::Finished;
    session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20DC, serialize_msg(cable_check_res)));
    REQUIRE(pump_until(
        reactor, [&]() { return captured.size() >= 11; }, 1s));

    message_20::DC_PreChargeResponse pre_charge_res{};
    pre_charge_res.header.session_id = session_id;
    pre_charge_res.response_code = message_20::datatypes::ResponseCode::OK;
    pre_charge_res.present_voltage = message_20::datatypes::from_float(0.0f);
    session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20DC, serialize_msg(pre_charge_res)));
    REQUIRE(pump_until(
        reactor, [&]() { return captured.size() >= 12; }, 1s));

    message_20::PowerDeliveryResponse power_delivery_res{};
    power_delivery_res.header.session_id = session_id;
    power_delivery_res.response_code = message_20::datatypes::ResponseCode::OK;
    session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(power_delivery_res)));
    REQUIRE(pump_until(
        reactor, [&]() { return captured.size() >= 13; }, 1s));
    {
        auto variant = decode_frame(captured.back());
        REQUIRE(variant.get_if<message_20::DC_ChargeLoopRequest>() != nullptr);
    }

    return session_id;
}

} // namespace

SCENARIO("EV pump drives evio states byte-by-byte through a full DC session to SessionStop") {

    GIVEN("A Session bound to a reactor with short send-delay and watchdog timers") {

        // evio's real states log on entry; provide a no-op sink so SessionLogger
        // does not throw bad_function_call from a state's enter().
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});

        everest::lib::io::event::fd_event_handler reactor;

        std::vector<std::vector<uint8_t>> captured;
        bool timed_out = false;
        bool ev_power_ready = false;
        bool dc_power_on = false;
        bool stop_from_charger = false;

        ev::feedback::Callbacks callbacks{};
        callbacks.timed_out = [&timed_out]() { timed_out = true; };
        callbacks.ev_power_ready = [&ev_power_ready]() { ev_power_ready = true; };
        callbacks.dc_power_on = [&dc_power_on]() { dc_power_on = true; };
        callbacks.stop_from_charger = [&stop_from_charger]() { stop_from_charger = true; };

        session::SessionLogger logger{nullptr};

        const ev::SessionTiming timing{5ms, 100ms};

        ev::Session session{callbacks,
                            [&captured](std::vector<uint8_t> frame) {
                                captured.push_back(std::move(frame));
                                return true;
                            },
                            logger,
                            reactor,
                            timing,
                            "EVTESTID01",
                            ev::test::default_advertised_app_protocols()};

        WHEN("the session starts and is fed SAP, SessionSetup, then AuthorizationSetup responses") {

            // --- start() -> SupportedAppProtocolRequest ---
            session.start();
            REQUIRE(pump_until(
                reactor, [&]() { return captured.size() >= 1; }, 1s));
            {
                const auto& frame = captured.back();
                REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::SAP);
                auto variant = decode_frame(frame);
                REQUIRE(variant.get_if<message_20::SupportedAppProtocolRequest>() != nullptr);
            }

            // --- inject SupportedAppProtocolResponse -> SessionSetupRequest ---
            {
                message_20::SupportedAppProtocolResponse sap_res{
                    message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1};
                session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::SAP, serialize_msg(sap_res)));
            }
            REQUIRE(pump_until(
                reactor, [&]() { return captured.size() >= 2; }, 1s));
            {
                const auto& frame = captured.back();
                REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);
                auto variant = decode_frame(frame);
                REQUIRE(variant.get_if<message_20::SessionSetupRequest>() != nullptr);
            }

            // --- inject SessionSetupResponse(OK_NewSessionEstablished) -> AuthorizationSetupRequest ---
            // evio's session_setup aborts on an all-zero returned session_id or an empty
            // evseid, so a valid establishment must carry both.
            const message_20::datatypes::SessionId session_id{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
            {
                message_20::SessionSetupResponse setup_res{};
                setup_res.response_code = message_20::datatypes::ResponseCode::OK_NewSessionEstablished;
                setup_res.header.session_id = session_id;
                setup_res.evseid = "DE*PNX*E12345";
                session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(setup_res)));
            }
            REQUIRE(pump_until(
                reactor, [&]() { return captured.size() >= 3; }, 1s));
            {
                const auto& frame = captured.back();
                REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);
                auto variant = decode_frame(frame);
                REQUIRE(variant.get_if<message_20::AuthorizationSetupRequest>() != nullptr);
            }

            // --- inject AuthorizationSetupResponse(OK, {EIM}) -> AuthorizationRequest ---
            // AuthorizationSetup selects EIM, emits an EIM AuthorizationRequest, and
            // transitions into the Authorization state.
            {
                message_20::AuthorizationSetupResponse auth_setup_res{};
                auth_setup_res.header.session_id = session_id;
                auth_setup_res.response_code = message_20::datatypes::ResponseCode::OK;
                auth_setup_res.authorization_services = {message_20::datatypes::Authorization::EIM};
                auth_setup_res.certificate_installation_service = false;
                auth_setup_res.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
                session.on_bytes_received(
                    frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(auth_setup_res)));
            }

            THEN("the pump drives Authorization, ServiceDiscovery, ServiceDetail, ServiceSelection, "
                 "DC_ChargeParameterDiscovery, ScheduleExchange, DC_CableCheck, DC_PreCharge, PowerDelivery, "
                 "DC_ChargeLoop, DC_WeldingDetection and reaches a clean SessionStop") {
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 4; }, 1s));

                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);

                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::AuthorizationRequest>();
                    REQUIRE(request != nullptr);

                    // The request carries the established session id and selects EIM.
                    REQUIRE(request->header.session_id == session_id);
                    REQUIRE(request->selected_authorization_service == message_20::datatypes::Authorization::EIM);
                    REQUIRE(std::holds_alternative<message_20::datatypes::EIM_ASReqAuthorizationMode>(
                        request->authorization_mode));
                }

                // --- inject AuthorizationResponse(OK, Finished) -> ServiceDiscoveryRequest ---
                // EIM returns OK + Finished on the first response, so Authorization::feed
                // transitions to ServiceDiscovery, whose enter() emits the discovery request.
                {
                    message_20::AuthorizationResponse auth_res{};
                    auth_res.header.session_id = session_id;
                    auth_res.response_code = message_20::datatypes::ResponseCode::OK;
                    auth_res.evse_processing = message_20::datatypes::Processing::Finished;
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(auth_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 5; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);

                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::ServiceDiscoveryRequest>();
                    REQUIRE(request != nullptr);
                    REQUIRE(request->header.session_id == session_id);
                }

                // --- inject ServiceDiscoveryResponse(OK, offering DC) -> ServiceDetailRequest ---
                // ServiceDiscovery::feed confirms the DC energy service is offered and
                // transitions to ServiceDetail, whose enter() emits a DC ServiceDetailRequest.
                {
                    message_20::ServiceDiscoveryResponse discovery_res{};
                    discovery_res.header.session_id = session_id;
                    discovery_res.response_code = message_20::datatypes::ResponseCode::OK;
                    discovery_res.energy_transfer_service_list = {{message_20::datatypes::ServiceCategory::DC, false}};
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(discovery_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 6; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);

                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::ServiceDetailRequest>();
                    REQUIRE(request != nullptr);
                    REQUIRE(request->header.session_id == session_id);
                    REQUIRE(request->service ==
                            message_20::to_underlying_value(message_20::datatypes::ServiceCategory::DC));
                }

                // --- inject ServiceDetailResponse(OK, one param set) -> ServiceSelectionRequest ---
                // ServiceDetail::feed picks the first parameter set and transitions to
                // ServiceSelection, whose enter() emits a DC ServiceSelectionRequest.
                {
                    message_20::ServiceDetailResponse detail_res{};
                    detail_res.header.session_id = session_id;
                    detail_res.response_code = message_20::datatypes::ResponseCode::OK;
                    detail_res.service = message_20::to_underlying_value(message_20::datatypes::ServiceCategory::DC);
                    detail_res.service_parameter_list = {message_20::datatypes::ParameterSet(1)};
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(detail_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 7; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);

                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::ServiceSelectionRequest>();
                    REQUIRE(request != nullptr);
                    REQUIRE(request->header.session_id == session_id);
                    REQUIRE(request->selected_energy_transfer_service.service_id ==
                            message_20::datatypes::ServiceCategory::DC);
                    REQUIRE(request->selected_energy_transfer_service.parameter_set_id == 1);
                }

                // The session walked seven byte-driven transitions without finishing or timing out.
                REQUIRE(captured.size() == 7);
                REQUIRE_FALSE(session.is_finished());
                REQUIRE_FALSE(timed_out);

                // --- inject ServiceSelectionResponse(OK) -> DC_ChargeParameterDiscoveryRequest ---
                // ServiceSelection::feed transitions into DC_ChargeParameterDiscovery, whose enter()
                // emits a DC_CPDReqEnergyTransferMode request. The pump Session wires no DC-params
                // monitor, so the limits are default/zero — assert only the request type and variant.
                {
                    message_20::ServiceSelectionResponse selection_res{};
                    selection_res.header.session_id = session_id;
                    selection_res.response_code = message_20::datatypes::ResponseCode::OK;
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(selection_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 8; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20DC);

                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::DC_ChargeParameterDiscoveryRequest>();
                    REQUIRE(request != nullptr);
                    REQUIRE(request->header.session_id == session_id);
                    REQUIRE(std::holds_alternative<message_20::datatypes::DC_CPDReqEnergyTransferMode>(
                        request->transfer_mode));
                }

                // --- inject DC_ChargeParameterDiscoveryResponse(OK) -> ScheduleExchangeRequest ---
                // DC_ChargeParameterDiscovery::feed transitions into ScheduleExchange, whose enter()
                // emits a minimal Dynamic ScheduleExchangeRequest.
                {
                    message_20::DC_ChargeParameterDiscoveryResponse discovery_res{};
                    discovery_res.header.session_id = session_id;
                    discovery_res.response_code = message_20::datatypes::ResponseCode::OK;
                    discovery_res.transfer_mode = message_20::datatypes::DC_CPDResEnergyTransferMode{};
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20DC, serialize_msg(discovery_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 9; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);

                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::ScheduleExchangeRequest>();
                    REQUIRE(request != nullptr);
                    REQUIRE(request->header.session_id == session_id);
                }

                // --- inject ScheduleExchangeResponse(OK, Finished) -> DC_CableCheckRequest ---
                // ScheduleExchange::feed fires ev_power_ready on a Finished response and transitions
                // into DC_CableCheck, whose enter() emits a DC_CableCheckRequest.
                REQUIRE_FALSE(ev_power_ready);
                {
                    message_20::ScheduleExchangeResponse schedule_res{};
                    schedule_res.header.session_id = session_id;
                    schedule_res.response_code = message_20::datatypes::ResponseCode::OK;
                    schedule_res.processing = message_20::datatypes::Processing::Finished;
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(schedule_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 10; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20DC);

                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::DC_CableCheckRequest>();
                    REQUIRE(request != nullptr);
                    REQUIRE(request->header.session_id == session_id);
                }

                // The Finished ScheduleExchangeResponse fired the ev_power_ready feedback.
                REQUIRE(ev_power_ready);

                // --- inject DC_CableCheckResponse(OK, Finished) -> DC_PreChargeRequest ---
                // DC_CableCheck::feed transitions into DC_PreCharge on a Finished response, whose
                // enter() emits a DC_PreChargeRequest(Ongoing).
                {
                    message_20::DC_CableCheckResponse cable_check_res{};
                    cable_check_res.header.session_id = session_id;
                    cable_check_res.response_code = message_20::datatypes::ResponseCode::OK;
                    cable_check_res.processing = message_20::datatypes::Processing::Finished;
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20DC, serialize_msg(cable_check_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 11; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20DC);

                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::DC_PreChargeRequest>();
                    REQUIRE(request != nullptr);
                    REQUIRE(request->header.session_id == session_id);
                    REQUIRE(request->processing == message_20::datatypes::Processing::Ongoing);
                }

                // --- inject DC_PreChargeResponse(OK) -> PowerDeliveryRequest(Start) ---
                // The pump Session wires no DC-params monitor, so the EV target voltage defaults to 0
                // and the first OK response counts as in-tolerance: DC_PreCharge::feed fires dc_power_on
                // and transitions into PowerDelivery, whose enter() emits a PowerDeliveryRequest(Start).
                REQUIRE_FALSE(dc_power_on);
                {
                    message_20::DC_PreChargeResponse pre_charge_res{};
                    pre_charge_res.header.session_id = session_id;
                    pre_charge_res.response_code = message_20::datatypes::ResponseCode::OK;
                    pre_charge_res.present_voltage = message_20::datatypes::from_float(0.0f);
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20DC, serialize_msg(pre_charge_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 12; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);

                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::PowerDeliveryRequest>();
                    REQUIRE(request != nullptr);
                    REQUIRE(request->header.session_id == session_id);
                    REQUIRE(request->charge_progress == message_20::datatypes::Progress::Start);
                }

                // The in-tolerance DC_PreChargeResponse fired the dc_power_on feedback.
                REQUIRE(dc_power_on);

                // --- inject PowerDeliveryResponse(OK) -> DC_ChargeLoopRequest ---
                // PowerDelivery(Start)::feed transitions into DC_ChargeLoop, whose enter()
                // emits a Dynamic DC_ChargeLoopRequest.
                {
                    message_20::PowerDeliveryResponse power_delivery_res{};
                    power_delivery_res.header.session_id = session_id;
                    power_delivery_res.response_code = message_20::datatypes::ResponseCode::OK;
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(power_delivery_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 13; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20DC);

                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::DC_ChargeLoopRequest>();
                    REQUIRE(request != nullptr);
                    REQUIRE(request->header.session_id == session_id);
                    REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_DC_CLReqControlMode>(
                        request->control_mode));
                }

                // --- inject DC_ChargeLoopResponse(OK, no Terminate) -> DC_ChargeLoopRequest ---
                // A plain OK response keeps the loop running: DC_ChargeLoop re-emits a fresh
                // loop request and does NOT fire stop_from_charger.
                REQUIRE_FALSE(stop_from_charger);
                {
                    message_20::DC_ChargeLoopResponse charge_loop_res{};
                    charge_loop_res.header.session_id = session_id;
                    charge_loop_res.response_code = message_20::datatypes::ResponseCode::OK;
                    charge_loop_res.control_mode = message_20::datatypes::Dynamic_DC_CLResControlMode{};
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20DC, serialize_msg(charge_loop_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 14; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20DC);

                    auto variant = decode_frame(frame);
                    REQUIRE(variant.get_if<message_20::DC_ChargeLoopRequest>() != nullptr);
                }
                REQUIRE_FALSE(stop_from_charger);

                // --- inject DC_ChargeLoopResponse(OK, Terminate) -> PowerDeliveryRequest(Stop) ---
                // The SECC's Terminate notification breaks the loop: DC_ChargeLoop fires
                // stop_from_charger and drives PowerDelivery(Stop).
                {
                    message_20::DC_ChargeLoopResponse charge_loop_res{};
                    charge_loop_res.header.session_id = session_id;
                    charge_loop_res.response_code = message_20::datatypes::ResponseCode::OK;
                    charge_loop_res.status =
                        message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Terminate};
                    charge_loop_res.control_mode = message_20::datatypes::Dynamic_DC_CLResControlMode{};
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20DC, serialize_msg(charge_loop_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 15; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);

                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::PowerDeliveryRequest>();
                    REQUIRE(request != nullptr);
                    REQUIRE(request->header.session_id == session_id);
                    REQUIRE(request->charge_progress == message_20::datatypes::Progress::Stop);
                }

                // The Terminate notification fired the stop_from_charger feedback.
                REQUIRE(stop_from_charger);

                // --- inject PowerDeliveryResponse(OK) -> DC_WeldingDetectionRequest ---
                // PowerDelivery(Stop)::feed transitions into DC_WeldingDetection, whose enter()
                // emits an Ongoing DC_WeldingDetectionRequest.
                {
                    message_20::PowerDeliveryResponse power_delivery_res{};
                    power_delivery_res.header.session_id = session_id;
                    power_delivery_res.response_code = message_20::datatypes::ResponseCode::OK;
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(power_delivery_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 16; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20DC);

                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::DC_WeldingDetectionRequest>();
                    REQUIRE(request != nullptr);
                    REQUIRE(request->header.session_id == session_id);
                }

                // --- inject DC_WeldingDetectionResponse(OK) -> SessionStopRequest ---
                // DC_WeldingDetection::feed transitions into SessionStop, whose enter() emits a
                // SessionStopRequest(Terminate). No second (Finished) welding request is emitted.
                {
                    message_20::DC_WeldingDetectionResponse welding_res{};
                    welding_res.header.session_id = session_id;
                    welding_res.response_code = message_20::datatypes::ResponseCode::OK;
                    welding_res.present_voltage = message_20::datatypes::from_float(0.0f);
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20DC, serialize_msg(welding_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 17; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);

                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::SessionStopRequest>();
                    REQUIRE(request != nullptr);
                    REQUIRE(request->header.session_id == session_id);
                    REQUIRE(request->charging_session == message_20::datatypes::ChargingSession::Terminate);
                }

                // --- inject SessionStopResponse(OK) -> session finishes cleanly ---
                // SessionStop::feed marks the session stopped; no watchdog park (not timed_out).
                {
                    message_20::SessionStopResponse stop_res{};
                    stop_res.header.session_id = session_id;
                    stop_res.response_code = message_20::datatypes::ResponseCode::OK;
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(stop_res)));
                }
                REQUIRE(pump_until(
                    reactor, [&]() { return session.is_finished(); }, 1s));

                REQUIRE(session.is_finished());
                REQUIRE(stop_from_charger);
                REQUIRE_FALSE(timed_out); // a clean SessionStop, not a watchdog park
            }
        }
    }
}

SCENARIO("EV pump drives a graceful EV-initiated stop from an active DC_ChargeLoop") {
    // Proves the delivery path request_stop() marshals: a StopCharging control event
    // delivered through Session::deliver_control_event latches on the Context and,
    // on the next DC_ChargeLoopResponse, drives PowerDelivery(Stop) ->
    // DC_WeldingDetection -> SessionStop without any SECC Terminate notification.
    GIVEN("A Session walked to an active DC_ChargeLoop") {
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});

        everest::lib::io::event::fd_event_handler reactor;

        std::vector<std::vector<uint8_t>> captured;
        bool timed_out = false;
        bool stop_from_charger = false;

        ev::feedback::Callbacks callbacks{};
        callbacks.timed_out = [&timed_out]() { timed_out = true; };
        callbacks.stop_from_charger = [&stop_from_charger]() { stop_from_charger = true; };

        session::SessionLogger logger{nullptr};

        const ev::SessionTiming timing{5ms, 100ms};

        ev::Session session{callbacks,
                            [&captured](std::vector<uint8_t> frame) {
                                captured.push_back(std::move(frame));
                                return true;
                            },
                            logger,
                            reactor,
                            timing,
                            "EVTESTID01",
                            ev::test::default_advertised_app_protocols()};

        const auto session_id = walk_to_dc_charge_loop(session, reactor, captured);

        WHEN("a StopCharging control event is delivered and the next OK loop response arrives") {
            // The EV requests a stop mid-loop; the SECC has NOT sent Terminate.
            session.deliver_control_event(ev::d20::StopCharging{true});

            message_20::DC_ChargeLoopResponse charge_loop_res{};
            charge_loop_res.header.session_id = session_id;
            charge_loop_res.response_code = message_20::datatypes::ResponseCode::OK;
            charge_loop_res.control_mode = message_20::datatypes::Dynamic_DC_CLResControlMode{};
            session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20DC, serialize_msg(charge_loop_res)));

            THEN("the loop breaks into PowerDelivery(Stop) and walks to a clean SessionStop") {
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 14; }, 1s));
                {
                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);
                    auto variant = decode_frame(frame);
                    const auto* request = variant.get_if<message_20::PowerDeliveryRequest>();
                    REQUIRE(request != nullptr);
                    REQUIRE(request->charge_progress == message_20::datatypes::Progress::Stop);
                }
                // The EV drove the stop; the SECC-Terminate feedback must NOT have fired.
                REQUIRE_FALSE(stop_from_charger);

                message_20::PowerDeliveryResponse power_delivery_res{};
                power_delivery_res.header.session_id = session_id;
                power_delivery_res.response_code = message_20::datatypes::ResponseCode::OK;
                session.on_bytes_received(
                    frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(power_delivery_res)));
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 15; }, 1s));
                {
                    auto variant = decode_frame(captured.back());
                    REQUIRE(variant.get_if<message_20::DC_WeldingDetectionRequest>() != nullptr);
                }

                message_20::DC_WeldingDetectionResponse welding_res{};
                welding_res.header.session_id = session_id;
                welding_res.response_code = message_20::datatypes::ResponseCode::OK;
                welding_res.present_voltage = message_20::datatypes::from_float(0.0f);
                session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20DC, serialize_msg(welding_res)));
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 16; }, 1s));
                {
                    auto variant = decode_frame(captured.back());
                    REQUIRE(variant.get_if<message_20::SessionStopRequest>() != nullptr);
                }

                message_20::SessionStopResponse stop_res{};
                stop_res.header.session_id = session_id;
                stop_res.response_code = message_20::datatypes::ResponseCode::OK;
                session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(stop_res)));
                REQUIRE(pump_until(
                    reactor, [&]() { return session.is_finished(); }, 1s));

                REQUIRE(session.is_finished());
                REQUIRE_FALSE(stop_from_charger);
                REQUIRE_FALSE(timed_out); // a graceful SessionStop, not a watchdog park
            }
        }
    }
}
