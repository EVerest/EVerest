// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Session-level FSM-walk integration test for the ISO 15118-2 EV engine.
//
// Like integration_walk.cpp, the only input is bytes and the only output is captured frames: the
// SupportedAppProtocol handshake hands over to the -2 engine, and every following step injects a
// canned EXI-encoded -2 response and asserts the request the Session emits next. Every -2 frame
// travels under V2GTP payload type SAP (0x8001).
//
// DC EIM walk (each arrow = one injected response -> one emitted request):
//
//   start()                             -> SupportedAppProtocolRequest (-20 DC, -2, DIN)
//   SupportedAppProtocolResponse(2)     -> SessionSetupRequest            (-2 engine)
//   SessionSetupResponse                -> ServiceDiscoveryRequest
//   ServiceDiscoveryResponse            -> PaymentServiceSelectionRequest (ExternalPayment)
//   PaymentServiceSelectionResponse     -> AuthorizationRequest
//   AuthorizationResponse(Finished)     -> ChargeParameterDiscoveryRequest
//   ChargeParameterDiscoveryRes(Fin.)   -> CableCheckRequest
//   CableCheckResponse(Ongoing)         -> CableCheckRequest
//   CableCheckResponse(Finished, Valid) -> PreChargeRequest
//   PreChargeResponse(in tolerance)     -> PowerDeliveryRequest(Start)
//   PowerDeliveryResponse               -> CurrentDemandRequest
//   CurrentDemandResponse               -> CurrentDemandRequest (loop continues)
//   CurrentDemandResponse               -> PowerDeliveryRequest(Stop)  (StopCharging delivered)
//   PowerDeliveryResponse               -> WeldingDetectionRequest
//   WeldingDetectionResponse(0 V)       -> SessionStopRequest(Terminate)
//   SessionStopResponse                 -> session finishes, DLINK_TERMINATE
//
// The AC walk replaces CableCheck/PreCharge/CurrentDemand/WeldingDetection by a ChargingStatus loop,
// the pause walk ends in SessionStopRequest(Pause) and re-joins the session id, and the Plug & Charge
// walk selects Contract and signs the AuthorizationReq.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <openssl/evp.h>
#include <openssl/pem.h>

#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/cable_check.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/charging_status.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/current_demand.hpp>
#include <iso15118/message_2/payment_details.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/power_delivery.hpp>
#include <iso15118/message_2/pre_charge.hpp>
#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/session_setup.hpp>
#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/message_2/type.hpp>
#include <iso15118/message_2/variant.hpp>
#include <iso15118/message_2/welding_detection.hpp>

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/sap_offer.hpp>
#include <iso15118/ev/session_params.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using namespace iso15118::ev::test;

namespace {

namespace dt = message_2::datatypes;
using PT = io::v2gtp::PayloadType;

constexpr dt::SessionId D2_SID{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
constexpr std::array<uint8_t, 6> EVCC_MAC{0x02, 0xDE, 0xAD, 0xBE, 0xEF, 0x01};
constexpr auto EVSE_ID = "DE*PNX*E12345";

constexpr float TARGET_VOLTAGE = 400.0f;
constexpr float TARGET_CURRENT = 100.0f;
constexpr float MAX_CHARGE_CURRENT = 150.0f;
constexpr float MAX_VOLTAGE = 900.0f;
constexpr double PRESENT_SOC = 55.0;
constexpr uint8_t SCHEDULE_TUPLE_ID = 7;

// SECC DC limits announced in ChargeParameterDiscoveryRes and asserted on the feedback.
constexpr float EVSE_MAX_VOLTAGE = 920.0f;
constexpr float EVSE_MAX_CURRENT = 200.0f;
constexpr float EVSE_MAX_POWER = 150000.0f;

// AC: nominal voltage and SECC current, whose product is the expected ac_target_power.
constexpr float AC_NOMINAL_VOLTAGE = 230.0f;
constexpr float AC_EVSE_MAX_CURRENT = 32.0f;

// The -2 engine paces requests at 100 ms (EvseV2G MAX_RES_TIME parity).
constexpr auto MIN_REQUEST_INTERVAL = 100ms;

template <typename Msg> std::vector<uint8_t> serialize_2(const Msg& msg) {
    uint8_t buffer[2048];
    io::StreamOutputView out({buffer, sizeof(buffer)});
    const auto size = message_2::serialize(msg, out);
    return std::vector<uint8_t>(buffer, buffer + size);
}

message_2::Variant decode_2(const std::vector<uint8_t>& frame) {
    uint32_t len_be;
    std::memcpy(&len_be, frame.data() + 4, sizeof(len_be));
    return message_2::Variant{io::StreamInputView{frame.data() + io::SdpPacket::V2GTP_HEADER_SIZE, ntohl(len_be)}};
}

// One walk step: inject a -2 response frame, run until the Session emits the next request, and assert
// it decodes as ExpectedReq. `step` labels failures for localization.
template <typename ExpectedReq, typename ResponseMsg>
ExpectedReq step(SessionFixture& fx, const char* label, const ResponseMsg& response) {
    INFO("walk step: " << label);
    const auto before = fx.captured.size();
    fx.session.on_bytes_received(frame_payload(PT::SAP, serialize_2(response)));
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() > before; }, 1s));
    auto variant = decode_2(fx.captured.back());
    const auto* request = variant.get_if<ExpectedReq>();
    REQUIRE(request != nullptr);
    return *request;
}

