// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "helper.hpp"

#include <iso15118/ev/d20/secc_clock.hpp>
#include <iso15118/ev/d20/state/session_setup.hpp>
#include <iso15118/ev/d20/state/supported_app_protocol.hpp>
#include <iso15118/io/log_levels.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
// SupportedAppProtocol accepting -20: the transition that sends the SessionSetupRequest.
void negotiate(FsmStateHelper& helper, fsm::v2::FSM<ev::d20::StateBase>& fsm) {
    helper.get_message_exchange().set_response(make_sap_ok_response());
    REQUIRE(fsm.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::SessionSetup);
}
} // namespace

SCENARIO("ISO15118-20 EV session setup state transitions") {

    const ev::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(callbacks);

    auto& ctx = state_helper.get_context();

    GIVEN("Good case - new session") {
        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::SessionSetup>()};

        const auto header = message_20::Header{{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};

        const auto res = message_20::SessionSetupResponse{
            header, message_20::datatypes::ResponseCode::OK_NewSessionEstablished, "everest se"};

        state_helper.handle_response(res);
        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("Check if passes to authorization setup state and sends AuthorizationSetupRequest") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::AuthorizationSetup);

            const auto requests = take_all_requests(state_helper.get_message_exchange());
            const auto request_message = requests.get<message_20::AuthorizationSetupRequest>();
            REQUIRE(request_message.has_value());

            const auto& authorization_setup_req = request_message.value();
            REQUIRE(authorization_setup_req.header.session_id == ctx.get_session().get_id());
        }
    }
}

// This EV exists to surface SECC deviations, so SessionSetup is strict: a new session
// is only accepted on OK_NewSessionEstablished. Plain OK and every WARNING_* stop the
// session here even though the generic response-code table accepts them elsewhere.
SCENARIO("ISO15118-20 EV session setup rejects every code but OK_NewSessionEstablished") {

    const ev::feedback::Callbacks callbacks{};

    const auto expect_rejected = [&](message_20::datatypes::ResponseCode code) {
        auto state_helper = FsmStateHelper(callbacks);
        auto& ctx = state_helper.get_context();

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::SessionSetup>()};

        const auto res = message_20::SessionSetupResponse{SESSION_HEADER, code, "everest se"};

        state_helper.handle_response(res);
        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("the session is stopped without adopting the id") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::SessionSetup);
            REQUIRE(ctx.is_session_stopped() == true);
            REQUIRE(ctx.get_session().get_id() == message_20::datatypes::SessionId{});
            REQUIRE(take_all_requests(state_helper.get_message_exchange()).empty());
        }
    };

    GIVEN("a plain OK response carrying a non-zero session id") {
        expect_rejected(message_20::datatypes::ResponseCode::OK);
    }

    GIVEN("an OK_CertificateExpiresSoon response carrying a non-zero session id") {
        expect_rejected(message_20::datatypes::ResponseCode::OK_CertificateExpiresSoon);
    }

    GIVEN("a WARNING_EIMAuthorizationFailure response carrying a non-zero session id") {
        expect_rejected(message_20::datatypes::ResponseCode::WARNING_EIMAuthorizationFailure);
    }

    GIVEN("an OK_NewSessionEstablished response carrying a zero session id") {
        auto state_helper = FsmStateHelper(callbacks);
        auto& ctx = state_helper.get_context();

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::SessionSetup>()};

        const auto zero_header = message_20::Header{std::array<uint8_t, 8>{}, 1691411798};
        const auto res = message_20::SessionSetupResponse{
            zero_header, message_20::datatypes::ResponseCode::OK_NewSessionEstablished, "everest se"};

        state_helper.handle_response(res);
        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("the session is stopped rather than continued with an unusable id") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::SessionSetup);
            REQUIRE(ctx.is_session_stopped() == true);
            REQUIRE(take_all_requests(state_helper.get_message_exchange()).empty());
        }
    }
}

