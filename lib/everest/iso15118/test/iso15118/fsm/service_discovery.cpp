// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/service_detail.hpp>
#include <iso15118/d20/state/service_discovery.hpp>

#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 service discovery state transitions") {

    const d20::EvseSetupConfig evse_setup = create_default_evse_setup();

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();
    ctx.session = d20::Session();

    GIVEN("Bad Case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::ServiceDiscoveryRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDiscovery);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::ServiceDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(res.service_renegotiation_supported == false);
            REQUIRE(res.energy_transfer_service_list.size() == 1);
            REQUIRE(res.energy_transfer_service_list[0].free_service == false);
            REQUIRE(res.energy_transfer_service_list[0].service_id == dt::ServiceCategory::AC);
            REQUIRE(res.vas_list.has_value() == false);
        }
    }

    GIVEN("Good Case - Setting ac services") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::AC, dt::ServiceCategory::AC_BPT};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::ServiceDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDetail);

            const auto response_message = ctx.get_response<message_20::ServiceDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service_renegotiation_supported == false);
            REQUIRE(res.energy_transfer_service_list.size() == 2);
            REQUIRE(res.energy_transfer_service_list[0].free_service == false);
            REQUIRE((res.energy_transfer_service_list[0].service_id == dt::ServiceCategory::AC ||
                     res.energy_transfer_service_list[0].service_id == dt::ServiceCategory::AC_BPT));
            REQUIRE(res.energy_transfer_service_list[1].free_service == false);
            REQUIRE((res.energy_transfer_service_list[1].service_id == dt::ServiceCategory::AC ||
                     res.energy_transfer_service_list[1].service_id == dt::ServiceCategory::AC_BPT));
            REQUIRE(res.vas_list.has_value() == false);
        }
    }
    GIVEN("Good Case - Setting dc services") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC, dt::ServiceCategory::DC_BPT};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::ServiceDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDetail);

            const auto response_message = ctx.get_response<message_20::ServiceDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service_renegotiation_supported == false);
            REQUIRE(res.energy_transfer_service_list.size() == 2);
            REQUIRE(res.energy_transfer_service_list[0].free_service == false);
            REQUIRE((res.energy_transfer_service_list[0].service_id == dt::ServiceCategory::DC ||
                     res.energy_transfer_service_list[0].service_id == dt::ServiceCategory::DC_BPT));
            REQUIRE(res.energy_transfer_service_list[1].free_service == false);
            REQUIRE((res.energy_transfer_service_list[1].service_id == dt::ServiceCategory::DC ||
                     res.energy_transfer_service_list[1].service_id == dt::ServiceCategory::DC_BPT));
            REQUIRE(res.vas_list.has_value() == false);
        }
    }
    GIVEN("Good Case - Setting MCS services") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::MCS, dt::ServiceCategory::MCS_BPT};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::ServiceDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDetail);

            const auto response_message = ctx.get_response<message_20::ServiceDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service_renegotiation_supported == false);
            REQUIRE(res.energy_transfer_service_list.size() == 2);
            REQUIRE(res.energy_transfer_service_list[0].free_service == false);
            REQUIRE((res.energy_transfer_service_list[0].service_id == dt::ServiceCategory::MCS ||
                     res.energy_transfer_service_list[0].service_id == dt::ServiceCategory::MCS_BPT));
            REQUIRE(res.energy_transfer_service_list[1].free_service == false);
            REQUIRE((res.energy_transfer_service_list[1].service_id == dt::ServiceCategory::MCS ||
                     res.energy_transfer_service_list[1].service_id == dt::ServiceCategory::MCS_BPT));
            REQUIRE(res.vas_list.has_value() == false);
        }
    }
    GIVEN("Good Case - Setting services + vas list") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC, dt::ServiceCategory::DC_BPT};
        ctx.session_config.supported_vas_services = {message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus)};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::ServiceDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDetail);

            const auto response_message = ctx.get_response<message_20::ServiceDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service_renegotiation_supported == false);
            REQUIRE(res.energy_transfer_service_list.size() == 2);
            REQUIRE(res.energy_transfer_service_list[0].free_service == false);
            REQUIRE((res.energy_transfer_service_list[0].service_id == dt::ServiceCategory::DC ||
                     res.energy_transfer_service_list[0].service_id == dt::ServiceCategory::DC_BPT));
            REQUIRE(res.energy_transfer_service_list[1].free_service == false);
            REQUIRE((res.energy_transfer_service_list[1].service_id == dt::ServiceCategory::DC ||
                     res.energy_transfer_service_list[1].service_id == dt::ServiceCategory::DC_BPT));
            REQUIRE(res.vas_list.has_value() == true);
            REQUIRE(res.vas_list.value()[0].free_service == false);
            REQUIRE(res.vas_list.value()[0].service_id == message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus));
        }
    }
    GIVEN("Good Case - Filter supported_service_providers") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC, dt::ServiceCategory::DC_BPT};
        ctx.session_config.supported_vas_services = {message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus)};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::ServiceDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& supported_service_ids = req.supported_service_ids.emplace();
        supported_service_ids.push_back(2); // DC service ID
        supported_service_ids.push_back(65); // Internet service ID

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDetail);

            const auto response_message = ctx.get_response<message_20::ServiceDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service_renegotiation_supported == false);
            REQUIRE(res.energy_transfer_service_list.size() == 1);
            REQUIRE(res.energy_transfer_service_list[0].free_service == false);
            REQUIRE(res.energy_transfer_service_list[0].service_id == dt::ServiceCategory::DC);
            REQUIRE(res.vas_list.has_value() == false);
        }
    }

    // [V2G20-1644]
    GIVEN("Good case - Resuming secc shall provide the same service ids and parameter set ids "
          "(ServiceRenegotiationSupported: false)") {
    } // Todo(sl): Fill out

    GIVEN("Bad case - EV supported_service_ids do not match with evse supported services") {
    } // Todo(sl): Fill out

    GIVEN("Event then other V2GTP_MESSAGE") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDiscovery>()};
        const auto result = fsm.feed(d20::Event::FAILED);
        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDiscovery);
        }
    }

    GIVEN("SessionStopReq") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDiscovery>()};

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
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDiscovery);

            const auto response_message = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_stop_res = response_message.value();
            REQUIRE(session_stop_res.response_code == dt::ResponseCode::OK);
        }
    }
    GIVEN("Sequence Error") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ServiceDiscovery>()};

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
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDiscovery);

            const auto response_message = ctx.get_response<message_20::SessionSetupResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_setup_res = response_message.value();
            REQUIRE(session_setup_res.response_code == dt::ResponseCode::FAILED_SequenceError);
        }
    }
}