template <typename Res> Res ok2(dt::ResponseCode code = dt::ResponseCode::OK) {
    Res res{};
    res.header.session_id = D2_SID;
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

ev::EvSessionParams walk_params(dt::EnergyTransferMode mode = dt::EnergyTransferMode::DC_extended) {
    ev::EvSessionParams params;
    params.evcc_mac = EVCC_MAC;
    params.energy_transfer_mode = mode;
    return params;
}

// A Session advertising @p supported in SAP priority order, mapping the negotiated schema id back
// through SessionOptions::offered_protocols.
std::unique_ptr<SessionFixture> make_fixture(const std::vector<ProtocolId>& supported, ev::EvSessionParams params,
                                             message_20::datatypes::ServiceCategory service,
                                             std::optional<ProtocolId> resume = std::nullopt,
                                             std::optional<dt::SessionId> resumed_session_id = std::nullopt) {
    ev::SapOfferInput input;
    input.supported_protocols = supported;
    input.energy_service = service;
    input.resume_protocol = resume;
    const auto offer = build_sap_offer(input);

    std::vector<message_20::SupportedAppProtocol> advertised;
    for (const auto& entry : offer) {
        advertised.push_back(entry.entry);
    }

    ev::d20::SessionOptions options;
    options.offered_protocols = offer;
    options.resumed_session_id = resumed_session_id;

    return std::make_unique<SessionFixture>("EVTESTID01", ev::SessionTiming{5ms, 1000ms}, walk_dc_params(), advertised,
                                            service, ev::AcChargeParams{}, default_der_control_functions(), true,
                                            std::move(options), std::move(params));
}

// start() -> SupportedAppProtocolRequest -> (SECC picks @p schema_id) -> -2 SessionSetupRequest.
message_2::SessionSetupRequest walk_sap(SessionFixture& fx, uint8_t schema_id, std::size_t offer_size) {
    fx.session.start();
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
    auto sap_variant = decode_frame(fx.captured.back());
    const auto* sap_req = sap_variant.get_if<message_20::SupportedAppProtocolRequest>();
    REQUIRE(sap_req != nullptr);
    REQUIRE(sap_req->app_protocol.size() == offer_size);

    const auto before = fx.captured.size();
    fx.session.on_bytes_received(frame_payload(PT::SAP, serialize_msg(sap_response(schema_id))));
    REQUIRE(fx.selected_protocol == ProtocolId::ISO15118_2);
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() > before; }, 1s));
    auto variant = decode_2(fx.captured.back());
    const auto* request = variant.get_if<message_2::SessionSetupRequest>();
    REQUIRE(request != nullptr);
    return *request;
}

message_2::SessionSetupResponse session_setup_res(dt::ResponseCode code = dt::ResponseCode::OK_NewSessionEstablished) {
    auto res = ok2<message_2::SessionSetupResponse>(code);
    res.evse_id = EVSE_ID;
    return res;
}