// OK_OldSessionJoined answers a request to resume; to an EV that asked for a new session it
// is a protocol error regardless of the selected service.
SCENARIO("ISO15118-20 EV session setup rejects an unsolicited resumed session") {

    const ev::feedback::Callbacks callbacks{};

    const auto run = [&](message_20::datatypes::ServiceCategory service) {
        auto state_helper = FsmStateHelper(callbacks, DEFAULT_APP_PROTOCOLS, service);
        auto& ctx = state_helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::SessionSetup>()};

        const auto res = message_20::SessionSetupResponse{
            SESSION_HEADER, message_20::datatypes::ResponseCode::OK_OldSessionJoined, "everest se"};

        state_helper.handle_response(res);
        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        REQUIRE(result.transitioned() == false);
        REQUIRE(result.output == ev::d20::Disposition::Stopping);
        REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::SessionSetup);
        REQUIRE(ctx.is_session_stopped() == true);
        REQUIRE(take_all_requests(state_helper.get_message_exchange()).empty());
    };

    GIVEN("an AC selected service") {
        run(message_20::datatypes::ServiceCategory::AC);
    }

    GIVEN("a DC selected service") {
        run(message_20::datatypes::ServiceCategory::DC);
    }
}

SCENARIO("ISO15118-20 EV request headers carry SECC time in microseconds [V2G20-1534]") {

    const ev::feedback::Callbacks callbacks{};

    const auto first_header_after_setup = [&](std::uint64_t reference) {
        auto state_helper = FsmStateHelper(callbacks);
        auto& ctx = state_helper.get_context();

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::SupportedAppProtocol>()};
        const auto before = ev::d20::SeccClock::system_now_us();
        negotiate(state_helper, fsm);
        const auto after = ev::d20::SeccClock::system_now_us();

        const auto setup_req =
            take_all_requests(state_helper.get_message_exchange()).get<message_20::SessionSetupRequest>();
        REQUIRE(setup_req.has_value());
        // [V2G20-1535]: the local UTC guess.
        REQUIRE(setup_req->header.timestamp >= before);
        REQUIRE(setup_req->header.timestamp <= after);

        const auto received = std::chrono::steady_clock::now();
        state_helper.handle_response(message_20::SessionSetupResponse{
            message_20::Header{SESSION_HEADER.session_id, reference},
            message_20::datatypes::ResponseCode::OK_NewSessionEstablished, "everest se"});
        REQUIRE(fsm.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());

        const auto auth_setup_req =
            take_all_requests(state_helper.get_message_exchange()).get<message_20::AuthorizationSetupRequest>();
        REQUIRE(auth_setup_req.has_value());
        REQUIRE(auth_setup_req->header.session_id == SESSION_HEADER.session_id);
        // [V2G20-1536]
        require_tracks_reference(auth_setup_req->header.timestamp, reference, received);
        REQUIRE(auth_setup_req->header.timestamp > reference);
    };

    GIVEN("a new session whose SECC time is far below local UTC") {
        first_header_after_setup(SECC_REFERENCE_US);
    }

    GIVEN("a new session whose SECC time is far above local UTC") {
        first_header_after_setup(ev::d20::SeccClock::system_now_us() * 2);
    }
}

SCENARIO("ISO15118-20 EV request headers keep increasing across a resume [V2G20-1537]") {

    const ev::feedback::Callbacks callbacks{};

    // The paused session, synchronized to an SECC epoch at power-on.
    auto paused = FsmStateHelper(callbacks);
    paused.get_context().secc_clock().synchronize(SECC_REFERENCE_US);
    paused.get_context().send_request(message_20::AuthorizationSetupRequest{});
    const auto last = take_all_requests(paused.get_message_exchange()).get<message_20::AuthorizationSetupRequest>();
    REQUIRE(last.has_value());

    ev::d20::SessionOptions options{};
    options.resume = ev::d20::Resume{SESSION_HEADER.session_id, paused.get_context().secc_clock().state(),
                                     ev::d20::SelectedService{message_20::datatypes::ServiceCategory::DC,
                                                              message_20::datatypes::ControlMode::Dynamic}};
    auto state_helper = FsmStateHelper(callbacks, DEFAULT_APP_PROTOCOLS, message_20::datatypes::ServiceCategory::DC,
                                       std::move(options));
    auto& ctx = state_helper.get_context();

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::SupportedAppProtocol>()};
    negotiate(state_helper, fsm);
    const auto setup_req =
        take_all_requests(state_helper.get_message_exchange()).get<message_20::SessionSetupRequest>();
    REQUIRE(setup_req.has_value());
    // Already in SECC time, not the local UTC guess.
    REQUIRE(setup_req->header.timestamp > last->header.timestamp);
    REQUIRE(setup_req->header.timestamp < ev::d20::SeccClock::system_now_us() / 2);

    // An SECC reference no later than the stamps already sent: they still do not go back.
    state_helper.handle_response(
        message_20::SessionSetupResponse{message_20::Header{SESSION_HEADER.session_id, SECC_REFERENCE_US},
                                         message_20::datatypes::ResponseCode::OK_OldSessionJoined, "everest se"});
    REQUIRE(fsm.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());

    const auto cpd_req =
        take_all_requests(state_helper.get_message_exchange()).get<message_20::DC_ChargeParameterDiscoveryRequest>();
    REQUIRE(cpd_req.has_value());
    REQUIRE(cpd_req->header.timestamp > setup_req->header.timestamp);
}

