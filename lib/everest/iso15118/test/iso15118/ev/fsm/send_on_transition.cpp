// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "helper.hpp"

#include <iso15118/ev/d20/state/ac_charge_loop.hpp>
#include <iso15118/ev/d20/state/ac_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/ac_der_iec_charge_loop.hpp>
#include <iso15118/ev/d20/state/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/ac_der_sae_charge_loop.hpp>
#include <iso15118/ev/d20/state/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/authorization.hpp>
#include <iso15118/ev/d20/state/authorization_setup.hpp>
#include <iso15118/ev/d20/state/dc_cable_check.hpp>
#include <iso15118/ev/d20/state/dc_charge_loop.hpp>
#include <iso15118/ev/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/dc_pre_charge.hpp>
#include <iso15118/ev/d20/state/dc_welding_detection.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/ev/d20/state/service_detail.hpp>
#include <iso15118/ev/d20/state/service_discovery.hpp>
#include <iso15118/ev/d20/state/service_selection.hpp>
#include <iso15118/ev/d20/state/session_setup.hpp>
#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/ev/d20/state/supported_app_protocol.hpp>
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_iec_charge_loop.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
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

using namespace iso15118;

namespace {

namespace dt = message_20::datatypes;
namespace state = ev::d20::state;
using dt::Processing;
using dt::ResponseCode;
using dt::ServiceCategory;
using ev::d20::Disposition;
using ev::d20::Event;
using ev::d20::StateID;

dt::ParameterSet dynamic_set(uint16_t id) {
    dt::ParameterSet set{};
    set.id = id;
    set.parameter.push_back({"ControlMode", static_cast<int32_t>(dt::ControlMode::Dynamic)});
    return set;
}

message_20::ScheduleExchangeResponse schedule_finished() {
    message_20::ScheduleExchangeResponse res{};
    res.header = SESSION_HEADER;
    res.response_code = ResponseCode::OK;
    res.processing = Processing::Finished;
    return res;
}

message_20::PowerDeliveryResponse power_delivery_ok() {
    return {SESSION_HEADER, ResponseCode::OK, std::nullopt};
}

const dt::EvseStatus evse_terminate{0, dt::EvseNotification::Terminate};

} // namespace

SCENARIO("ISO15118-20 EV session setup transitions send the successor's first request") {
    GIVEN("SupportedAppProtocol accepting -20") {
        auto pred = Predecessor::dc<state::SupportedAppProtocol>();
        pred.helper->get_message_exchange().set_response(make_sap_ok_response());
        const auto sent = pred.feed_into(StateID::SessionSetup);
        const auto req = sent.get<message_20::SessionSetupRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->evccid == "EVTESTID01");
    }

    GIVEN("SupportedAppProtocol re-joining a paused session") {
        const std::array<uint8_t, 8> paused{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        ev::d20::SessionOptions options{};
        options.resume = ev::d20::Resume{paused};
        auto pred = Predecessor::in<state::SupportedAppProtocol>(ServiceCategory::DC, options);
        pred.helper->get_message_exchange().set_response(make_sap_ok_response());
        const auto sent = pred.feed_into(StateID::SessionSetup);
        const auto req = sent.get<message_20::SessionSetupRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->header.session_id == paused);
    }

    GIVEN("SessionSetup establishing a new session") {
        auto pred = Predecessor::dc<state::SessionSetup>();
        const auto sent = pred.transitions_to(
            StateID::AuthorizationSetup,
            message_20::SessionSetupResponse{SESSION_HEADER, ResponseCode::OK_NewSessionEstablished, "everest se"});
        const auto req = sent.get<message_20::AuthorizationSetupRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->header.session_id == SESSION_HEADER.session_id);
    }

    GIVEN("AuthorizationSetup offering EIM") {
        auto pred = Predecessor::dc<state::AuthorizationSetup>();
        const auto sent = pred.transitions_to(
            StateID::Authorization,
            message_20::AuthorizationSetupResponse{SESSION_HEADER, ResponseCode::OK, {dt::Authorization::EIM}, false});
        const auto req = sent.get<message_20::AuthorizationRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->selected_authorization_service == dt::Authorization::EIM);
    }

    GIVEN("Authorization finished") {
        auto pred = Predecessor::dc<state::Authorization>();
        const auto sent = pred.transitions_to(
            StateID::ServiceDiscovery,
            message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK, Processing::Finished});
        const auto req = sent.get<message_20::ServiceDiscoveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->supported_service_ids.has_value());
    }

    GIVEN("ServiceDiscovery offering DC") {
        auto pred = Predecessor::dc<state::ServiceDiscovery>();
        message_20::ServiceDiscoveryResponse res{};
        res.header = SESSION_HEADER;
        res.response_code = ResponseCode::OK;
        res.energy_transfer_service_list = {{ServiceCategory::DC, false}};
        const auto sent = pred.transitions_to(StateID::ServiceDetail, res);
        const auto req = sent.get<message_20::ServiceDetailRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->service == message_20::to_underlying_value(ServiceCategory::DC));
    }

    GIVEN("ServiceDetail offering a Dynamic set") {
        auto pred = Predecessor::dc<state::ServiceDetail>();
        message_20::ServiceDetailResponse res{};
        res.header = SESSION_HEADER;
        res.response_code = ResponseCode::OK;
        res.service = message_20::to_underlying_value(ServiceCategory::DC);
        res.service_parameter_list.clear();
        res.service_parameter_list.push_back(dynamic_set(7));
        const auto sent = pred.transitions_to(StateID::ServiceSelection, res);
        const auto req = sent.get<message_20::ServiceSelectionRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->selected_energy_transfer_service.service_id == ServiceCategory::DC);
        REQUIRE(req->selected_energy_transfer_service.parameter_set_id == 7);
    }
}