message_2::ServiceDiscoveryResponse service_discovery_res(dt::EnergyTransferMode mode, bool offer_contract = false) {
    auto res = ok2<message_2::ServiceDiscoveryResponse>();
    res.payment_option_list = {dt::PaymentOption::ExternalPayment};
    if (offer_contract) {
        res.payment_option_list.push_back(dt::PaymentOption::Contract);
    }
    res.charge_service.service_id = dt::CHARGE_SERVICE_ID;
    res.charge_service.service_category = dt::ServiceCategory::EVCharging;
    res.charge_service.free_service = false;
    res.charge_service.supported_energy_transfer_mode = {mode};
    return res;
}

// A Finished ChargeParameterDiscoveryRes carrying the SAScheduleList the following PowerDeliveryReq
// must reference, plus the DC limits published as dc_evse_present_limits.
message_2::ChargeParameterDiscoveryResponse dc_cpd_res() {
    auto res = ok2<message_2::ChargeParameterDiscoveryResponse>();
    res.evse_processing = dt::EVSEProcessing::Finished;

    dt::SAScheduleTuple tuple;
    tuple.sa_schedule_tuple_id = SCHEDULE_TUPLE_ID;
    dt::PMaxScheduleEntry entry;
    entry.start = 0;
    entry.p_max = dt::to_physical_value(EVSE_MAX_POWER, dt::Unit::W);
    tuple.pmax_schedule.push_back(entry);
    res.sa_schedule_list = dt::SAScheduleList{tuple};

    dt::DC_EVSEChargeParameter evse_param{};
    evse_param.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    evse_param.evse_maximum_voltage_limit = dt::to_physical_value(EVSE_MAX_VOLTAGE, dt::Unit::V);
    evse_param.evse_maximum_current_limit = dt::to_physical_value(EVSE_MAX_CURRENT, dt::Unit::A);
    evse_param.evse_maximum_power_limit = dt::to_physical_value(EVSE_MAX_POWER, dt::Unit::W);
    evse_param.evse_minimum_voltage_limit = dt::to_physical_value(150.0f, dt::Unit::V);
    evse_param.evse_minimum_current_limit = dt::to_physical_value(0.0f, dt::Unit::A);
    evse_param.evse_peak_current_ripple = dt::to_physical_value(1.0f, dt::Unit::A);
    res.dc_evse_charge_parameter = evse_param;
    return res;
}

message_2::ChargeParameterDiscoveryResponse ac_cpd_res() {
    auto res = ok2<message_2::ChargeParameterDiscoveryResponse>();
    res.evse_processing = dt::EVSEProcessing::Finished;

    dt::SAScheduleTuple tuple;
    tuple.sa_schedule_tuple_id = SCHEDULE_TUPLE_ID;
    dt::PMaxScheduleEntry entry;
    entry.start = 0;
    entry.p_max = dt::to_physical_value(22000.0f, dt::Unit::W);
    tuple.pmax_schedule.push_back(entry);
    res.sa_schedule_list = dt::SAScheduleList{tuple};

    dt::AC_EVSEChargeParameter evse_param{};
    evse_param.evse_nominal_voltage = dt::to_physical_value(AC_NOMINAL_VOLTAGE, dt::Unit::V);
    evse_param.evse_max_current = dt::to_physical_value(AC_EVSE_MAX_CURRENT, dt::Unit::A);
    res.ac_evse_charge_parameter = evse_param;
    return res;
}

message_2::CableCheckResponse cable_check_res(dt::EVSEProcessing processing) {
    auto res = ok2<message_2::CableCheckResponse>();
    res.evse_processing = processing;
    res.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    res.dc_evse_status.isolation_status = dt::IsolationLevel::Valid;
    return res;
}

message_2::PreChargeResponse pre_charge_res() {
    auto res = ok2<message_2::PreChargeResponse>();
    res.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    res.evse_present_voltage = dt::to_physical_value(TARGET_VOLTAGE, dt::Unit::V);
    return res;
}

message_2::CurrentDemandResponse current_demand_res(dt::EVSENotification notification = dt::EVSENotification::None) {
    auto res = ok2<message_2::CurrentDemandResponse>();
    res.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    res.dc_evse_status.notification = notification;
    res.evse_present_voltage = dt::to_physical_value(TARGET_VOLTAGE, dt::Unit::V);
    res.evse_present_current = dt::to_physical_value(TARGET_CURRENT, dt::Unit::A);
    res.evse_id = EVSE_ID;
    res.sa_schedule_tuple_id = SCHEDULE_TUPLE_ID;
    res.evse_maximum_voltage_limit = dt::to_physical_value(EVSE_MAX_VOLTAGE, dt::Unit::V);
    res.evse_maximum_current_limit = dt::to_physical_value(EVSE_MAX_CURRENT, dt::Unit::A);
    res.evse_maximum_power_limit = dt::to_physical_value(EVSE_MAX_POWER, dt::Unit::W);
    return res;
}

