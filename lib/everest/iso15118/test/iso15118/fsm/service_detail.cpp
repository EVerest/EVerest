// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/service_detail.hpp>
#include <iso15118/d20/state/service_selection.hpp>

#include <iso15118/message/service_detail.hpp>

#include <iso15118/detail/helper.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 service detail state transitions") {

    namespace dt = message_20::datatypes;

    const auto evse_id = std::string("everest se");
    const std::vector<dt::ServiceCategory> supported_energy_services = {dt::ServiceCategory::DC};
    const auto cert_install{false};
    const std::vector<uint16_t> vas_services{}; // TODO(SL): Add Custom  service
    const std::vector<dt::Authorization> auth_services = {dt::Authorization::EIM};
    const d20::DcTransferLimits dc_limits;
    const d20::AcTransferLimits ac_limits;
    const d20::DcTransferLimits powersupply_limits;
    const std::vector<d20::ControlMobilityNeedsModes> control_mobility_modes = {
        {dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};

    const d20::EvseSetupConfig evse_setup{
        evse_id,   supported_energy_services, auth_services, vas_services, cert_install, dc_limits,
        ac_limits, control_mobility_modes,    std::nullopt, std::nullopt, std::nullopt, std::nullopt, powersupply_limits};

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    session::feedback::Callbacks callbacks{};

    callbacks.get_vas_parameters = [](uint16_t id) {
        auto service_parameter_list = dt::ServiceParameterList{};

        if (id == 4599) {
            auto& parameter_set = service_parameter_list.emplace_back();
            parameter_set.id = 0;
            parameter_set.parameter.push_back({"Service1", 40});
            parameter_set.parameter.push_back({"Service2", "house"});
        } else if (id == message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus)) {
            auto& parameter_set = service_parameter_list.emplace_back();
            parameter_set.id = 0;
            parameter_set.parameter.push_back({"IntendedService", 1});
            parameter_set.parameter.push_back({"ParkingStatusType", 4});
        } else if (id == message_20::to_underlying_value(dt::ServiceCategory::Internet)) {
            auto& parameter_set = service_parameter_list.emplace_back();
            parameter_set.id = 3;
            parameter_set.parameter.push_back({"Protocol", "http"});
            parameter_set.parameter.push_back({"Port", 80});
        }

        return std::make_optional(service_parameter_list);
    };

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();
    ctx.session = d20::Session();

    GIVEN("Bad Case - Unknown session") {

        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{d20::Session().get_id(), 1691411798};
        const auto req =
            message_20::ServiceDetailRequest{header_req, message_20::to_underlying_value(dt::ServiceCategory::DC)};

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDetail);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::DC));
            REQUIRE(res.service_parameter_list.size() == 1);
        }
    }

    GIVEN("Bad Case - FAILED_ServiceIDInvalid") {

        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req =
            message_20::ServiceDetailRequest{header_req, message_20::to_underlying_value(dt::ServiceCategory::DC_BPT)};

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDetail);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ServiceIDInvalid);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::DC));
            REQUIRE(res.service_parameter_list.size() == 1);
        }
    }

    GIVEN("Good Case - DC Service") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req =
            message_20::ServiceDetailRequest{header_req, message_20::to_underlying_value(dt::ServiceCategory::DC)};

        state_helper.handle_request(req);

        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::DC));
            REQUIRE(res.service_parameter_list.size() == 1);
            auto& parameters = res.service_parameter_list[0];
            REQUIRE(parameters.id == 0);
            REQUIRE(parameters.parameter.size() == 4);

            // Connector == Extended
            REQUIRE(parameters.parameter[0].name == "Connector");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[0].value) == 2);
            // ControlMode == Scheduled
            REQUIRE(parameters.parameter[1].name == "ControlMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[1].value) == 1);
            // MobilityNeedsMode == ProvidedbyEvcc
            REQUIRE(parameters.parameter[2].name == "MobilityNeedsMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[2].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[2].value) == 1);
            // Pricing == No Pricing
            REQUIRE(parameters.parameter[3].name == "Pricing");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[3].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[3].value) == 0);
        }
    }

    GIVEN("Good Case - DC_BPT Service") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req =
            message_20::ServiceDetailRequest{header_req, message_20::to_underlying_value(dt::ServiceCategory::DC_BPT)};

        state_helper.handle_request(req);

        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC_BPT};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::DC_BPT));
            REQUIRE(res.service_parameter_list.size() == 1);
            auto& parameters = res.service_parameter_list[0];
            REQUIRE(parameters.id == 0);
            REQUIRE(parameters.parameter.size() == 6);

            // Connector == Extended
            REQUIRE(parameters.parameter[0].name == "Connector");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[0].value) == 2);
            // ControlMode == Scheduled
            REQUIRE(parameters.parameter[1].name == "ControlMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[1].value) == 1);
            // MobilityNeedsMode == ProvidedbyEvcc
            REQUIRE(parameters.parameter[2].name == "MobilityNeedsMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[2].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[2].value) == 1);
            // Pricing == No Pricing
            REQUIRE(parameters.parameter[3].name == "Pricing");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[3].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[3].value) == 0);
            // BPTChannel == Unified
            REQUIRE(parameters.parameter[4].name == "BPTChannel");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[4].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[4].value) == 1);
            // GeneratorMode == GridFollowing
            REQUIRE(parameters.parameter[5].name == "GeneratorMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[5].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[5].value) == 1);
        }
    }

    GIVEN("Good Case - 2x DC Service") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req =
            message_20::ServiceDetailRequest{header_req, message_20::to_underlying_value(dt::ServiceCategory::DC)};

        state_helper.handle_request(req);

        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC};

        const auto temp = ctx.session_config.dc_parameter_list;
        ctx.session_config.dc_parameter_list = {{
                                                    dt::DcConnector::Extended,
                                                    dt::ControlMode::Scheduled,
                                                    dt::MobilityNeedsMode::ProvidedByEvcc,
                                                    dt::Pricing::NoPricing,
                                                },
                                                {
                                                    dt::DcConnector::Extended,
                                                    dt::ControlMode::Dynamic,
                                                    dt::MobilityNeedsMode::ProvidedBySecc,
                                                    dt::Pricing::NoPricing,
                                                }};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::DC));
            REQUIRE(res.service_parameter_list.size() == 2);
            auto& parameters_0 = res.service_parameter_list[0];
            REQUIRE(parameters_0.id == 0);
            REQUIRE(parameters_0.parameter.size() == 4);

            // Connector == Extended
            REQUIRE(parameters_0.parameter[0].name == "Connector");
            REQUIRE(std::holds_alternative<int32_t>(parameters_0.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters_0.parameter[0].value) == 2);
            // ControlMode == Scheduled
            REQUIRE(parameters_0.parameter[1].name == "ControlMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters_0.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters_0.parameter[1].value) == 1);
            // MobilityNeedsMode == ProvidedbyEvcc
            REQUIRE(parameters_0.parameter[2].name == "MobilityNeedsMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters_0.parameter[2].value));
            REQUIRE(std::get<int32_t>(parameters_0.parameter[2].value) == 1);
            // Pricing == No Pricing
            REQUIRE(parameters_0.parameter[3].name == "Pricing");
            REQUIRE(std::holds_alternative<int32_t>(parameters_0.parameter[3].value));
            REQUIRE(std::get<int32_t>(parameters_0.parameter[3].value) == 0);

            auto& parameters_1 = res.service_parameter_list[1];
            REQUIRE(parameters_1.id == 1);
            REQUIRE(parameters_1.parameter.size() == 4);

            // Connector == Extended
            REQUIRE(parameters_1.parameter[0].name == "Connector");
            REQUIRE(std::holds_alternative<int32_t>(parameters_1.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters_1.parameter[0].value) == 2);
            // ControlMode == Scheduled
            REQUIRE(parameters_1.parameter[1].name == "ControlMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters_1.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters_1.parameter[1].value) == 2);
            // MobilityNeedsMode == ProvidedbyEvcc
            REQUIRE(parameters_1.parameter[2].name == "MobilityNeedsMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters_1.parameter[2].value));
            REQUIRE(std::get<int32_t>(parameters_1.parameter[2].value) == 2);
            // Pricing == No Pricing
            REQUIRE(parameters_1.parameter[3].name == "Pricing");
            REQUIRE(std::holds_alternative<int32_t>(parameters_1.parameter[3].value));
            REQUIRE(std::get<int32_t>(parameters_1.parameter[3].value) == 0);
        }

        ctx.session_config.dc_parameter_list = temp;
    }

    GIVEN("Good Case - DC Service: Scheduled Mode: 1, MobilityNeedsMode: 2 change to 1") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req =
            message_20::ServiceDetailRequest{header_req, message_20::to_underlying_value(dt::ServiceCategory::DC)};

        state_helper.handle_request(req);

        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::DC));
            REQUIRE(res.service_parameter_list.size() == 1);
            auto& parameters = res.service_parameter_list[0];
            REQUIRE(parameters.id == 0);
            REQUIRE(parameters.parameter.size() == 4);

            // Connector == Extended
            REQUIRE(parameters.parameter[0].name == "Connector");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[0].value) == 2);
            // ControlMode == Scheduled
            REQUIRE(parameters.parameter[1].name == "ControlMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[1].value) == 1);
            // MobilityNeedsMode == ProvidedbyEvcc
            REQUIRE(parameters.parameter[2].name == "MobilityNeedsMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[2].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[2].value) == 1);
            // Pricing == No Pricing
            REQUIRE(parameters.parameter[3].name == "Pricing");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[3].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[3].value) == 0);
        }
    }

    GIVEN("Good Case - Internet Service") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req = message_20::ServiceDetailRequest{
            header_req, message_20::to_underlying_value(dt::ServiceCategory::Internet)};

        state_helper.handle_request(req);

        ctx.session_config.internet_parameter_list = {
            {dt::Protocol::Http, dt::Port::Port80}}; // TODO(SL): Reset to start

        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC};
        ctx.session.offered_services.vas_services = {message_20::to_underlying_value(dt::ServiceCategory::Internet)};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::Internet));
            REQUIRE(res.service_parameter_list.size() == 1);
            auto& parameters = res.service_parameter_list[0];
            REQUIRE(parameters.id == 3);
            REQUIRE(parameters.parameter.size() == 2);

            // Protocol == HTTP
            REQUIRE(parameters.parameter[0].name == "Protocol");
            REQUIRE(std::holds_alternative<std::string>(parameters.parameter[0].value));
            REQUIRE(std::get<std::string>(parameters.parameter[0].value) == "http");
            // Port == 80
            REQUIRE(parameters.parameter[1].name == "Port");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[1].value) == 80);
        }
    }

    GIVEN("Good Case - Parking status service") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req = message_20::ServiceDetailRequest{
            header_req, message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus)};

        state_helper.handle_request(req);

        ctx.session_config.parking_parameter_list = {
            {dt::IntendedService::VehicleCheckIn, dt::ParkingStatus::ManualExternal}}; // TODO(SL): Reset to start value

        ctx.session.offered_services.energy_services = {dt::ServiceCategory::DC};
        ctx.session.offered_services.vas_services = {
            message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus)};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus));
            REQUIRE(res.service_parameter_list.size() == 1);
            auto& parameters = res.service_parameter_list[0];
            REQUIRE(parameters.id == 0);
            REQUIRE(parameters.parameter.size() == 2);

            // IntendedService == VehicleCheckIn
            REQUIRE(parameters.parameter[0].name == "IntendedService");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[0].value) == 1);
            // ParkingStatusType == Manual/External
            REQUIRE(parameters.parameter[1].name == "ParkingStatusType");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[1].value) == 4);
        }
    }

    GIVEN("Good Case - AC Service") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req =
            message_20::ServiceDetailRequest{header_req, message_20::to_underlying_value(dt::ServiceCategory::AC)};

        state_helper.handle_request(req);

        ctx.session_config.ac_parameter_list = {{
            dt::AcConnector::ThreePhase,
            dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc,
            230,
            dt::Pricing::NoPricing,
        }};

        ctx.session.offered_services.energy_services = {dt::ServiceCategory::AC};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::AC));
            REQUIRE(res.service_parameter_list.size() == 1);
            auto& parameters = res.service_parameter_list[0];
            REQUIRE(parameters.id == 0);
            REQUIRE(parameters.parameter.size() == 5);

            // Connector == ThreePhases
            REQUIRE(parameters.parameter[0].name == "Connector");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[0].value) == 3);
            // ControlMode == Scheduled
            REQUIRE(parameters.parameter[1].name == "ControlMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[1].value) == 1);
            // EVSENominalVoltage == 230
            REQUIRE(parameters.parameter[2].name == "EVSENominalVoltage");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[2].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[2].value) == 230);
            // MobilityNeedsMode == ProvidedbyEvcc
            REQUIRE(parameters.parameter[3].name == "MobilityNeedsMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[3].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[3].value) == 1);
            // Pricing == No Pricing
            REQUIRE(parameters.parameter[4].name == "Pricing");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[4].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[4].value) == 0);
        }
    }

    GIVEN("Good Case - AC_BPT Service") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req =
            message_20::ServiceDetailRequest{header_req, message_20::to_underlying_value(dt::ServiceCategory::AC_BPT)};

        state_helper.handle_request(req);

        ctx.session.offered_services.energy_services = {dt::ServiceCategory::AC_BPT};

        ctx.session_config.ac_bpt_parameter_list = {{
            {
                dt::AcConnector::ThreePhase,
                dt::ControlMode::Scheduled,
                dt::MobilityNeedsMode::ProvidedByEvcc,
                230,
                dt::Pricing::NoPricing,
            },
            dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing,
            dt::GridCodeIslandingDetectionMethod::Passive,
        }};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::AC_BPT));
            REQUIRE(res.service_parameter_list.size() == 1);
            auto& parameters = res.service_parameter_list[0];
            REQUIRE(parameters.id == 0);
            REQUIRE(parameters.parameter.size() == 8);

            // Connector == ThreePhases
            REQUIRE(parameters.parameter[0].name == "Connector");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[0].value) == 3);
            // ControlMode == Scheduled
            REQUIRE(parameters.parameter[1].name == "ControlMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[1].value) == 1);
            // EVSENominalVoltage == 230
            REQUIRE(parameters.parameter[2].name == "EVSENominalVoltage");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[2].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[2].value) == 230);
            // MobilityNeedsMode == ProvidedbyEvcc
            REQUIRE(parameters.parameter[3].name == "MobilityNeedsMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[3].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[3].value) == 1);
            // Pricing == No Pricing
            REQUIRE(parameters.parameter[4].name == "Pricing");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[4].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[4].value) == 0);
            // BPTChannel == Unified
            REQUIRE(parameters.parameter[5].name == "BPTChannel");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[5].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[5].value) == 1);
            // GeneratorMode == GridFollowing
            REQUIRE(parameters.parameter[6].name == "GeneratorMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[6].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[6].value) == 1);
            // DetectionMethodGridCodeIslanding == Passive
            REQUIRE(parameters.parameter[7].name == "DetectionMethodGridCodeIslanding");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[7].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[7].value) == 2);
        }
    }

    GIVEN("Good Case - 2x AC Services") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req =
            message_20::ServiceDetailRequest{header_req, message_20::to_underlying_value(dt::ServiceCategory::AC)};

        state_helper.handle_request(req);

        ctx.session.offered_services.energy_services = {dt::ServiceCategory::AC};
        ctx.session_config.ac_parameter_list = {{
                                                    dt::AcConnector::ThreePhase,
                                                    dt::ControlMode::Scheduled,
                                                    dt::MobilityNeedsMode::ProvidedByEvcc,
                                                    230,
                                                    dt::Pricing::NoPricing,
                                                },
                                                {
                                                    dt::AcConnector::ThreePhase,
                                                    dt::ControlMode::Dynamic,
                                                    dt::MobilityNeedsMode::ProvidedBySecc,
                                                    230,
                                                    dt::Pricing::NoPricing,
                                                }};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::AC));
            REQUIRE(res.service_parameter_list.size() == 2);
            auto& parameters_0 = res.service_parameter_list[0];
            REQUIRE(parameters_0.id == 0);
            REQUIRE(parameters_0.parameter.size() == 5);

            // Connector == ThreePhases
            REQUIRE(parameters_0.parameter[0].name == "Connector");
            REQUIRE(std::holds_alternative<int32_t>(parameters_0.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters_0.parameter[0].value) == 3);
            // ControlMode == Scheduled
            REQUIRE(parameters_0.parameter[1].name == "ControlMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters_0.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters_0.parameter[1].value) == 1);
            // EVSENominalVoltage == 230
            REQUIRE(parameters_0.parameter[2].name == "EVSENominalVoltage");
            REQUIRE(std::holds_alternative<int32_t>(parameters_0.parameter[2].value));
            REQUIRE(std::get<int32_t>(parameters_0.parameter[2].value) == 230);
            // MobilityNeedsMode == ProvidedbyEvcc
            REQUIRE(parameters_0.parameter[3].name == "MobilityNeedsMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters_0.parameter[3].value));
            REQUIRE(std::get<int32_t>(parameters_0.parameter[3].value) == 1);
            // Pricing == No Pricing
            REQUIRE(parameters_0.parameter[4].name == "Pricing");
            REQUIRE(std::holds_alternative<int32_t>(parameters_0.parameter[4].value));
            REQUIRE(std::get<int32_t>(parameters_0.parameter[4].value) == 0);

            auto& parameters_1 = res.service_parameter_list[1];
            REQUIRE(parameters_1.id == 1);
            REQUIRE(parameters_1.parameter.size() == 5);

            // Connector == ThreePhases
            REQUIRE(parameters_1.parameter[0].name == "Connector");
            REQUIRE(std::holds_alternative<int32_t>(parameters_1.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters_1.parameter[0].value) == 3);
            // ControlMode == Dynamic
            REQUIRE(parameters_1.parameter[1].name == "ControlMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters_1.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters_1.parameter[1].value) == 2);
            // EVSENominalVoltage == 230
            REQUIRE(parameters_1.parameter[2].name == "EVSENominalVoltage");
            REQUIRE(std::holds_alternative<int32_t>(parameters_1.parameter[2].value));
            REQUIRE(std::get<int32_t>(parameters_1.parameter[2].value) == 230);
            // MobilityNeedsMode == ProvidedbySecc
            REQUIRE(parameters_1.parameter[3].name == "MobilityNeedsMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters_1.parameter[3].value));
            REQUIRE(std::get<int32_t>(parameters_1.parameter[3].value) == 2);
            // Pricing == No Pricing
            REQUIRE(parameters_1.parameter[4].name == "Pricing");
            REQUIRE(std::holds_alternative<int32_t>(parameters_1.parameter[4].value));
            REQUIRE(std::get<int32_t>(parameters_1.parameter[4].value) == 0);
        }
    }

    GIVEN("Good Case - AC Service: Scheduled Mode: 1, MobilityNeedsMode: 2 change to 1") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req =
            message_20::ServiceDetailRequest{header_req, message_20::to_underlying_value(dt::ServiceCategory::AC)};

        state_helper.handle_request(req);

        ctx.session.offered_services.energy_services = {dt::ServiceCategory::AC};
        ctx.session_config.ac_parameter_list = {{
            dt::AcConnector::ThreePhase,
            dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedBySecc,
            230,
            dt::Pricing::NoPricing,
        }};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::AC));
            REQUIRE(res.service_parameter_list.size() == 1);
            auto& parameters = res.service_parameter_list[0];
            REQUIRE(parameters.id == 0);
            REQUIRE(parameters.parameter.size() == 5);

            // Connector == ThreePhases
            REQUIRE(parameters.parameter[0].name == "Connector");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[0].value) == 3);
            // ControlMode == Scheduled
            REQUIRE(parameters.parameter[1].name == "ControlMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[1].value) == 1);
            // EVSENominalVoltage == 230
            REQUIRE(parameters.parameter[2].name == "EVSENominalVoltage");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[2].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[2].value) == 230);
            // MobilityNeedsMode == ProvidedbyEvcc
            REQUIRE(parameters.parameter[3].name == "MobilityNeedsMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[3].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[3].value) == 1);
            // Pricing == No Pricing
            REQUIRE(parameters.parameter[4].name == "Pricing");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[4].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[4].value) == 0);
        }
    }

    GIVEN("Good Case - MCS Service") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req =
            message_20::ServiceDetailRequest{header_req, message_20::to_underlying_value(dt::ServiceCategory::MCS)};

        state_helper.handle_request(req);

        ctx.session.offered_services.energy_services = {dt::ServiceCategory::MCS};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::MCS));
            REQUIRE(res.service_parameter_list.size() == 1);
            auto& parameters = res.service_parameter_list[0];
            REQUIRE(parameters.id == 0);
            REQUIRE(parameters.parameter.size() == 4);

            // Connector == MCS
            REQUIRE(parameters.parameter[0].name == "Connector");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[0].value) == 1);
            // ControlMode == Scheduled
            REQUIRE(parameters.parameter[1].name == "ControlMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[1].value) == 1);
            // MobilityNeedsMode == ProvidedbyEvcc
            REQUIRE(parameters.parameter[2].name == "MobilityNeedsMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[2].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[2].value) == 1);
            // Pricing == No Pricing
            REQUIRE(parameters.parameter[3].name == "Pricing");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[3].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[3].value) == 0);
        }
    }

    GIVEN("Good Case - MCS_BPT Service") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req =
            message_20::ServiceDetailRequest{header_req, message_20::to_underlying_value(dt::ServiceCategory::MCS_BPT)};

        state_helper.handle_request(req);

        ctx.session.offered_services.energy_services = {dt::ServiceCategory::MCS_BPT};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service == message_20::to_underlying_value(dt::ServiceCategory::MCS_BPT));
            REQUIRE(res.service_parameter_list.size() == 1);
            auto& parameters = res.service_parameter_list[0];
            REQUIRE(parameters.id == 0);
            REQUIRE(parameters.parameter.size() == 6);

            // Connector == MCS
            REQUIRE(parameters.parameter[0].name == "Connector");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[0].value) == 1);
            // ControlMode == Scheduled
            REQUIRE(parameters.parameter[1].name == "ControlMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[1].value) == 1);
            // MobilityNeedsMode == ProvidedbyEvcc
            REQUIRE(parameters.parameter[2].name == "MobilityNeedsMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[2].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[2].value) == 1);
            // Pricing == No Pricing
            REQUIRE(parameters.parameter[3].name == "Pricing");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[3].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[3].value) == 0);
            // BPTChannel == Unified
            REQUIRE(parameters.parameter[4].name == "BPTChannel");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[4].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[4].value) == 1);
            // GeneratorMode == GridFollowing
            REQUIRE(parameters.parameter[5].name == "GeneratorMode");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[5].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[5].value) == 1);
        }
    }

    GIVEN("Good case - Ev requests parameter from custom vas") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req = message_20::ServiceDetailRequest{header_req, 4599};

        state_helper.handle_request(req);

        ctx.session.offered_services.vas_services = {4599};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& service_detail_res = response_message.value();

            REQUIRE(service_detail_res.response_code == dt::ResponseCode::OK);
            REQUIRE(service_detail_res.service == 4599);
            REQUIRE(service_detail_res.service_parameter_list.size() == 1);
            auto& parameters = service_detail_res.service_parameter_list[0];
            REQUIRE(parameters.id == 0);
            REQUIRE(parameters.parameter.size() == 2);

            REQUIRE(parameters.parameter[0].name == "Service1");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[0].value) == 40);

            REQUIRE(parameters.parameter[1].name == "Service2");
            REQUIRE(std::holds_alternative<std::string>(parameters.parameter[1].value));
            REQUIRE(std::get<std::string>(parameters.parameter[1].value) == "house");
        }
    }

    GIVEN("Good case - Ev requests parameter from parking vas") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req = message_20::ServiceDetailRequest{
            header_req, message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus)};

        state_helper.handle_request(req);

        ctx.session.offered_services.vas_services = {
            message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus)};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& service_detail_res = response_message.value();

            REQUIRE(service_detail_res.response_code == dt::ResponseCode::OK);
            REQUIRE(service_detail_res.service == message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus));
            REQUIRE(service_detail_res.service_parameter_list.size() == 1);
            auto& parameters = service_detail_res.service_parameter_list[0];
            REQUIRE(parameters.id == 0);
            REQUIRE(parameters.parameter.size() == 2);

            REQUIRE(parameters.parameter[0].name == "IntendedService");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[0].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[0].value) == 1);

            REQUIRE(parameters.parameter[1].name == "ParkingStatusType");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[1].value) == 4);
        }
    }

    GIVEN("Good case - Ev requests parameter from internet vas") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDetail>()};

        const auto header_req = message_20::Header{ctx.session.get_id(), 1691411798};
        const auto req = message_20::ServiceDetailRequest{
            header_req, message_20::to_underlying_value(dt::ServiceCategory::Internet)};

        state_helper.handle_request(req);

        ctx.session.offered_services.vas_services = {message_20::to_underlying_value(dt::ServiceCategory::Internet)};

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceSelection);

            const auto response_message = ctx.get_response<message_20::ServiceDetailResponse>();
            REQUIRE(response_message.has_value());

            const auto& service_detail_res = response_message.value();

            REQUIRE(service_detail_res.response_code == dt::ResponseCode::OK);
            REQUIRE(service_detail_res.service == message_20::to_underlying_value(dt::ServiceCategory::Internet));
            REQUIRE(service_detail_res.service_parameter_list.size() == 1);
            auto& parameters = service_detail_res.service_parameter_list[0];
            REQUIRE(parameters.id == 3);
            REQUIRE(parameters.parameter.size() == 2);

            REQUIRE(parameters.parameter[0].name == "Protocol");
            REQUIRE(std::holds_alternative<std::string>(parameters.parameter[0].value));
            REQUIRE(std::get<std::string>(parameters.parameter[0].value) == "http");

            REQUIRE(parameters.parameter[1].name == "Port");
            REQUIRE(std::holds_alternative<int32_t>(parameters.parameter[1].value));
            REQUIRE(std::get<int32_t>(parameters.parameter[1].value) == 80);
        }
    }
}
