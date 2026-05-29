// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d2/state/authorization.hpp>
#include <iso15118/d2/state/payment_service_selection.hpp>

#include <iso15118/message/d2/payment_service_selection.hpp>
#include <iso15118/message/d2/service_detail.hpp>
#include <optional>
#include <vector>

using namespace iso15118;

SCENARIO("ISO15118-2 payment service selection state transitions") {

    namespace dt = d2::msg::data_types;

    // Move to helper function?
    const auto evse_id = std::string("everest se");

    const std::vector<dt::EnergyTransferMode> supported_energy_transfer_modes = {dt::EnergyTransferMode::DcExtended};
    const std::vector<dt::PaymentOption> supported_payment_options = {dt::PaymentOption::ExternalPayment};
    const std::vector<dt::ServiceID> offered_services{};
    const d2::EvseSetupConfig evse_setup{evse_id, supported_energy_transfer_modes, supported_payment_options,
                                         offered_services};

    const auto custom_service =
        dt::Service{10, "Custom", dt::ServiceCategory::OtherCustom, "https://example.com", true};
    auto custom_service_parameter = dt::Parameter{};
    custom_service_parameter.name = "Foo";
    custom_service_parameter.value.emplace<int>(100);
    const dt::ParameterSet custom_service_parameter_set = {1, {custom_service_parameter}};

    session::feedback::Callbacks feedback_callbacks;
    feedback_callbacks.get_service_from_id = [&](dt::ServiceID id) -> std::optional<dt::Service> {
        if (id == custom_service.service_id) {
            return custom_service;
        } else {
            return std::nullopt;
        }
    };
    feedback_callbacks.get_service_parameters_list = [&](dt::ServiceID id) -> std::optional<dt::ServiceParameterList> {
        if (id == custom_service.service_id) {
            return dt::ServiceParameterList{custom_service_parameter_set};
        } else {
            return std::nullopt;
        }
    };

    auto state_helper = FsmStateHelper(d2::SessionConfig(evse_setup), feedback_callbacks);
    auto ctx = state_helper.get_context();

    GIVEN("Good case - PaymentServiceSelectionReq respond properly") {
        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::PaymentServiceSelection>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        const auto selected_service = dt::SelectedService{10, custom_service_parameter_set.parameter_set_id};
        const auto req =
            d2::msg::PaymentServiceSelectionRequest{header_req, dt::PaymentOption::ExternalPayment, {selected_service}};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d2::StateID::Authorization);
            REQUIRE(ctx.session.selected_payment_option == dt::PaymentOption::ExternalPayment);
            const auto selected_services = ctx.session.get_selected_services();
            REQUIRE(selected_services.size() == 1);
            REQUIRE(selected_services[0].service_id == 10);
            REQUIRE(selected_services[0].parameter_set_id == custom_service_parameter_set.parameter_set_id);

            const auto response_message = ctx.get_response<d2::msg::PaymentServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto payment_service_selection_res = response_message.value();
            REQUIRE(payment_service_selection_res.response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("Bad case - PaymentServiceSelectionReq unsupported payment option") {
        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::PaymentServiceSelection>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        const auto req = d2::msg::PaymentServiceSelectionRequest{header_req, dt::PaymentOption::Contract, {}};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(not ctx.session.selected_payment_option.has_value());

            const auto response_message = ctx.get_response<d2::msg::PaymentServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto payment_service_selection_res = response_message.value();
            REQUIRE(payment_service_selection_res.response_code == dt::ResponseCode::FAILED);
        }
    }

    GIVEN("Bad case - PaymentServiceSelectionReq unsupported service") {
        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::PaymentServiceSelection>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        const auto selected_service = dt::SelectedService{11};
        const auto req =
            d2::msg::PaymentServiceSelectionRequest{header_req, dt::PaymentOption::ExternalPayment, {selected_service}};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(ctx.session.selected_payment_option.has_value());

            const auto response_message = ctx.get_response<d2::msg::PaymentServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto payment_service_selection_res = response_message.value();
            REQUIRE(payment_service_selection_res.response_code == dt::ResponseCode::FAILED);
        }
    }

    GIVEN("Bad case - PaymentServiceSelectionReq unsupported service parameter list") {
        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::PaymentServiceSelection>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        const auto selected_service = dt::SelectedService{10, 100};
        const auto req =
            d2::msg::PaymentServiceSelectionRequest{header_req, dt::PaymentOption::ExternalPayment, {selected_service}};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(ctx.session.selected_payment_option.has_value());

            const auto response_message = ctx.get_response<d2::msg::PaymentServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto payment_service_selection_res = response_message.value();
            REQUIRE(payment_service_selection_res.response_code == dt::ResponseCode::FAILED);
        }
    }

    GIVEN("Good case - ServiceDetailReq respond properly") {
        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::PaymentServiceSelection>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        const auto req = d2::msg::ServiceDetailRequest{header_req, custom_service.service_id};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d2::StateID::PaymentServiceSelection);

            const auto response_message = ctx.get_response<d2::msg::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto service_detail_res = response_message.value();
            REQUIRE(service_detail_res.response_code == dt::ResponseCode::OK);
            REQUIRE(service_detail_res.service_id == custom_service.service_id);
            REQUIRE(service_detail_res.service_parameter_list.has_value());

            const auto& service_parameter_list = service_detail_res.service_parameter_list.value();
            REQUIRE(service_parameter_list.size() == 1);

            const auto& parameter_set = service_parameter_list[0];
            REQUIRE(parameter_set.parameter_set_id == custom_service_parameter_set.parameter_set_id);
            REQUIRE(parameter_set.parameter.size() == 1);

            const auto& parameter = parameter_set.parameter[0];
            REQUIRE(parameter.name == "Foo");
            REQUIRE(std::holds_alternative<int>(parameter.value));
            REQUIRE(std::get<int>(parameter.value) == 100);
        }
    }

    GIVEN("Bad case - ServiceDetailReq unsupported service") {
        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::PaymentServiceSelection>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        const auto req = d2::msg::ServiceDetailRequest{header_req, 999};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d2::StateID::PaymentServiceSelection);

            const auto response_message = ctx.get_response<d2::msg::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto service_detail_res = response_message.value();
            REQUIRE(service_detail_res.response_code == dt::ResponseCode::FAILED);
        }
    }
}