SCENARIO("ISO15118-20 EV ServiceSelection sends the selected service's ChargeParameterDiscoveryReq") {
    const message_20::ServiceSelectionResponse ok{SESSION_HEADER, ResponseCode::OK};

    GIVEN("DC") {
        auto pred = Predecessor::in<state::ServiceSelection>(ServiceCategory::DC, {}, uint16_t{1});
        const auto sent = pred.transitions_to(StateID::DC_ChargeParameterDiscovery, ok);
        REQUIRE(sent.get<message_20::DC_ChargeParameterDiscoveryRequest>().has_value());
    }

    GIVEN("AC") {
        auto pred = Predecessor::in<state::ServiceSelection>(ServiceCategory::AC, {}, uint16_t{1});
        const auto sent = pred.transitions_to(StateID::AC_ChargeParameterDiscovery, ok);
        REQUIRE(sent.get<message_20::AC_ChargeParameterDiscoveryRequest>().has_value());
    }

    GIVEN("AC_DER_IEC") {
        auto pred = Predecessor::in<state::ServiceSelection>(ServiceCategory::AC_DER_IEC, {}, uint16_t{1});
        const auto sent = pred.transitions_to(StateID::AC_DER_IEC_ChargeParameterDiscovery, ok);
        REQUIRE(sent.get<message_20::DER_AC_ChargeParameterDiscoveryRequest>().has_value());
    }

    GIVEN("AC_DER_SAE with two rounds") {
        ev::d20::SessionOptions options{};
        options.d20.cpd_rounds = 2;
        auto pred = Predecessor::in<state::ServiceSelection>(ServiceCategory::AC_DER_SAE, options, uint16_t{1});
        const auto sent = pred.transitions_to(StateID::AC_DER_SAE_ChargeParameterDiscovery, ok);
        const auto req = sent.get<message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->transfer_mode.processing == Processing::Ongoing);
    }

    GIVEN("AC_DER_SAE with two rounds, the first answered Ongoing") {
        ev::d20::SessionOptions options{};
        options.d20.cpd_rounds = 2;
        const ev::feedback::Callbacks callbacks{};
        FsmStateHelper helper{callbacks, DEFAULT_APP_PROTOCOLS, ServiceCategory::AC_DER_SAE, options};
        helper.get_context().get_session().set_id(SESSION_HEADER.session_id);
        const auto discovery = successor_of<state::ServiceSelection>(helper, ok, uint16_t{1});

        message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse res{};
        res.header = SESSION_HEADER;
        res.response_code = ResponseCode::OK;
        res.transfer_mode.processing = Processing::Ongoing;
        helper.handle_response(res);
        REQUIRE(discovery->feed(Event::V2GTP_MESSAGE).output == ev::d20::Disposition::Awaiting);

        // The round ServiceSelection sent counts, so the second is the last.
        const auto req = take_all_requests(helper.get_message_exchange())
                             .get<message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->transfer_mode.processing == Processing::Finished);
    }
}

