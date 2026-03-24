// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/ac_charge_parameter_discovery.hpp>
#include <iso15118/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/d20/state/service_selection.hpp>

#include <iso15118/message/service_selection.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 service selection state transitions") {

    const d20::EvseSetupConfig evse_setup = create_default_evse_setup();

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();
    ctx.session = d20::Session();

    GIVEN("Bad Case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::ServiceSelectionRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::DC_BPT;
        req.selected_energy_transfer_service.parameter_set_id = 0;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
        }
    }

    GIVEN("Bad case: selected_energy_transfer_service false parameter set id - FAILED_ServiceSelectionInvalid") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        // Set up the session with offered services
        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC};
        ctx.session.offered_services.dc_parameter_list[0] = {
            dt::DcConnector::Extended,
            dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc,
            dt::Pricing::NoPricing,
        };

        message_20::ServiceSelectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::DC;
        req.selected_energy_transfer_service.parameter_set_id = 1; // Invalid parameter set id

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ServiceSelectionInvalid);
        }
    }
    GIVEN("Bad case: selected_energy_transfer service is not correct - FAILED_NoEnergyTransferServiceSelected") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        // Set up the session with offered services - only DC is offered
        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC};
        ctx.session.offered_services.dc_parameter_list[0] = {
            dt::DcConnector::Extended,
            dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc,
            dt::Pricing::NoPricing,
        };

        message_20::ServiceSelectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::AC; // Wrong service - AC not offered
        req.selected_energy_transfer_service.parameter_set_id = 0;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_NoEnergyTransferServiceSelected);
        }
    }
    GIVEN("Good case") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        // Set up the session with offered services
        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC};
        ctx.session.offered_services.dc_parameter_list[0] = {
            dt::DcConnector::Extended,
            dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc,
            dt::Pricing::NoPricing,
        };

        message_20::ServiceSelectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::DC;
        req.selected_energy_transfer_service.parameter_set_id = 0;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }
    GIVEN("Good case - Check if session variables is set") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        // Set up the session with offered services
        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC};
        ctx.session.offered_services.dc_parameter_list[0] = {
            dt::DcConnector::Extended,
            dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc,
            dt::Pricing::NoPricing,
        };

        message_20::ServiceSelectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::DC;
        req.selected_energy_transfer_service.parameter_set_id = 0;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition, response and session variables") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            // Check if session variables are set correctly
            const auto selected_services = ctx.session.get_selected_services();
            REQUIRE(selected_services.selected_energy_service == dt::ServiceCategory::DC);
            REQUIRE(selected_services.selected_control_mode == dt::ControlMode::Scheduled);
        }
    }
    GIVEN("Good case - DC_BPT") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC_BPT};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        // Set up the session with offered services
        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC_BPT};
        ctx.session.offered_services.dc_bpt_parameter_list[0] = {{
                                                                 dt::DcConnector::Extended,
                                                                 dt::ControlMode::Scheduled,
                                                                 dt::MobilityNeedsMode::ProvidedByEvcc,
                                                                 dt::Pricing::NoPricing,
                                                             },
                                                             dt::BptChannel::Unified,
                                                             dt::GeneratorMode::GridFollowing};

        message_20::ServiceSelectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::DC_BPT;
        req.selected_energy_transfer_service.parameter_set_id = 0;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition, response and session variables") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            // Check if session variables are set correctly
            const auto selected_services = ctx.session.get_selected_services();
            REQUIRE(selected_services.selected_energy_service == dt::ServiceCategory::DC_BPT);
            REQUIRE(selected_services.selected_control_mode == dt::ControlMode::Scheduled);
        }
    }
    GIVEN("Bad case: selected_vas_list false service id - FAILED_ServiceSelectionInvalid") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {message_20::to_underlying_value(dt::ServiceCategory::Internet)};
        ctx.session_ev_info.ev_energy_services = {};

        // Set up the session with offered services
        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC};
        ctx.session.offered_services.dc_parameter_list[0] = {
            dt::DcConnector::Extended,
            dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc,
            dt::Pricing::NoPricing,
        };

        ctx.session.offered_services.vas_services = {message_20::to_underlying_value(dt::ServiceCategory::Internet)};
        ctx.session.offered_services.internet_parameter_list[0] = {
            dt::Protocol::Http,
            dt::Port::Port80,
        };

        message_20::ServiceSelectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::DC;
        req.selected_energy_transfer_service.parameter_set_id = 0;

        req.selected_vas_list = {{message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus), 0}};

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ServiceSelectionInvalid);
        }
    }
    GIVEN("Bad case: selected_vas_list false parameter set id - FAILED_ServiceSelectionInvalid") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {message_20::to_underlying_value(dt::ServiceCategory::Internet)};
        ctx.session_ev_info.ev_energy_services = {};

        // Set up the session with offered services
        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC};
        ctx.session.offered_services.dc_parameter_list[0] = {
            dt::DcConnector::Extended,
            dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc,
            dt::Pricing::NoPricing,
        };

        ctx.session.offered_services.vas_services = {message_20::to_underlying_value(dt::ServiceCategory::Internet)};
        ctx.session.offered_services.internet_parameter_list[0] = {
            dt::Protocol::Http,
            dt::Port::Port80,
        };

        message_20::ServiceSelectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::DC;
        req.selected_energy_transfer_service.parameter_set_id = 0;

        req.selected_vas_list = {{message_20::to_underlying_value(dt::ServiceCategory::Internet), 1}};

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ServiceSelectionInvalid);
        }
    }
    GIVEN("Good case - DC & Internet & Parking") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {message_20::to_underlying_value(dt::ServiceCategory::Internet),
                                                     message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus)};
        ctx.session_ev_info.ev_energy_services = {};

        // Set up the session with offered services
        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC};
        ctx.session.offered_services.dc_parameter_list[0] = {
            dt::DcConnector::Extended,
            dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc,
            dt::Pricing::NoPricing,
        };

        ctx.session.offered_services.vas_services = {message_20::to_underlying_value(dt::ServiceCategory::Internet),
                                                     message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus)};
        ctx.session.offered_services.internet_parameter_list[0] = {
            dt::Protocol::Http,
            dt::Port::Port80,
        };

        ctx.session.offered_services.parking_parameter_list[0] = {
            dt::IntendedService::VehicleCheckIn,
            dt::ParkingStatus::ManualExternal,
        };

        message_20::ServiceSelectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::DC;
        req.selected_energy_transfer_service.parameter_set_id = 0;

        req.selected_vas_list = {{message_20::to_underlying_value(dt::ServiceCategory::Internet), 0},
                                 {message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus), 0}};

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }
    GIVEN("Good case - AC") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::AC};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        // Set up the session with offered services
        ctx.session.offered_services.energy_services = {dt::ServiceCategory::AC};
        ctx.session.offered_services.ac_parameter_list[0] = {
            dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, 230,
            dt::Pricing::NoPricing,
        };

        message_20::ServiceSelectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::AC;
        req.selected_energy_transfer_service.parameter_set_id = 0;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }
    GIVEN("Good case - AC_BPT") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::AC_BPT};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        // Set up the session with offered services
        ctx.session.offered_services.energy_services = {dt::ServiceCategory::AC_BPT};
        ctx.session.offered_services.ac_bpt_parameter_list[0] = {{
                                                                 dt::AcConnector::ThreePhase,
                                                                 dt::ControlMode::Scheduled,
                                                                 dt::MobilityNeedsMode::ProvidedByEvcc,
                                                                 230,
                                                                 dt::Pricing::NoPricing,
                                                             },
                                                             dt::BptChannel::Unified,
                                                             dt::GeneratorMode::GridFollowing,
                                                             dt::GridCodeIslandingDetectionMethod::Passive};

        message_20::ServiceSelectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::AC_BPT;
        req.selected_energy_transfer_service.parameter_set_id = 0;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition, response and session variables") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            // Check if session variables are set correctly
            const auto selected_services = ctx.session.get_selected_services();
            REQUIRE(selected_services.selected_energy_service == dt::ServiceCategory::AC_BPT);
            REQUIRE(selected_services.selected_control_mode == dt::ControlMode::Scheduled);
        }
    }
    GIVEN("Good case - MCS") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::MCS};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        // Set up the session with offered services
        ctx.session.offered_services.energy_services = {dt::ServiceCategory::MCS};
        ctx.session.offered_services.mcs_parameter_list[0] = {
            dt::McsConnector::Mcs,
            dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc,
            dt::Pricing::NoPricing,
        };

        message_20::ServiceSelectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::MCS;
        req.selected_energy_transfer_service.parameter_set_id = 0;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition, response and session variables") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            // Check if session variables are set correctly
            const auto selected_services = ctx.session.get_selected_services();
            REQUIRE(selected_services.selected_energy_service == dt::ServiceCategory::MCS);
            REQUIRE(selected_services.selected_control_mode == dt::ControlMode::Scheduled);
            REQUIRE(*std::get_if<dt::McsConnector>(&selected_services.selected_connector) == dt::McsConnector::Mcs);
        }
    }
    GIVEN("Good case - MCS_BPT") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::MCS_BPT};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        // Set up the session with offered services
        ctx.session.offered_services.energy_services = {dt::ServiceCategory::MCS_BPT};
        ctx.session.offered_services.mcs_bpt_parameter_list[0] = {{
                                                                  dt::McsConnector::Mcs,
                                                                  dt::ControlMode::Scheduled,
                                                                  dt::MobilityNeedsMode::ProvidedByEvcc,
                                                                  dt::Pricing::NoPricing,
                                                              },
                                                              dt::BptChannel::Unified,
                                                              dt::GeneratorMode::GridFollowing};

        message_20::ServiceSelectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::MCS_BPT;
        req.selected_energy_transfer_service.parameter_set_id = 0;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition, response and session variables") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            // Check if session variables are set correctly
            const auto selected_services = ctx.session.get_selected_services();
            REQUIRE(selected_services.selected_energy_service == dt::ServiceCategory::MCS_BPT);
            REQUIRE(selected_services.selected_control_mode == dt::ControlMode::Scheduled);
            REQUIRE(*std::get_if<dt::McsConnector>(&selected_services.selected_connector) == dt::McsConnector::Mcs);
            const auto bpt_channel = selected_services.selected_bpt_channel.has_value() and
                                     selected_services.selected_bpt_channel.value() == dt::BptChannel::Unified;
            REQUIRE(bpt_channel == true);
        }
    }
    GIVEN("Good case - DC & Custom VAS") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {4599};
        ctx.session_ev_info.ev_energy_services = {};

        // Set up the session with offered services
        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC};
        ctx.session.offered_services.dc_parameter_list[0] = {
            dt::DcConnector::Extended,
            dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc,
            dt::Pricing::NoPricing,
        };

        ctx.session.offered_services.vas_services = {4599};
        ctx.session.offered_services.custom_vas_list[4599] = {0, 2};

        message_20::ServiceSelectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_energy_transfer_service.service_id = dt::ServiceCategory::DC;
        req.selected_energy_transfer_service.parameter_set_id = 0;

        req.selected_vas_list = {
            {4599, 0},
        };

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::ServiceSelectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }

    // GIVEN("Bad case - FAILED_NoServiceRenegotiationSupported") {} // todo(sl): pause/resume not supported yet

    GIVEN("Event then other V2GTP_MESSAGE") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};
        const auto result = fsm.feed(d20::Event::FAILED);
        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);
        }
    }

    GIVEN("SessionStopReq") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::EIM};

        message_20::SessionStopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.charging_session = dt::ChargingSession::Terminate;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_stop_res = response_message.value();
            REQUIRE(session_stop_res.response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("Sequence Error") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceSelection>()};

        // Setting up session_config based on test
        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::EIM};

        message_20::SessionSetupRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.evccid = "WMIV1234567890ABCDEX";

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::SessionSetupResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_setup_res = response_message.value();
            REQUIRE(session_setup_res.response_code == dt::ResponseCode::FAILED_SequenceError);
        }
    }
}
