// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d2/state/authorization.hpp>
#include <iso15118/d2/state/payment_service_selection.hpp>

#include <iso15118/message/d2/authorization.hpp>
#include <optional>
#include <vector>

// TODO(kd): Change this after files are moved to a common place
#include <iso15118/d20/control_event.hpp>
typedef ::iso15118::d20::AuthorizationResponse AuthorizationResponse;

using namespace iso15118;

SCENARIO("ISO15118-2 authorization state transitions") {

    namespace dt = d2::msg::data_types;

    // Move to helper function?
    const auto evse_id = std::string("everest se");

    const std::vector<dt::EnergyTransferMode> supported_energy_transfer_modes = {dt::EnergyTransferMode::DcExtended};
    const std::vector<dt::PaymentOption> supported_payment_options = {dt::PaymentOption::ExternalPayment};
    const std::vector<dt::ServiceID> offered_services{};
    const d2::EvseSetupConfig evse_setup{evse_id, supported_energy_transfer_modes, supported_payment_options,
                                         offered_services};

    session::feedback::Callbacks feedback_callbacks;
    auto state_helper = FsmStateHelper(d2::SessionConfig(evse_setup), feedback_callbacks);
    auto ctx = state_helper.get_context();

    GIVEN("Good case - AuthorizationReq EIM - ongoing") {
        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::Authorization>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        const auto req = d2::msg::AuthorizationRequest{header_req, std::nullopt, std::nullopt};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == false);

            const auto response_message = ctx.get_response<d2::msg::AuthorizationResponse>();
            REQUIRE(response_message.has_value());

            const auto authorization_res = response_message.value();
            REQUIRE(authorization_res.response_code == dt::ResponseCode::OK);
            REQUIRE(authorization_res.evse_processing == dt::EvseProcessing::Ongoing);
        }
    }

    GIVEN("Good case - AuthorizationReq EIM - authorized") {
        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::Authorization>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        const auto req = d2::msg::AuthorizationRequest{header_req, std::nullopt, std::nullopt};

        state_helper.set_current_control_event(AuthorizationResponse(true));
        fsm.feed(d2::Event::CONTROL_MESSAGE);
        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d2::StateID::ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<d2::msg::AuthorizationResponse>();
            REQUIRE(response_message.has_value());

            const auto authorization_res = response_message.value();
            REQUIRE(authorization_res.response_code == dt::ResponseCode::OK);
            REQUIRE(authorization_res.evse_processing == dt::EvseProcessing::Finished);
        }
    }

    GIVEN("Good case - AuthorizationReq EIM - not authorized") {
        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::Authorization>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        const auto req = d2::msg::AuthorizationRequest{header_req, std::nullopt, std::nullopt};

        state_helper.set_current_control_event(AuthorizationResponse(false));
        fsm.feed(d2::Event::CONTROL_MESSAGE);
        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == false);

            const auto response_message = ctx.get_response<d2::msg::AuthorizationResponse>();
            REQUIRE(response_message.has_value());

            const auto authorization_res = response_message.value();
            REQUIRE(authorization_res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(authorization_res.evse_processing == dt::EvseProcessing::Finished);
        }
    }

    GIVEN("Good case - AuthorizationReq EIM - ongoing timeout") {
        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::Authorization>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        const auto req = d2::msg::AuthorizationRequest{header_req, std::nullopt, std::nullopt};

        state_helper.handle_request(req);
        fsm.feed(d2::Event::V2GTP_MESSAGE);
        ctx.set_active_timeout(TimeoutType::ONGOING);
        fsm.feed(d2::Event::TIMEOUT);
        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == false);

            const auto response_message = ctx.get_response<d2::msg::AuthorizationResponse>();
            REQUIRE(response_message.has_value());

            const auto authorization_res = response_message.value();
            REQUIRE(authorization_res.response_code == dt::ResponseCode::FAILED);
        }
    }

    GIVEN("Good case - AuthorizationReq - session stop") {
        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::Authorization>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        const auto req = d2::msg::AuthorizationRequest{header_req, std::nullopt, std::nullopt};

        state_helper.set_current_control_event(AuthorizationResponse(false));
        fsm.feed(d2::Event::CONTROL_MESSAGE);
        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == false);

            const auto response_message = ctx.get_response<d2::msg::AuthorizationResponse>();
            REQUIRE(response_message.has_value());

            const auto authorization_res = response_message.value();
            REQUIRE(authorization_res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(authorization_res.evse_processing == dt::EvseProcessing::Finished);
        }
    }
}