message_2::ChargingStatusResponse charging_status_res(dt::EVSENotification notification = dt::EVSENotification::None) {
    auto res = ok2<message_2::ChargingStatusResponse>();
    res.evse_id = EVSE_ID;
    res.sa_schedule_tuple_id = SCHEDULE_TUPLE_ID;
    res.evse_max_current = dt::to_physical_value(AC_EVSE_MAX_CURRENT, dt::Unit::A);
    res.ac_evse_status.notification = notification;
    return res;
}

message_2::WeldingDetectionResponse welding_detection_res() {
    auto res = ok2<message_2::WeldingDetectionResponse>();
    res.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    res.evse_present_voltage = dt::to_physical_value(0.0f, dt::Unit::V);
    return res;
}

// SessionSetup -> ServiceDiscovery -> PaymentServiceSelection -> Authorization, EIM throughout.
void walk_eim_to_authorization(SessionFixture& fx, dt::EnergyTransferMode mode) {
    {
        const auto req =
            step<message_2::ServiceDiscoveryRequest>(fx, "SessionSetup -> ServiceDiscovery", session_setup_res());
        REQUIRE(req.header.session_id == D2_SID);
    }
    REQUIRE(fx.evse_id == EVSE_ID);

    {
        const auto req = step<message_2::PaymentServiceSelectionRequest>(
            fx, "ServiceDiscovery -> PaymentServiceSelection", service_discovery_res(mode));
        REQUIRE(req.header.session_id == D2_SID);
        REQUIRE(req.selected_payment_option == dt::PaymentOption::ExternalPayment);
        REQUIRE(req.selected_service_list.size() == 1);
        REQUIRE(req.selected_service_list[0].service_id == dt::CHARGE_SERVICE_ID);
    }

    {
        const auto req = step<message_2::AuthorizationRequest>(fx, "PaymentServiceSelection -> Authorization",
                                                               ok2<message_2::PaymentServiceSelectionResponse>());
        REQUIRE(req.header.session_id == D2_SID);
        REQUIRE_FALSE(req.gen_challenge.has_value());
    }
}

message_2::AuthorizationResponse authorization_res() {
    auto res = ok2<message_2::AuthorizationResponse>();
    res.evse_processing = dt::EVSEProcessing::Finished;
    return res;
}