SCENARIO("ISO15118-20 EV ChargeParameterDiscovery sends ScheduleExchangeReq on its transition") {
    GIVEN("DC") {
        auto pred = Predecessor::dc<state::DC_ChargeParameterDiscovery>();
        const auto sent = pred.transitions_to(StateID::ScheduleExchange,
                                              message_20::DC_ChargeParameterDiscoveryResponse{
                                                  SESSION_HEADER, ResponseCode::OK, dt::DC_CPDResEnergyTransferMode{}});
        const auto req = sent.get<message_20::ScheduleExchangeRequest>();
        REQUIRE(req.has_value());
        REQUIRE(std::holds_alternative<dt::Dynamic_SEReqControlMode>(req->control_mode));
    }

    GIVEN("AC") {
        auto pred = Predecessor::in<state::AC_ChargeParameterDiscovery>(ServiceCategory::AC, {});
        const auto sent = pred.transitions_to(StateID::ScheduleExchange,
                                              message_20::AC_ChargeParameterDiscoveryResponse{
                                                  SESSION_HEADER, ResponseCode::OK, dt::AC_CPDResEnergyTransferMode{}});
        REQUIRE(sent.get<message_20::ScheduleExchangeRequest>().has_value());
    }

    GIVEN("AC_DER_IEC") {
        auto pred = Predecessor::in<state::AC_DER_IEC_ChargeParameterDiscovery>(ServiceCategory::AC_DER_IEC, {});
        message_20::DER_AC_ChargeParameterDiscoveryResponse res{};
        res.header = SESSION_HEADER;
        res.response_code = ResponseCode::OK;
        const auto sent = pred.transitions_to(StateID::ScheduleExchange, res);
        REQUIRE(sent.get<message_20::ScheduleExchangeRequest>().has_value());
    }

    GIVEN("AC_DER_SAE") {
        auto pred = Predecessor::in<state::AC_DER_SAE_ChargeParameterDiscovery>(ServiceCategory::AC_DER_SAE, {});
        message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse res{};
        res.header = SESSION_HEADER;
        res.response_code = ResponseCode::OK;
        res.transfer_mode.processing = Processing::Finished;
        const auto sent = pred.transitions_to(StateID::ScheduleExchange, res);
        REQUIRE(sent.get<message_20::ScheduleExchangeRequest>().has_value());
    }
}

SCENARIO("ISO15118-20 EV energy transfer transitions send the successor's first request") {
    GIVEN("ScheduleExchange finishing an AC session") {
        auto pred = Predecessor::in<state::ScheduleExchange>(ServiceCategory::AC, {});
        const auto sent = pred.transitions_to(StateID::PowerDelivery, schedule_finished());
        const auto req = sent.get<message_20::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::Progress::Start);
        REQUIRE(req->processing == Processing::Finished);
    }

    GIVEN("ScheduleExchange finishing a DC session without CP-state feedback") {
        auto pred = Predecessor::dc<state::ScheduleExchange>();
        const auto sent = pred.transitions_to(StateID::DC_CableCheck, schedule_finished());
        REQUIRE(sent.get<message_20::DC_CableCheckRequest>().has_value());
    }

    GIVEN("ScheduleExchange finishing a DC session with CP already in C") {
        ev::d20::SessionOptions options{};
        options.has_cp_state_feedback = true;
        auto pred = Predecessor::in<state::ScheduleExchange>(ServiceCategory::DC, options);
        pred.ctx.set_cp_state(true);
        const auto sent = pred.transitions_to(StateID::DC_CableCheck, schedule_finished());
        REQUIRE(sent.get<message_20::DC_CableCheckRequest>().has_value());
    }

    GIVEN("DC_PreCharge acknowledging its Finished request") {
        ev::params::DcChargeParams params{};
        params.target_voltage = 400.0f;
        params.max_charge_power = 11000.0f;
        auto pred = Predecessor::dc<state::DC_PreCharge>();
        pred.helper->set_dc_params(params);
        pred.stays_on(message_20::DC_PreChargeResponse{SESSION_HEADER, ResponseCode::OK, dt::from_float(400.0f)});
        const auto sent = pred.transitions_to(
            StateID::PowerDelivery,
            message_20::DC_PreChargeResponse{SESSION_HEADER, ResponseCode::OK, dt::from_float(400.0f)});
        const auto req = sent.get<message_20::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::Progress::Start);
    }
}

