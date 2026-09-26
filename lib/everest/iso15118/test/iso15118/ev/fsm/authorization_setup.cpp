// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <everest/util/vector/fixed_vector.hpp>
#include <iso15118/ev/d20/state/authorization_setup.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 EV authorization setup state transitions") {

    const ev::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(callbacks);

    auto& ctx = state_helper.get_context();

    GIVEN("Good case - authorization setup response with OK and EIM") {

        // setup the state and context to something reasonable
        const auto header = message_20::Header{{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};

        ctx.get_session().set_id(header.session_id);

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::AuthorizationSetup>()};

        const auto res = message_20::AuthorizationSetupResponse{
            header, message_20::datatypes::ResponseCode::OK, {message_20::datatypes::Authorization::EIM}, false};

        state_helper.handle_response(res);

        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("Check if passes to authorization state and sends EIM AuthorizationRequest") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::Authorization);

            const auto requests = take_all_requests(state_helper.get_message_exchange());
            const auto request_message = requests.get<message_20::AuthorizationRequest>();
            REQUIRE(request_message.has_value());

            const auto& request = request_message.value();
            REQUIRE(request.header.session_id == header.session_id);
            REQUIRE(request.selected_authorization_service == message_20::datatypes::Authorization::EIM);
            REQUIRE(
                std::holds_alternative<message_20::datatypes::EIM_ASReqAuthorizationMode>(request.authorization_mode));
        }
    }

    // The EV selects EIM only, so a PnC-only EVSE leaves it with nothing to select.
    GIVEN("Bad case - authorization setup response with OK and PnC only") {

        // setup the state and context to something reasonable
        const auto header = message_20::Header{{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};

        ctx.get_session().set_id(header.session_id);

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::AuthorizationSetup>()};

        const auto res = message_20::AuthorizationSetupResponse{
            header, message_20::datatypes::ResponseCode::OK, {message_20::datatypes::Authorization::PnC}, false};

        state_helper.handle_response(res);

        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("Check if passes to authorization state, stops the session and sends no AuthorizationRequest") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::Authorization);
            REQUIRE(ctx.is_session_stopped() == true);

            const auto requests = take_all_requests(state_helper.get_message_exchange());
            REQUIRE(requests.empty());
        }
    }

    // An EVSE offering nothing at all is rejected before the Authorization state is created,
    // unlike the PnC-only case above which is rejected on entering it.
    GIVEN("Bad case - authorization setup response with OK and no authorization services") {

        const auto header = message_20::Header{{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};

        ctx.get_session().set_id(header.session_id);

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::AuthorizationSetup>()};

        auto res = message_20::AuthorizationSetupResponse{header, message_20::datatypes::ResponseCode::OK};
        res.authorization_services.clear();

        state_helper.handle_response(res);

        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("Check that the session stops in AuthorizationSetup without sending a request") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::AuthorizationSetup);
            REQUIRE(ctx.is_session_stopped() == true);
            REQUIRE(take_all_requests(state_helper.get_message_exchange()).empty());
        }
    }

    GIVEN("Good case - authorization setup response with OK, PnC and EIM") {

        const auto header = message_20::Header{{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};

        ctx.get_session().set_id(header.session_id);

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::AuthorizationSetup>()};

        const auto res = message_20::AuthorizationSetupResponse{
            header,
            message_20::datatypes::ResponseCode::OK,
            {message_20::datatypes::Authorization::PnC, message_20::datatypes::Authorization::EIM},
            false};

        state_helper.handle_response(res);

        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("Check that both services are recorded and EIM is the one selected") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::Authorization);
            REQUIRE(ctx.is_session_stopped() == false);
            REQUIRE(ctx.get_evse_session_info().auth_services.size() == 2);

            const auto requests = take_all_requests(state_helper.get_message_exchange());
            const auto request_message = requests.get<message_20::AuthorizationRequest>();
            REQUIRE(request_message.has_value());
            REQUIRE(request_message->selected_authorization_service == message_20::datatypes::Authorization::EIM);
            REQUIRE(std::holds_alternative<message_20::datatypes::EIM_ASReqAuthorizationMode>(
                request_message->authorization_mode));
        }
    }

    // The EV cannot use the PnC authorization mode, but it records the provider data so the
    // module can report it, and still authorizes over EIM.
    GIVEN("Good case - authorization setup response with an unsupported PnC authorization mode") {

        const auto header = message_20::Header{{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};

        ctx.get_session().set_id(header.session_id);

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::AuthorizationSetup>()};

        message_20::datatypes::PnC_ASResAuthorizationMode pnc_mode{};
        pnc_mode.gen_challenge = std::array<uint8_t, 16>{0x01, 0x02, 0x03};
        everest::lib::util::fixed_vector<message_20::datatypes::Name, 128> providers{};
        providers.push_back("PIONIX");
        pnc_mode.supported_providers = providers;

        auto res = message_20::AuthorizationSetupResponse{
            header, message_20::datatypes::ResponseCode::OK, {message_20::datatypes::Authorization::EIM}, false};
        res.authorization_mode = pnc_mode;

        state_helper.handle_response(res);

        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("Check that the PnC provider data is recorded and EIM is still selected") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::Authorization);
            REQUIRE(ctx.is_session_stopped() == false);

            const auto& info = ctx.get_evse_session_info();
            REQUIRE(info.gen_challenge == pnc_mode.gen_challenge);
            REQUIRE(info.supported_providers.has_value());
            REQUIRE(info.supported_providers->size() == 1);
            REQUIRE((*info.supported_providers)[0] == "PIONIX");

            const auto requests = take_all_requests(state_helper.get_message_exchange());
            const auto request_message = requests.get<message_20::AuthorizationRequest>();
            REQUIRE(request_message.has_value());
            REQUIRE(request_message->selected_authorization_service == message_20::datatypes::Authorization::EIM);
        }
    }

    // Certificate installation is not implemented in the EV, so the offer is only recorded.
    GIVEN("Good case - authorization setup response offering the certificate installation service") {

        const auto header = message_20::Header{{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};

        ctx.get_session().set_id(header.session_id);

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::AuthorizationSetup>()};

        const auto res = message_20::AuthorizationSetupResponse{
            header, message_20::datatypes::ResponseCode::OK, {message_20::datatypes::Authorization::EIM}, true};

        state_helper.handle_response(res);

        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("Check that the offer is recorded and the session proceeds over EIM") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::Authorization);
            REQUIRE(ctx.is_session_stopped() == false);
            REQUIRE(ctx.get_evse_session_info().certificate_installation_service == true);
            REQUIRE(take_all_requests(state_helper.get_message_exchange())
                        .get<message_20::AuthorizationRequest>()
                        .has_value());
        }
    }

    GIVEN("Bad case - authorization setup response with FAILED and EIM") {

        // setup the state and context to something reasonable
        const auto header = message_20::Header{{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};

        ctx.get_session().set_id(header.session_id);

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::AuthorizationSetup>()};

        const auto res = message_20::AuthorizationSetupResponse{
            header, message_20::datatypes::ResponseCode::FAILED, {message_20::datatypes::Authorization::EIM}, false};

        state_helper.handle_response(res);

        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("Check that the session stops without sending an AuthorizationRequest") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::AuthorizationSetup);
            REQUIRE(ctx.is_session_stopped() == true);
        }
    }
}