// SAP through the first CurrentDemandRequest of a DC EIM session.
void walk_dc_to_current_demand(SessionFixture& fx) {
    const auto setup_req = walk_sap(fx, 2, 3);
    REQUIRE(setup_req.evcc_id == EVCC_MAC);
    REQUIRE(setup_req.header.session_id == dt::SessionId{});

    walk_eim_to_authorization(fx, dt::EnergyTransferMode::DC_extended);

    {
        const auto req = step<message_2::ChargeParameterDiscoveryRequest>(
            fx, "Authorization -> ChargeParameterDiscovery", authorization_res());
        REQUIRE(req.requested_energy_transfer_mode == dt::EnergyTransferMode::DC_extended);
        REQUIRE(req.dc_ev_charge_parameter.has_value());
        REQUIRE(req.dc_ev_charge_parameter->dc_ev_status.ev_ress_soc == static_cast<int8_t>(PRESENT_SOC));
        REQUIRE(dt::from_physical_value(req.dc_ev_charge_parameter->ev_maximum_current_limit) ==
                Catch::Approx(MAX_CHARGE_CURRENT));
        REQUIRE(dt::from_physical_value(req.dc_ev_charge_parameter->ev_maximum_voltage_limit) ==
                Catch::Approx(MAX_VOLTAGE));
    }

    REQUIRE_FALSE(fx.ev_power_ready);
    {
        const auto req = step<message_2::CableCheckRequest>(fx, "ChargeParameterDiscovery -> CableCheck", dc_cpd_res());
        REQUIRE(req.header.session_id == D2_SID);
        REQUIRE(req.dc_ev_status.ev_ready);
    }
    REQUIRE(fx.ev_power_ready);
    REQUIRE(fx.dc_present_limits.has_value());
    REQUIRE(fx.dc_present_limits->voltage == Catch::Approx(EVSE_MAX_VOLTAGE));
    REQUIRE(fx.dc_present_limits->current == Catch::Approx(EVSE_MAX_CURRENT));

    step<message_2::CableCheckRequest>(fx, "CableCheck(Ongoing) -> CableCheck",
                                       cable_check_res(dt::EVSEProcessing::Ongoing));

    {
        const auto req = step<message_2::PreChargeRequest>(fx, "CableCheck(Finished) -> PreCharge",
                                                           cable_check_res(dt::EVSEProcessing::Finished));
        REQUIRE(dt::from_physical_value(req.ev_target_voltage) == Catch::Approx(TARGET_VOLTAGE));
        REQUIRE(dt::from_physical_value(req.ev_target_current) == Catch::Approx(0.0));
    }

    REQUIRE_FALSE(fx.dc_power_on);
    {
        const auto req =
            step<message_2::PowerDeliveryRequest>(fx, "PreCharge -> PowerDelivery(Start)", pre_charge_res());
        REQUIRE(req.charge_progress == dt::ChargeProgress::Start);
        REQUIRE(req.sa_schedule_tuple_id == SCHEDULE_TUPLE_ID);
        REQUIRE(req.charging_profile.has_value());
        REQUIRE(req.dc_ev_power_delivery_parameter.has_value());
    }
    REQUIRE(fx.dc_power_on);

    {
        const auto req = step<message_2::CurrentDemandRequest>(fx, "PowerDelivery -> CurrentDemand",
                                                               ok2<message_2::PowerDeliveryResponse>());
        REQUIRE(req.header.session_id == D2_SID);
        REQUIRE(dt::from_physical_value(req.ev_target_voltage) == Catch::Approx(TARGET_VOLTAGE));
        REQUIRE(dt::from_physical_value(req.ev_target_current) == Catch::Approx(TARGET_CURRENT));
        REQUIRE(req.dc_ev_status.ev_ress_soc == static_cast<int8_t>(PRESENT_SOC));
    }
}

// PowerDelivery(Stop) -> WeldingDetection -> SessionStop -> clean finish.
void walk_dc_stop_to_finish(SessionFixture& fx, dt::ChargingSession expected_reason) {
    step<message_2::WeldingDetectionRequest>(fx, "PowerDelivery(Stop) -> WeldingDetection",
                                             ok2<message_2::PowerDeliveryResponse>());

    {
        const auto req =
            step<message_2::SessionStopRequest>(fx, "WeldingDetection -> SessionStop", welding_detection_res());
        REQUIRE(req.header.session_id == D2_SID);
        REQUIRE(req.charging_session == expected_reason);
    }

    fx.session.on_bytes_received(frame_payload(PT::SAP, serialize_2(ok2<message_2::SessionStopResponse>())));
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.session.is_finished(); }, 1s));
    REQUIRE_FALSE(fx.timed_out);
}

// A freshly generated secp256r1 private key in PEM, for the Plug & Charge signing path.
std::string make_test_ec_key_pem() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1);
    EVP_PKEY_keygen(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    char* data = nullptr;
    const long length = BIO_get_mem_data(bio, &data);
    std::string pem(data, static_cast<size_t>(length));
    BIO_free(bio);
    EVP_PKEY_free(pkey);
    return pem;
}

} // namespace