SCENARIO("ISO15118-20 EV DC_CableCheck sends DC_PreChargeReq on Finished") {
    ev::params::DcChargeParams params{};
    params.present_voltage = 12.0f;
    params.target_voltage = 400.0f;

    // Driven through ScheduleExchange so the CableCheck state is built the way a session builds it.
    auto pred = Predecessor::dc<state::ScheduleExchange>();
    pred.helper->set_dc_params(params);
    fsm::v2::FSM<ev::d20::StateBase> fsm{pred.ctx.create_state<state::ScheduleExchange>()};
    pred.helper->handle_response(schedule_finished());
    fsm.feed(Event::V2GTP_MESSAGE);
    REQUIRE(fsm.get_current_state_id() == StateID::DC_CableCheck);
    take_all_requests(pred.helper->get_message_exchange());

    pred.helper->handle_response(
        message_20::DC_CableCheckResponse{SESSION_HEADER, ResponseCode::OK, Processing::Finished});
    const auto result = fsm.feed(Event::V2GTP_MESSAGE);
    REQUIRE(result.transitioned());
    REQUIRE(fsm.get_current_state_id() == StateID::DC_PreCharge);
    const auto req = take_all_requests(pred.helper->get_message_exchange()).get<message_20::DC_PreChargeRequest>();
    REQUIRE(req.has_value());
    REQUIRE(req->processing == Processing::Ongoing);
    REQUIRE(dt::from_RationalNumber(req->present_voltage) == 12.0f);
    REQUIRE(dt::from_RationalNumber(req->target_voltage) == 400.0f);
}

SCENARIO("ISO15118-20 EV PowerDelivery(Start) sends the service's first ChargeLoopReq") {
    GIVEN("DC") {
        auto pred = Predecessor::dc<state::PowerDelivery>(state::PowerDelivery::Start{});
        const auto sent = pred.transitions_to(StateID::DC_ChargeLoop, power_delivery_ok());
        REQUIRE(sent.get<message_20::DC_ChargeLoopRequest>().has_value());
    }

    GIVEN("AC") {
        auto pred = Predecessor::in<state::PowerDelivery>(ServiceCategory::AC, {}, state::PowerDelivery::Start{});
        const auto sent = pred.transitions_to(StateID::AC_ChargeLoop, power_delivery_ok());
        REQUIRE(sent.get<message_20::AC_ChargeLoopRequest>().has_value());
    }

    GIVEN("AC_DER_IEC") {
        auto pred =
            Predecessor::in<state::PowerDelivery>(ServiceCategory::AC_DER_IEC, {}, state::PowerDelivery::Start{});
        const auto sent = pred.transitions_to(StateID::AC_DER_IEC_ChargeLoop, power_delivery_ok());
        REQUIRE(sent.get<message_20::DER_AC_ChargeLoopRequest>().has_value());
    }

    GIVEN("AC_DER_SAE") {
        auto pred =
            Predecessor::in<state::PowerDelivery>(ServiceCategory::AC_DER_SAE, {}, state::PowerDelivery::Start{});
        const auto sent = pred.transitions_to(StateID::AC_DER_SAE_ChargeLoop, power_delivery_ok());
        REQUIRE(sent.get<message_20::DER_SAE_AC_ChargeLoopRequest>().has_value());
    }
}