namespace {

using message_20::datatypes::AcConnector;
using message_20::datatypes::ControlMode;
using message_20::datatypes::Processing;
using message_20::datatypes::ResponseCode;
using message_20::datatypes::ServiceCategory;

// An EV that asked to resume SESSION_HEADER's session with `retained` and was constructed for
// `configured`, entered at SessionSetup with the request SessionSetup answers already sent.
struct Rejoin {
    Rejoin(ServiceCategory configured, ev::d20::SelectedService retained) :
        helper(ev::feedback::Callbacks{}, DEFAULT_APP_PROTOCOLS, configured, options_for(retained)),
        ctx(helper.get_context()),
        fsm(enter_session_setup()) {
    }

    // Answers the pending request and returns the request the answer sent.
    template <typename Request, typename Response> Request answer(const Response& response) {
        helper.handle_response(response);
        fsm.feed(ev::d20::Event::V2GTP_MESSAGE);
        REQUIRE_FALSE(ctx.is_session_stopped());
        const auto request = take_all_requests(helper.get_message_exchange()).template get<Request>();
        REQUIRE(request.has_value());
        REQUIRE(request->header.session_id == SESSION_HEADER.session_id);
        return *request;
    }

    template <typename Request> Request joined() {
        return answer<Request>(
            message_20::SessionSetupResponse{SESSION_HEADER, ResponseCode::OK_OldSessionJoined, "everest se"});
    }

    FsmStateHelper helper;
    ev::d20::Context& ctx;
    fsm::v2::FSM<ev::d20::StateBase> fsm;

private:
    static ev::d20::SessionOptions options_for(const ev::d20::SelectedService& retained) {
        ev::d20::SessionOptions options{};
        options.resume = ev::d20::Resume{SESSION_HEADER.session_id, {}, retained};
        return options;
    }

    ev::d20::BasePointerType enter_session_setup() {
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        // A single-line EV: on a ThreePhase connector its limits carry L2 and L3, on SinglePhase they do not.
        auto ac = ev::params::AcChargeParams{};
        ac.phase_count = 1;
        ac.max_charge_power = 7000.0f;
        helper.set_ac_params(ac);
        return ctx.create_state<ev::d20::state::SessionSetup>();
    }
};

} // namespace