SCENARIO("ISO15118-2 EV Session walks a DC EIM session to a graceful stop") {
    GIVEN("a Session offering ISO 15118-20 DC, ISO 15118-2 and DIN SPEC 70121") {
        auto fx = make_fixture({ProtocolId::ISO15118_20, ProtocolId::ISO15118_2, ProtocolId::DIN70121}, walk_params(),
                               message_20::datatypes::ServiceCategory::DC);

        WHEN("the SECC selects the ISO 15118-2 schema id and answers every request") {
            walk_dc_to_current_demand(*fx);

            THEN("the charge loop paces requests and a StopCharging ends the session cleanly") {
                const auto first_request = std::chrono::steady_clock::now();
                step<message_2::CurrentDemandRequest>(*fx, "CurrentDemand -> CurrentDemand", current_demand_res());
                const auto second_request = std::chrono::steady_clock::now();
                REQUIRE(second_request - first_request >= MIN_REQUEST_INTERVAL);

                fx->session.deliver_control_event(ev::d20::ControlEvent{ev::d20::StopCharging{true}});

                const auto req = step<message_2::PowerDeliveryRequest>(*fx, "CurrentDemand -> PowerDelivery(Stop)",
                                                                       current_demand_res());
                REQUIRE(req.charge_progress == dt::ChargeProgress::Stop);
                REQUIRE_FALSE(req.charging_profile.has_value());

                walk_dc_stop_to_finish(*fx, dt::ChargingSession::Terminate);

                REQUIRE(fx->session.is_finished());
                REQUIRE_FALSE(fx->session.is_paused());
                REQUIRE(fx->session.session_id() == D2_SID);
                REQUIRE(fx->session.selected_protocol() == ProtocolId::ISO15118_2);
                REQUIRE(fx->signals.back() == ev::feedback::Signal::DLINK_TERMINATE);
            }
        }
    }
}

SCENARIO("ISO15118-2 EV Session walks an AC EIM session through the ChargingStatus loop") {
    GIVEN("an AC three-phase Session offering ISO 15118-20 AC and ISO 15118-2") {
        auto fx = make_fixture({ProtocolId::ISO15118_20, ProtocolId::ISO15118_2},
                               walk_params(dt::EnergyTransferMode::AC_three_phase_core),
                               message_20::datatypes::ServiceCategory::AC);

        WHEN("the SECC selects the ISO 15118-2 schema id") {
            // DIN SPEC 70121 is never offered for an AC service, so -2 is schema id 2 of two entries.
            const auto setup_req = walk_sap(*fx, 2, 2);
            REQUIRE(setup_req.evcc_id == EVCC_MAC);

            walk_eim_to_authorization(*fx, dt::EnergyTransferMode::AC_three_phase_core);

            THEN("the AC branch skips CableCheck and stops without WeldingDetection") {
                {
                    const auto req = step<message_2::ChargeParameterDiscoveryRequest>(
                        *fx, "Authorization -> ChargeParameterDiscovery", authorization_res());
                    REQUIRE(req.requested_energy_transfer_mode == dt::EnergyTransferMode::AC_three_phase_core);
                    REQUIRE(req.ac_ev_charge_parameter.has_value());
                    REQUIRE_FALSE(req.dc_ev_charge_parameter.has_value());
                }

                {
                    const auto req = step<message_2::PowerDeliveryRequest>(
                        *fx, "ChargeParameterDiscovery -> PowerDelivery(Start)", ac_cpd_res());
                    REQUIRE(req.charge_progress == dt::ChargeProgress::Start);
                    REQUIRE(req.sa_schedule_tuple_id == SCHEDULE_TUPLE_ID);
                    REQUIRE_FALSE(req.dc_ev_power_delivery_parameter.has_value());
                }
                REQUIRE(fx->ev_power_ready);

                step<message_2::ChargingStatusRequest>(*fx, "PowerDelivery -> ChargingStatus",
                                                       ok2<message_2::PowerDeliveryResponse>());

                REQUIRE_FALSE(fx->ac_target_power);
                step<message_2::ChargingStatusRequest>(*fx, "ChargingStatus -> ChargingStatus", charging_status_res());
                REQUIRE(fx->ac_target_power);

                fx->session.deliver_control_event(ev::d20::ControlEvent{ev::d20::StopCharging{true}});
                {
                    const auto req = step<message_2::PowerDeliveryRequest>(*fx, "ChargingStatus -> PowerDelivery(Stop)",
                                                                           charging_status_res());
                    REQUIRE(req.charge_progress == dt::ChargeProgress::Stop);
                }

                {
                    const auto req = step<message_2::SessionStopRequest>(*fx, "PowerDelivery(Stop) -> SessionStop",
                                                                         ok2<message_2::PowerDeliveryResponse>());
                    REQUIRE(req.charging_session == dt::ChargingSession::Terminate);
                }

                fx->session.on_bytes_received(
                    frame_payload(PT::SAP, serialize_2(ok2<message_2::SessionStopResponse>())));
                REQUIRE(run_reactor_until(
                    fx->reactor, [&]() { return fx->session.is_finished(); }, 1s));
                REQUIRE_FALSE(fx->session.is_paused());
            }
        }
    }
}