SCENARIO("ISO15118-20 EV stop walk transitions send the successor's first request") {
    GIVEN("a DC charge loop the EVSE terminates") {
        auto pred = Predecessor::dc<state::DC_ChargeLoop>();
        message_20::DC_ChargeLoopResponse res{};
        res.header = SESSION_HEADER;
        res.response_code = ResponseCode::OK;
        res.status = evse_terminate;
        res.control_mode = dt::Dynamic_DC_CLResControlMode{};
        const auto sent = pred.transitions_to(StateID::PowerDelivery, res);
        const auto req = sent.get<message_20::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::Progress::Stop);
    }

    GIVEN("an AC charge loop the EVSE terminates") {
        auto pred = Predecessor::in<state::AC_ChargeLoop>(ServiceCategory::AC, {});
        message_20::AC_ChargeLoopResponse res{};
        res.header = SESSION_HEADER;
        res.response_code = ResponseCode::OK;
        res.status = evse_terminate;
        res.control_mode = dt::Dynamic_AC_CLResControlMode{};
        const auto sent = pred.transitions_to(StateID::PowerDelivery, res);
        const auto req = sent.get<message_20::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::Progress::Stop);
    }

    GIVEN("a DC PowerDelivery(Stop) accepted") {
        auto pred = Predecessor::dc<state::PowerDelivery>(state::PowerDelivery::Stop{ev::session::StopReason::EvStop});
        const auto sent = pred.transitions_to(StateID::DC_WeldingDetection, power_delivery_ok());
        const auto req = sent.get<message_20::DC_WeldingDetectionRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->processing == Processing::Ongoing);
    }

    GIVEN("an AC PowerDelivery(Stop) for an EV pause accepted") {
        auto pred = Predecessor::in<state::PowerDelivery>(ServiceCategory::AC, {},
                                                          state::PowerDelivery::Stop{ev::session::StopReason::EvPause});
        const auto sent = pred.transitions_to(StateID::SessionStop, power_delivery_ok());
        const auto req = sent.get<message_20::SessionStopRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charging_session == dt::ChargingSession::Pause);
    }

    GIVEN("an AC PowerDelivery(Stop) for a pause, with an EV stop latched since") {
        auto pred = Predecessor::in<state::PowerDelivery>(ServiceCategory::AC, {},
                                                          state::PowerDelivery::Stop{ev::session::StopReason::EvPause});
        pred.ctx.set_stop_charging_requested(true);
        const auto sent = pred.transitions_to(StateID::SessionStop, power_delivery_ok());
        const auto req = sent.get<message_20::SessionStopRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charging_session == dt::ChargingSession::Terminate);
    }

    GIVEN("DC welding detection acknowledging its Finished request") {
        auto pred = Predecessor::dc<state::DC_WeldingDetection>(ev::session::StopReason::EvStop);
        const message_20::DC_WeldingDetectionResponse safe{SESSION_HEADER, ResponseCode::OK, dt::RationalNumber{0, 0}};
        pred.stays_on(safe);
        const auto sent = pred.transitions_to(StateID::SessionStop, safe);
        const auto req = sent.get<message_20::SessionStopRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charging_session == dt::ChargingSession::Terminate);
    }

    GIVEN("an EV pause latched before PowerDelivery(Start)") {
        auto pred = Predecessor::dc<state::ServiceDiscovery>();
        pred.ctx.set_pause_charging_requested(true);
        message_20::ServiceDiscoveryResponse res{};
        res.header = SESSION_HEADER;
        res.response_code = ResponseCode::OK;
        res.energy_transfer_service_list = {{ServiceCategory::DC, false}};
        const auto sent = pred.transitions_to(StateID::SessionStop, res);
        const auto req = sent.get<message_20::SessionStopRequest>();
        REQUIRE(req.has_value());
        // [V2G20-2644]: before PowerDeliveryReq(Start) a pause terminates too.
        REQUIRE(req->charging_session == dt::ChargingSession::Terminate);
    }
}