SCENARIO("ISO15118-20 EV continues a re-joined session at the retained service's ChargeParameterDiscovery") {

    GIVEN("a paused DC session, resumed by an EV configured for AC") {
        Rejoin rejoin{ServiceCategory::AC, {ServiceCategory::DC, ControlMode::Dynamic}};
        const auto req = rejoin.joined<message_20::DC_ChargeParameterDiscoveryRequest>();

        REQUIRE(std::holds_alternative<message_20::datatypes::DC_CPDReqEnergyTransferMode>(req.transfer_mode));
        REQUIRE(rejoin.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeParameterDiscovery);
        REQUIRE(rejoin.ctx.selected_service() == ServiceCategory::DC);
    }

    GIVEN("a paused DC_BPT session") {
        Rejoin rejoin{ServiceCategory::DC, {ServiceCategory::DC_BPT, ControlMode::Dynamic}};
        const auto req = rejoin.joined<message_20::DC_ChargeParameterDiscoveryRequest>();

        REQUIRE(std::holds_alternative<message_20::datatypes::BPT_DC_CPDReqEnergyTransferMode>(req.transfer_mode));
    }

    GIVEN("a paused MCS session, resumed by an EV configured for DC") {
        Rejoin rejoin{ServiceCategory::DC, {ServiceCategory::MCS, ControlMode::Dynamic}};
        const auto req = rejoin.joined<message_20::DC_ChargeParameterDiscoveryRequest>();

        REQUIRE(std::holds_alternative<message_20::datatypes::DC_CPDReqEnergyTransferMode>(req.transfer_mode));
        REQUIRE(rejoin.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeParameterDiscovery);
        REQUIRE(rejoin.ctx.selected_service() == ServiceCategory::MCS);
    }

    GIVEN("a paused MCS_BPT session, resumed by an EV configured for DC") {
        Rejoin rejoin{ServiceCategory::DC, {ServiceCategory::MCS_BPT, ControlMode::Dynamic}};
        const auto req = rejoin.joined<message_20::DC_ChargeParameterDiscoveryRequest>();

        REQUIRE(std::holds_alternative<message_20::datatypes::BPT_DC_CPDReqEnergyTransferMode>(req.transfer_mode));
        REQUIRE(rejoin.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeParameterDiscovery);
        REQUIRE(rejoin.ctx.selected_service() == ServiceCategory::MCS_BPT);
    }

    GIVEN("a paused AC_BPT session on a ThreePhase connector, resumed by an EV configured for AC") {
        Rejoin rejoin{ServiceCategory::AC, {ServiceCategory::AC_BPT, ControlMode::Dynamic, AcConnector::ThreePhase}};
        const auto req = rejoin.joined<message_20::AC_ChargeParameterDiscoveryRequest>();

        const auto* mode = std::get_if<message_20::datatypes::BPT_AC_CPDReqEnergyTransferMode>(&req.transfer_mode);
        REQUIRE(mode != nullptr);
        REQUIRE(mode->max_charge_power_L2.has_value());
        REQUIRE(rejoin.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeParameterDiscovery);
        REQUIRE(rejoin.ctx.selected_service() == ServiceCategory::AC_BPT);
    }

    GIVEN("a paused AC session on a ThreePhase connector, resumed by an EV configured for DC") {
        Rejoin rejoin{ServiceCategory::DC, {ServiceCategory::AC, ControlMode::Dynamic, AcConnector::ThreePhase}};
        const auto req = rejoin.joined<message_20::AC_ChargeParameterDiscoveryRequest>();

        const auto* mode = std::get_if<message_20::datatypes::AC_CPDReqEnergyTransferMode>(&req.transfer_mode);
        REQUIRE(mode != nullptr);
        REQUIRE(mode->max_charge_power_L2.has_value());
        REQUIRE(rejoin.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeParameterDiscovery);
    }

    GIVEN("a paused AC_DER_IEC session on a ThreePhase connector") {
        Rejoin rejoin{ServiceCategory::DC,
                      {ServiceCategory::AC_DER_IEC, ControlMode::Dynamic, AcConnector::ThreePhase}};
        const auto req = rejoin.joined<message_20::DER_AC_ChargeParameterDiscoveryRequest>();

        REQUIRE(req.transfer_mode.max_charge_power_L2.has_value());
        REQUIRE(rejoin.fsm.get_current_state_id() == ev::d20::StateID::AC_DER_IEC_ChargeParameterDiscovery);
    }

    GIVEN("a paused AC_DER_SAE session on a ThreePhase connector") {
        Rejoin rejoin{ServiceCategory::DC,
                      {ServiceCategory::AC_DER_SAE, ControlMode::Dynamic, AcConnector::ThreePhase}};
        const auto req = rejoin.joined<message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest>();

        REQUIRE(req.transfer_mode.max_charge_power_L2.has_value());
        REQUIRE(rejoin.fsm.get_current_state_id() == ev::d20::StateID::AC_DER_SAE_ChargeParameterDiscovery);
    }
}

SCENARIO("ISO15118-20 EV warns when a re-joined session continues another service than configured") {
    GIVEN("a paused AC session resumed by an EV configured for AC_DER_IEC") {
        const LogCapture logs{};
        Rejoin rejoin{ServiceCategory::AC_DER_IEC, {ServiceCategory::AC, ControlMode::Dynamic}};
        rejoin.joined<message_20::AC_ChargeParameterDiscoveryRequest>();

        THEN("one warning names the retained AC (1) and the configured AC_DER_IEC (10) service") {
            const auto warnings = logs.messages(LogLevel::Warning);
            REQUIRE(warnings.size() == 1);
            REQUIRE(warnings[0].find("retained service 1 ") != std::string::npos);
            REQUIRE(warnings[0].find("configured service 10") != std::string::npos);
        }
    }

    GIVEN("a paused AC session resumed by an EV configured for AC") {
        const LogCapture logs{};
        Rejoin rejoin{ServiceCategory::AC, {ServiceCategory::AC, ControlMode::Scheduled}};
        rejoin.joined<message_20::AC_ChargeParameterDiscoveryRequest>();

        THEN("nothing is warned") {
            REQUIRE(logs.messages(LogLevel::Warning).empty());
        }
    }
}