SCENARIO("ISO15118-2 EV Session pauses and re-joins the paused session id") {
    GIVEN("a DC Session in the CurrentDemand loop") {
        auto fx = make_fixture({ProtocolId::ISO15118_20, ProtocolId::ISO15118_2, ProtocolId::DIN70121}, walk_params(),
                               message_20::datatypes::ServiceCategory::DC);
        walk_dc_to_current_demand(*fx);

        WHEN("a PauseCharging is delivered in the loop") {
            fx->session.deliver_control_event(ev::d20::ControlEvent{ev::d20::PauseCharging{true}});
            const auto req = step<message_2::PowerDeliveryRequest>(*fx, "CurrentDemand -> PowerDelivery(Stop)",
                                                                   current_demand_res());
            REQUIRE(req.charge_progress == dt::ChargeProgress::Stop);

            THEN("SessionStop requests a Pause and the session parks the session id") {
                walk_dc_stop_to_finish(*fx, dt::ChargingSession::Pause);

                REQUIRE(fx->session.is_paused());
                REQUIRE(fx->session.session_id() == D2_SID);
                REQUIRE(fx->signals.back() == ev::feedback::Signal::DLINK_PAUSE);
            }
        }
    }

    GIVEN("a second Session constructed with the paused session id and an offer constrained to -2") {
        auto fx = make_fixture({ProtocolId::ISO15118_20, ProtocolId::ISO15118_2, ProtocolId::DIN70121}, walk_params(),
                               message_20::datatypes::ServiceCategory::DC, ProtocolId::ISO15118_2, D2_SID);

        WHEN("the SECC accepts the resumed session") {
            const auto setup_req = walk_sap(*fx, 1, 1);

            THEN("SessionSetupReq carries the id and OK_OldSessionJoined is accepted") {
                REQUIRE(setup_req.header.session_id == D2_SID);
                const auto req =
                    step<message_2::ServiceDiscoveryRequest>(*fx, "SessionSetup(OldSessionJoined) -> ServiceDiscovery",
                                                             session_setup_res(dt::ResponseCode::OK_OldSessionJoined));
                REQUIRE(req.header.session_id == D2_SID);
                REQUIRE_FALSE(fx->session.is_finished());
            }
        }
    }
}

SCENARIO("ISO15118-2 EV Session follows a charger-initiated stop") {
    GIVEN("a DC Session in the CurrentDemand loop") {
        auto fx = make_fixture({ProtocolId::ISO15118_20, ProtocolId::ISO15118_2, ProtocolId::DIN70121}, walk_params(),
                               message_20::datatypes::ServiceCategory::DC);
        walk_dc_to_current_demand(*fx);

        WHEN("the SECC answers with an EVSENotification of StopCharging") {
            REQUIRE_FALSE(fx->stop_from_charger);
            const auto req =
                step<message_2::PowerDeliveryRequest>(*fx, "CurrentDemand(StopCharging) -> PowerDelivery(Stop)",
                                                      current_demand_res(dt::EVSENotification::StopCharging));

            THEN("stop_from_charger fires and the session takes the stop path") {
                REQUIRE(fx->stop_from_charger);
                REQUIRE(req.charge_progress == dt::ChargeProgress::Stop);
                walk_dc_stop_to_finish(*fx, dt::ChargingSession::Terminate);
                REQUIRE_FALSE(fx->session.is_paused());
            }
        }
    }
}

