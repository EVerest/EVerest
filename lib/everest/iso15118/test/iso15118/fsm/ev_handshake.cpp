// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
//
// Scripted FSM-level test driving the EVCC handshake state machine from SessionSetup through
// ServiceSelection. The SupportedAppProtocol phase (now run by the session driver, not the FSM) is
// covered here at function level. The canned SECC responses are produced by constructing the SECC
// response structs and serializing them via message_20::serialize, then decoding them back through
// message_20::Variant, i.e. they go through the exact same EXI codec that is used on the wire.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <optional>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/d20/ev/context.hpp>
#include <iso15118/d20/ev/state/session_setup.hpp>
#include <iso15118/d20/ev/states.hpp>
#include <iso15118/d20/timeout.hpp>

#include <iso15118/detail/d20/ev/state/supported_app_protocol.hpp>
#include <iso15118/session/protocol.hpp>

#include <iso15118/io/logging.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/payload_type.hpp>
#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/service_selection.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
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

    // Decode the outgoing request the current state produced (SEND_REQUEST).
    template <typename Req> std::optional<Req> take_request() {
        return msg_exch.get_response<Req>();
    }

    // Inject a canned SECC response: serialize the struct and decode it back through the codec.
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

session::EvSessionConfig make_dc_config() {
    session::EvSetupConfig setup;
    setup.evcc_id = "WMIV1234567890ABCDEX";
    setup.supported_energy_services = {dt::ServiceCategory::DC};
    setup.preferred_control_mode = dt::ControlMode::Dynamic;
    setup.supported_auth_options = {dt::Authorization::EIM};
    return session::EvSessionConfig(setup);
}

message_20::Header make_header() {
    message_20::Header header;
    header.session_id = ASSIGNED_SESSION_ID;
    header.timestamp = 1691411798;
    return header;
}

} // namespace

SCENARIO("EVCC handshake FSM drives SessionSetup through ServiceSelection against canned SECC responses") {

    session::ev::feedback::Callbacks callbacks{};
    std::string evse_id_seen;
    callbacks.evse_id = [&](const std::string& id) { evse_id_seen = id; };

    EvFsmHelper helper(make_dc_config(), callbacks);
    auto& ctx = helper.get_context();

    // --- SupportedAppProtocol (run by the session driver; verified here at function level) ---
    {
        const auto req = d20::ev::state::supported_app_protocol::create_request(
            {ProtocolId::ISO15118_20}, {dt::ServiceCategory::DC}, std::nullopt);
        REQUIRE(req.app_protocol.size() == 1);
        REQUIRE(req.app_protocol[0].protocol_namespace == "urn:iso:std:iso:15118:-20:DC");
        REQUIRE(protocol_id_from_namespace(req.app_protocol[0].protocol_namespace) == ProtocolId::ISO15118_20);

        message_20::SupportedAppProtocolResponse res;
        res.response_code = message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation;
        res.schema_id = 1;
        const auto result = d20::ev::state::supported_app_protocol::handle_response(res);
        REQUIRE(result.negotiated);
        REQUIRE(result.schema_id.has_value());
        REQUIRE(result.schema_id.value() == 1);
    }

    fsm::v2::FSM<d20::ev::StateBase> fsm{ctx.create_state<d20::ev::state::SessionSetup>()};

    // --- SessionSetup ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::SessionSetupRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->evccid == "WMIV1234567890ABCDEX");
        REQUIRE(req->header.session_id == dt::SessionId{0, 0, 0, 0, 0, 0, 0, 0});
    }
    {
        message_20::SessionSetupResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK_NewSessionEstablished;
        res.evseid = "everest se";
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::AuthorizationSetup);
    }
    REQUIRE(evse_id_seen == "everest se");
    REQUIRE(ctx.get_session_id() == ASSIGNED_SESSION_ID);

    // --- AuthorizationSetup ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_20::AuthorizationSetupRequest>().has_value());
    {
        message_20::AuthorizationSetupResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.authorization_services = {dt::Authorization::EIM};
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::Authorization);
    }

    // --- Authorization (ongoing then finished) ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::AuthorizationRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->selected_authorization_service == dt::Authorization::EIM);
    }
    {
        // First an Ongoing response: the EV must resend and stay in Authorization.
        message_20::AuthorizationResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::Processing::Ongoing;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(not result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::Authorization);
        // the resent request is in the outgoing slot
        REQUIRE(helper.take_request<message_20::AuthorizationRequest>().has_value());
    }
    {
        // Then Finished: transition to ServiceDiscovery.
        message_20::AuthorizationResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::Processing::Finished;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ServiceDiscovery);
    }

    // --- ServiceDiscovery ---
    fsm.feed(Event::SEND_REQUEST);
    REQUIRE(helper.take_request<message_20::ServiceDiscoveryRequest>().has_value());
    {
        message_20::ServiceDiscoveryResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.energy_transfer_service_list.clear();
        res.energy_transfer_service_list.push_back({dt::ServiceCategory::DC, false});
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ServiceDetail);
    }
    REQUIRE(ctx.evse_info.selected_energy_service == dt::ServiceCategory::DC);

    // --- ServiceDetail ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::ServiceDetailRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->service == message_20::to_underlying_value(dt::ServiceCategory::DC));
    }
    {
        message_20::ServiceDetailResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        res.service = message_20::to_underlying_value(dt::ServiceCategory::DC);
        res.service_parameter_list.clear();
        const dt::DcParameterList list{dt::DcConnector::Extended, dt::ControlMode::Dynamic,
                                       dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing};
        res.service_parameter_list.push_back(dt::ParameterSet(5, list));
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == StateID::ServiceSelection);
    }
    REQUIRE(ctx.evse_info.selected_parameter_set_id == 5);
    REQUIRE(ctx.evse_info.selected_control_mode == dt::ControlMode::Dynamic);

    // --- ServiceSelection ---
    fsm.feed(Event::SEND_REQUEST);
    {
        const auto req = helper.take_request<message_20::ServiceSelectionRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->selected_energy_transfer_service.service_id == dt::ServiceCategory::DC);
        REQUIRE(req->selected_energy_transfer_service.parameter_set_id == 5);
    }
    {
        message_20::ServiceSelectionResponse res;
        res.header = make_header();
        res.response_code = dt::ResponseCode::OK;
        helper.inject_response(res);
        const auto result = fsm.feed(Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned());
        // DC branch: transitions to the (stub) DC_ChargeParameterDiscovery
        REQUIRE(fsm.get_current_state_id() == StateID::DC_ChargeParameterDiscovery);
    }
}
