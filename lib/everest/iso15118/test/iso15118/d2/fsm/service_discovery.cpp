// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d2/state/service_discovery.hpp>

#include <iso15118/message/d2/service_discovery.hpp>
#include <optional>
#include <vector>

using namespace iso15118;

SCENARIO("ISO15118-2 service discovery state transitions") {

    namespace dt = d2::msg::data_types;

    // Move to helper function?
    const auto evse_id = std::string("everest se");
    const auto expected_charge_service = dt::ChargeService{
        dt::Service{
            1,
            "AC_DC_Charging",
            dt::ServiceCategory::EvCharging,
            std::nullopt,
            true,
        },
        {dt::EnergyTransferMode::DcExtended},
    };

    const std::vector<dt::EnergyTransferMode> supported_energy_transfer_modes = {dt::EnergyTransferMode::DcExtended};
    const std::vector<dt::PaymentOption> supported_payment_options = {dt::PaymentOption::ExternalPayment};
    const std::vector<dt::ServiceID> offered_services{};
    const d2::EvseSetupConfig evse_setup{evse_id, supported_energy_transfer_modes, supported_payment_options,
                                         offered_services};

    session::feedback::Callbacks feedback_callbacks;
    feedback_callbacks.get_service_from_id = [](dt::ServiceID id) -> std::optional<dt::Service> {
        if (id == 10) {
            return dt::Service{10, "Custom", dt::ServiceCategory::OtherCustom, "https://example.com", true};
        } else {
            return std::nullopt;
        }
    };

    auto state_helper = FsmStateHelper(d2::SessionConfig(evse_setup), feedback_callbacks);
    auto ctx = state_helper.get_context();

    GIVEN("Good case - respond properly") {
        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::ServiceDiscovery>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        const auto req = d2::msg::ServiceDiscoveryRequest{header_req};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d2::StateID::PaymentServiceSelection);
            REQUIRE(ctx.session.get_id() != std::array<uint8_t, 8>{0});

            const auto response_message = ctx.get_response<d2::msg::ServiceDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_setup_res = response_message.value();
            REQUIRE(session_setup_res.response_code == dt::ResponseCode::OK);
            REQUIRE(session_setup_res.charge_service.service_id == expected_charge_service.service_id);
            REQUIRE(session_setup_res.charge_service.service_name == expected_charge_service.service_name);
            REQUIRE(session_setup_res.charge_service.service_category == expected_charge_service.service_category);
            REQUIRE(session_setup_res.charge_service.service_scope == expected_charge_service.service_scope);
            REQUIRE(session_setup_res.charge_service.FreeService == expected_charge_service.FreeService);
            REQUIRE(session_setup_res.charge_service.supported_energy_transfer_mode ==
                    expected_charge_service.supported_energy_transfer_mode);

            REQUIRE(session_setup_res.payment_option_list == supported_payment_options);
            REQUIRE(session_setup_res.service_list.has_value() == false);
        }
    }

    GIVEN("Good case - respond with offered services") {
        ctx.session_config.offered_services = {2, 3, 10}; // Certificate, InternetAccess and Custom

        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::ServiceDiscovery>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        const auto req = d2::msg::ServiceDiscoveryRequest{header_req};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d2::StateID::PaymentServiceSelection);
            REQUIRE(ctx.session.get_id() != std::array<uint8_t, 8>{0});

            const auto response_message = ctx.get_response<d2::msg::ServiceDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_setup_res = response_message.value();
            REQUIRE(session_setup_res.response_code == dt::ResponseCode::OK);
            REQUIRE(session_setup_res.charge_service.service_id == expected_charge_service.service_id);
            REQUIRE(session_setup_res.charge_service.service_name == expected_charge_service.service_name);
            REQUIRE(session_setup_res.charge_service.service_category == expected_charge_service.service_category);
            REQUIRE(session_setup_res.charge_service.service_scope == expected_charge_service.service_scope);
            REQUIRE(session_setup_res.charge_service.FreeService == expected_charge_service.FreeService);
            REQUIRE(session_setup_res.charge_service.supported_energy_transfer_mode ==
                    expected_charge_service.supported_energy_transfer_mode);

            REQUIRE(session_setup_res.payment_option_list == supported_payment_options);

            REQUIRE(session_setup_res.service_list.has_value() == true);
            REQUIRE(session_setup_res.service_list->size() == 3);
            const auto& service1 = session_setup_res.service_list->at(0);
            const auto& service2 = session_setup_res.service_list->at(1);
            const auto& service3 = session_setup_res.service_list->at(2);
            REQUIRE(service1.service_id == 2);
            REQUIRE(service1.service_name.has_value());
            REQUIRE(service1.service_name == "Certificate");
            REQUIRE(service1.service_category == dt::ServiceCategory::ContractCertificate);
            REQUIRE(!service1.service_scope.has_value());
            REQUIRE(service1.FreeService);
            REQUIRE(service2.service_id == 3);
            REQUIRE(service2.service_name.has_value());
            REQUIRE(service2.service_name == "InternetAccess");
            REQUIRE(service2.service_category == dt::ServiceCategory::Internet);
            REQUIRE(!service2.service_scope.has_value());
            REQUIRE(service2.FreeService);
            REQUIRE(service3.service_id == 10);
            REQUIRE(service3.service_name.has_value());
            REQUIRE(service3.service_name == "Custom");
            REQUIRE(service3.service_category == dt::ServiceCategory::OtherCustom);
            REQUIRE(service3.service_scope.has_value());
            REQUIRE(service3.service_scope == "https://example.com");
            REQUIRE(service3.FreeService);
        }
    }

    GIVEN("Good case - respond with filtered services by category") {
        ctx.session_config.offered_services = {2, 3, 10}; // Certificate, InternetAccess and Custom

        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::ServiceDiscovery>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        auto req = d2::msg::ServiceDiscoveryRequest{header_req};
        req.service_category = dt::ServiceCategory::Internet;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d2::StateID::PaymentServiceSelection);
            REQUIRE(ctx.session.get_id() != std::array<uint8_t, 8>{0});

            const auto response_message = ctx.get_response<d2::msg::ServiceDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_setup_res = response_message.value();
            REQUIRE(session_setup_res.response_code == dt::ResponseCode::OK);
            REQUIRE(session_setup_res.charge_service.service_id == expected_charge_service.service_id);
            REQUIRE(session_setup_res.charge_service.service_name == expected_charge_service.service_name);
            REQUIRE(session_setup_res.charge_service.service_category == expected_charge_service.service_category);
            REQUIRE(session_setup_res.charge_service.service_scope == expected_charge_service.service_scope);
            REQUIRE(session_setup_res.charge_service.FreeService == expected_charge_service.FreeService);
            REQUIRE(session_setup_res.charge_service.supported_energy_transfer_mode ==
                    expected_charge_service.supported_energy_transfer_mode);

            REQUIRE(session_setup_res.payment_option_list == supported_payment_options);

            REQUIRE(session_setup_res.service_list.has_value() == true);
            REQUIRE(session_setup_res.service_list->size() == 1);
            const auto& service1 = session_setup_res.service_list->at(0);
            REQUIRE(service1.service_id == 3);
            REQUIRE(service1.service_name.has_value());
            REQUIRE(service1.service_name.value() == "InternetAccess");
            REQUIRE(service1.service_category == dt::ServiceCategory::Internet);
            REQUIRE(!service1.service_scope.has_value());
            REQUIRE(service1.FreeService);
        }
    }

    GIVEN("Good case - respond with filtered services by scope") {
        ctx.session_config.offered_services = {2, 3, 10}; // Certificate, InternetAccess and Custom

        fsm::v2::FSM<d2::StateBase> fsm{ctx.create_state<d2::state::ServiceDiscovery>()};

        const auto header_req = d2::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};
        auto req = d2::msg::ServiceDiscoveryRequest{header_req};
        req.service_scope = "https://example.com";

        state_helper.handle_request(req);
        const auto result = fsm.feed(d2::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response values") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d2::StateID::PaymentServiceSelection);
            REQUIRE(ctx.session.get_id() != std::array<uint8_t, 8>{0});

            const auto response_message = ctx.get_response<d2::msg::ServiceDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_setup_res = response_message.value();
            REQUIRE(session_setup_res.response_code == dt::ResponseCode::OK);
            REQUIRE(session_setup_res.charge_service.service_id == expected_charge_service.service_id);
            REQUIRE(session_setup_res.charge_service.service_name == expected_charge_service.service_name);
            REQUIRE(session_setup_res.charge_service.service_category == expected_charge_service.service_category);
            REQUIRE(session_setup_res.charge_service.service_scope == expected_charge_service.service_scope);
            REQUIRE(session_setup_res.charge_service.FreeService == expected_charge_service.FreeService);
            REQUIRE(session_setup_res.charge_service.supported_energy_transfer_mode ==
                    expected_charge_service.supported_energy_transfer_mode);

            REQUIRE(session_setup_res.payment_option_list == supported_payment_options);

            REQUIRE(session_setup_res.service_list.has_value() == true);
            REQUIRE(session_setup_res.service_list->size() == 1);
            const auto& service1 = session_setup_res.service_list->at(0);
            REQUIRE(service1.service_id == 10);
            REQUIRE(service1.service_name.has_value());
            REQUIRE(service1.service_name.value() == "Custom");
            REQUIRE(service1.service_category == dt::ServiceCategory::OtherCustom);
            REQUIRE(service1.service_scope.has_value());
            REQUIRE(service1.service_scope.value() == "https://example.com");
            REQUIRE(service1.FreeService);
        }
    }
}