SCENARIO("ISO15118-2 EV Session runs a Plug & Charge contract session") {
    GIVEN("a DC Session preferring a pre-installed contract") {
        auto params = walk_params();
        params.pnc.prefer_contract = true;
        params.pnc.contract_cert_der = {0x30, 0x02, 0x03};
        params.pnc.contract_sub_certs_der = {{0x30, 0x04}};
        params.pnc.contract_key_pem = make_test_ec_key_pem();
        params.pnc.contract_emaid = "DEPNXCONTRACT1";

        auto fx = make_fixture({ProtocolId::ISO15118_20, ProtocolId::ISO15118_2, ProtocolId::DIN70121},
                               std::move(params), message_20::datatypes::ServiceCategory::DC);

        WHEN("the SECC offers Contract alongside ExternalPayment") {
            walk_sap(*fx, 2, 3);
            step<message_2::ServiceDiscoveryRequest>(*fx, "SessionSetup -> ServiceDiscovery", session_setup_res());

            const auto selection = step<message_2::PaymentServiceSelectionRequest>(
                *fx, "ServiceDiscovery -> PaymentServiceSelection",
                service_discovery_res(dt::EnergyTransferMode::DC_extended, true));

            THEN("Contract is selected, the chain is presented and the AuthorizationReq is signed") {
                REQUIRE(selection.selected_payment_option == dt::PaymentOption::Contract);

                {
                    const auto req =
                        step<message_2::PaymentDetailsRequest>(*fx, "PaymentServiceSelection -> PaymentDetails",
                                                               ok2<message_2::PaymentServiceSelectionResponse>());
                    REQUIRE(req.emaid == "DEPNXCONTRACT1");
                    REQUIRE(req.contract_certificate.size() == 3);
                    REQUIRE(req.sub_certificates.size() == 1);
                }

                constexpr dt::GenChallenge challenge{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
                auto details_res = ok2<message_2::PaymentDetailsResponse>();
                details_res.gen_challenge = challenge;
                {
                    const auto req =
                        step<message_2::AuthorizationRequest>(*fx, "PaymentDetails -> Authorization", details_res);
                    REQUIRE(req.id == "id1");
                    REQUIRE(req.gen_challenge.has_value());
                    REQUIRE(req.gen_challenge.value() == challenge);
                }

                const auto req = step<message_2::ChargeParameterDiscoveryRequest>(
                    *fx, "Authorization -> ChargeParameterDiscovery", authorization_res());
                REQUIRE(req.requested_energy_transfer_mode == dt::EnergyTransferMode::DC_extended);
            }
        }
    }
}

SCENARIO("ISO15118-2 EV Session ignores frames with a non-SAP payload type") {
    GIVEN("a Session that handed over to the ISO 15118-2 engine") {
        auto fx = make_fixture({ProtocolId::ISO15118_20, ProtocolId::ISO15118_2, ProtocolId::DIN70121}, walk_params(),
                               message_20::datatypes::ServiceCategory::DC);
        walk_sap(*fx, 2, 3);
        const auto captured_before = fx->captured.size();

        WHEN("a SessionSetupRes arrives under the ISO 15118-20 payload type") {
            fx->session.on_bytes_received(frame_payload(PT::Part20Main, serialize_2(session_setup_res())));
            REQUIRE_FALSE(run_reactor_until(
                fx->reactor, [&]() { return fx->captured.size() > captured_before; }, 200ms));

            THEN("nothing changes and the same response under SAP still advances the session") {
                REQUIRE(fx->captured.size() == captured_before);
                REQUIRE_FALSE(fx->session.is_finished());
                REQUIRE(fx->evse_id.empty());

                const auto req = step<message_2::ServiceDiscoveryRequest>(*fx, "SessionSetup -> ServiceDiscovery",
                                                                          session_setup_res());
                REQUIRE(req.header.session_id == D2_SID);
                REQUIRE_FALSE(fx->timed_out);
            }
        }
    }
}

SCENARIO("ISO15118-2 EV Session re-arms a dropped frame with the configured response timeout") {
    GIVEN("a Session with a 1000 ms override against the 2000 ms -2 message timeout") {
        auto fx = make_fixture({ProtocolId::ISO15118_20, ProtocolId::ISO15118_2, ProtocolId::DIN70121}, walk_params(),
                               message_20::datatypes::ServiceCategory::DC);
        walk_sap(*fx, 2, 3);

        WHEN("a frame under a non-SAP payload type is dropped while SessionSetupRes is outstanding") {
            fx->session.on_bytes_received(frame_payload(PT::Part20Main, serialize_2(session_setup_res())));

            THEN("the watchdog fires at the override, not at the engine's table value") {
                REQUIRE_FALSE(run_reactor_until(
                    fx->reactor, [&]() { return fx->timed_out; }, 800ms));
                REQUIRE(run_reactor_until(
                    fx->reactor, [&]() { return fx->timed_out; }, 800ms));
            }
        }
    }
}