SCENARIO("ISO15118-20 EV DC welding detection failure sends SessionStopReq on its transition") {
    ev::d20::SessionOptions options{};
    options.welding_detection.max_cycles = 1;
    auto pred =
        Predecessor::in<state::DC_WeldingDetection>(ServiceCategory::DC, options, ev::session::StopReason::EvPause);
    const message_20::DC_WeldingDetectionResponse unsafe{SESSION_HEADER, ResponseCode::OK, dt::from_float(400.0f)};
    pred.stays_on(unsafe);
    const auto closing = take_all_requests(pred.helper->get_message_exchange());
    REQUIRE(closing.get<message_20::DC_WeldingDetectionRequest>()->processing == dt::Processing::Finished);
    const auto sent = pred.transitions_to(StateID::SessionStop, unsafe);
    const auto req = sent.get<message_20::SessionStopRequest>();
    REQUIRE(req.has_value());
    REQUIRE(req->charging_session == dt::ChargingSession::Terminate);
}

SCENARIO("ISO15118-20 EV states send nothing on enter") {
    const ev::feedback::Callbacks callbacks{};

    const auto enter_sends_nothing = [&](ServiceCategory service, auto create) {
        FsmStateHelper helper{callbacks, DEFAULT_APP_PROTOCOLS, service};
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        create(ctx)->enter();
        REQUIRE_FALSE(helper.get_message_exchange().has_request());
    };
    const auto dc = ServiceCategory::DC;
    const auto ac = ServiceCategory::AC;

    enter_sends_nothing(dc, [](auto& ctx) { return ctx.template create_state<state::SupportedAppProtocol>(); });
    enter_sends_nothing(dc, [](auto& ctx) { return ctx.template create_state<state::SessionSetup>(); });
    enter_sends_nothing(dc, [](auto& ctx) { return ctx.template create_state<state::AuthorizationSetup>(); });
    enter_sends_nothing(dc, [](auto& ctx) { return ctx.template create_state<state::Authorization>(); });
    enter_sends_nothing(dc, [](auto& ctx) { return ctx.template create_state<state::ServiceDiscovery>(); });
    enter_sends_nothing(dc, [](auto& ctx) { return ctx.template create_state<state::ServiceDetail>(); });
    enter_sends_nothing(dc, [](auto& ctx) { return ctx.template create_state<state::ServiceSelection>(uint16_t{1}); });
    enter_sends_nothing(dc, [](auto& ctx) { return ctx.template create_state<state::DC_ChargeParameterDiscovery>(); });
    enter_sends_nothing(ac, [](auto& ctx) { return ctx.template create_state<state::AC_ChargeParameterDiscovery>(); });
    enter_sends_nothing(ServiceCategory::AC_DER_IEC, [](auto& ctx) {
        return ctx.template create_state<state::AC_DER_IEC_ChargeParameterDiscovery>();
    });
    enter_sends_nothing(ServiceCategory::AC_DER_SAE, [](auto& ctx) {
        return ctx.template create_state<state::AC_DER_SAE_ChargeParameterDiscovery>();
    });
    enter_sends_nothing(dc, [](auto& ctx) { return ctx.template create_state<state::ScheduleExchange>(); });
    enter_sends_nothing(dc, [](auto& ctx) { return ctx.template create_state<state::DC_PreCharge>(); });
    enter_sends_nothing(
        dc, [](auto& ctx) { return ctx.template create_state<state::PowerDelivery>(state::PowerDelivery::Start{}); });
    enter_sends_nothing(dc, [](auto& ctx) { return ctx.template create_state<state::DC_ChargeLoop>(); });
    enter_sends_nothing(ac, [](auto& ctx) { return ctx.template create_state<state::AC_ChargeLoop>(); });
    enter_sends_nothing(ServiceCategory::AC_DER_IEC,
                        [](auto& ctx) { return ctx.template create_state<state::AC_DER_IEC_ChargeLoop>(); });
    enter_sends_nothing(ServiceCategory::AC_DER_SAE, [](auto& ctx) {
        return ctx.template create_state<state::AC_DER_SAE_ChargeLoop>(ev::d20::der::SaeNegotiation{});
    });
    enter_sends_nothing(dc, [](auto& ctx) {
        return ctx.template create_state<state::DC_WeldingDetection>(ev::session::StopReason::EvStop);
    });
    enter_sends_nothing(
        dc, [](auto& ctx) { return ctx.template create_state<state::SessionStop>(ev::session::StopReason::EvStop); });
}