SCENARIO("ISO15118-20 EV charges a re-joined AC session in its retained Scheduled mode and connector") {
    // The EV prefers Dynamic; only the retained selection says Scheduled and ThreePhase.
    Rejoin rejoin{ServiceCategory::AC, {ServiceCategory::AC, ControlMode::Scheduled, AcConnector::ThreePhase}};
    rejoin.joined<message_20::AC_ChargeParameterDiscoveryRequest>();

    auto cpd_res = message_20::AC_ChargeParameterDiscoveryResponse{};
    cpd_res.header = SESSION_HEADER;
    cpd_res.response_code = ResponseCode::OK;
    cpd_res.transfer_mode = message_20::datatypes::AC_CPDResEnergyTransferMode{};
    const auto schedule_req = rejoin.answer<message_20::ScheduleExchangeRequest>(cpd_res);
    REQUIRE(std::holds_alternative<message_20::datatypes::Scheduled_SEReqControlMode>(schedule_req.control_mode));

    auto schedule_res =
        message_20::ScheduleExchangeResponse{SESSION_HEADER, ResponseCode::OK, Processing::Finished, std::nullopt};
    message_20::datatypes::Scheduled_SEResControlMode scheduled{};
    scheduled.schedule_tuple.push_back(message_20::datatypes::ScheduleTuple{});
    scheduled.schedule_tuple.back().schedule_tuple_id = 7;
    schedule_res.control_mode = scheduled;
    const auto start_req = rejoin.answer<message_20::PowerDeliveryRequest>(schedule_res);
    REQUIRE(start_req.charge_progress == message_20::datatypes::Progress::Start);
    REQUIRE(start_req.power_profile.has_value());
    const auto* profile_mode =
        std::get_if<message_20::datatypes::Scheduled_EVPPTControlMode>(&start_req.power_profile->control_mode);
    REQUIRE(profile_mode != nullptr);
    REQUIRE(profile_mode->selected_schedule == 7);
    REQUIRE(start_req.power_profile->entries.size() == 1);
    REQUIRE(start_req.power_profile->entries[0].power_l2.has_value());

    const auto loop_req = rejoin.answer<message_20::AC_ChargeLoopRequest>(
        message_20::PowerDeliveryResponse{SESSION_HEADER, ResponseCode::OK, std::nullopt});
    REQUIRE(std::holds_alternative<message_20::datatypes::Scheduled_AC_CLReqControlMode>(loop_req.control_mode));

    auto loop_res = message_20::AC_ChargeLoopResponse{};
    loop_res.header = SESSION_HEADER;
    loop_res.response_code = ResponseCode::OK;
    loop_res.control_mode = message_20::datatypes::Scheduled_AC_CLResControlMode{};
    const auto next_loop_req = rejoin.answer<message_20::AC_ChargeLoopRequest>(loop_res);
    REQUIRE(std::holds_alternative<message_20::datatypes::Scheduled_AC_CLReqControlMode>(next_loop_req.control_mode));
    REQUIRE(rejoin.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);
}

SCENARIO("ISO15118-20 EV charges a re-joined DC session through cable check and pre-charge") {
    Rejoin rejoin{ServiceCategory::DC, {ServiceCategory::DC, ControlMode::Dynamic}};
    rejoin.joined<message_20::DC_ChargeParameterDiscoveryRequest>();

    const auto schedule_req = rejoin.answer<message_20::ScheduleExchangeRequest>(
        message_20::DC_ChargeParameterDiscoveryResponse{SESSION_HEADER, ResponseCode::OK});
    REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_SEReqControlMode>(schedule_req.control_mode));

    rejoin.answer<message_20::DC_CableCheckRequest>(
        message_20::ScheduleExchangeResponse{SESSION_HEADER, ResponseCode::OK, Processing::Finished, std::nullopt});
    rejoin.answer<message_20::DC_PreChargeRequest>(
        message_20::DC_CableCheckResponse{SESSION_HEADER, ResponseCode::OK, Processing::Finished});
    // The default target voltage is 0 V, so a 0 V present voltage is in tolerance.
    const auto finished_req = rejoin.answer<message_20::DC_PreChargeRequest>(
        message_20::DC_PreChargeResponse{SESSION_HEADER, ResponseCode::OK, {0, 0}});
    REQUIRE(finished_req.processing == Processing::Finished);

    const auto start_req = rejoin.answer<message_20::PowerDeliveryRequest>(
        message_20::DC_PreChargeResponse{SESSION_HEADER, ResponseCode::OK, {0, 0}});
    REQUIRE(start_req.charge_progress == message_20::datatypes::Progress::Start);
    REQUIRE(start_req.power_profile.has_value());
    REQUIRE(
        std::holds_alternative<message_20::datatypes::Dynamic_EVPPTControlMode>(start_req.power_profile->control_mode));

    const auto loop_req = rejoin.answer<message_20::DC_ChargeLoopRequest>(
        message_20::PowerDeliveryResponse{SESSION_HEADER, ResponseCode::OK, std::nullopt});
    REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_DC_CLReqControlMode>(loop_req.control_mode));

    auto loop_res = message_20::DC_ChargeLoopResponse{};
    loop_res.header = SESSION_HEADER;
    loop_res.response_code = ResponseCode::OK;
    loop_res.control_mode = message_20::datatypes::Dynamic_DC_CLResControlMode{};
    const auto next_loop_req = rejoin.answer<message_20::DC_ChargeLoopRequest>(loop_res);
    REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_DC_CLReqControlMode>(next_loop_req.control_mode));
    REQUIRE(rejoin.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
}

SCENARIO("ISO15118-20 EV starts afresh when its resume is answered with a new session") {
    // A charger whose pause context is gone answers the retained id with a new one.
    Rejoin rejoin{ServiceCategory::DC, {ServiceCategory::AC, ControlMode::Scheduled, AcConnector::ThreePhase}};
    const auto new_header = message_20::Header{{0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11}, 1691411798};

    rejoin.helper.handle_response(
        message_20::SessionSetupResponse{new_header, ResponseCode::OK_NewSessionEstablished, "everest se"});
    REQUIRE(rejoin.fsm.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());

    const auto req =
        take_all_requests(rejoin.helper.get_message_exchange()).get<message_20::AuthorizationSetupRequest>();
    REQUIRE(req.has_value());
    REQUIRE(req->header.session_id == new_header.session_id);
    REQUIRE(rejoin.fsm.get_current_state_id() == ev::d20::StateID::AuthorizationSetup);
    REQUIRE(rejoin.ctx.selected_service() == ServiceCategory::DC);
    REQUIRE_FALSE(rejoin.ctx.service_selection().has_value());
}

SCENARIO("ISO15118-20 EV stops a re-joined session that paused before selecting a service") {
    ev::d20::SessionOptions options{};
    options.resume = ev::d20::Resume{SESSION_HEADER.session_id};
    FsmStateHelper helper{ev::feedback::Callbacks{}, DEFAULT_APP_PROTOCOLS, ServiceCategory::DC, std::move(options)};
    helper.get_context().get_session().set_id(SESSION_HEADER.session_id);
    fsm::v2::FSM<ev::d20::StateBase> fsm{helper.get_context().create_state<ev::d20::state::SessionSetup>()};

    expect_stops_session(
        helper, fsm, message_20::SessionSetupResponse{SESSION_HEADER, ResponseCode::OK_OldSessionJoined, "everest se"},
        ev::d20::StateID::SessionSetup);
    REQUIRE(take_all_requests(helper.get_message_exchange()).empty());
}

TEST_CASE("ISO15118-20 EV SessionSetup reports a decode failure once") {
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks};
    auto& ctx = helper.get_context();
    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::SessionSetup>()};
    const LogCapture logs{};
    std::unique_ptr<message_20::ResponseVariant> response;
    std::string error;
    SECTION("undecodable response") {
        const std::array<uint8_t, 4> bytes{0xff, 0xff, 0xff, 0xff};
        response = std::make_unique<message_20::ResponseVariant>(v2gtp::PayloadType::Part20Main,
                                                                 io::StreamInputView{bytes.data(), bytes.size()});
        error = response->get_error();
    }
    SECTION("wrong response type still logs a mismatch") {
        response = std::make_unique<message_20::ResponseVariant>(message_20::PowerDeliveryResponse{});
        error = "Unexpected ISO 15118-20 response, got message type id:";
    }
    helper.get_message_exchange().set_response(std::move(response));
    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);
    const auto errors = logs.messages(LogLevel::Error);
    REQUIRE_FALSE(result.transitioned());
    REQUIRE(ctx.is_session_stopped());
    REQUIRE_FALSE(error.empty());
    REQUIRE(errors.size() == 1);
    REQUIRE(errors.front().find(error) != std::string::npos);
}
