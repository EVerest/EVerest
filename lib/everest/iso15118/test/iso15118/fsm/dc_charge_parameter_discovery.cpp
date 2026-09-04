// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/dc_charge_parameter_discovery.hpp>

#include <iso15118/message/dc_charge_parameter_discovery.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

SCENARIO("ISO15118-20 dc charge parameter discovery state transitions") {

    auto evse_setup = create_default_evse_setup();
    evse_setup.powersupply_limits.voltage.min = {450, 0};

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();
    ctx.session = d20::Session();

    GIVEN("Bad case - incompatible limits must not override unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeParameterDiscovery>()};

        message_20::DC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode.emplace<dt::DC_CPDReqEnergyTransferMode>();
        req_out.max_charge_power = {50, 3};
        req_out.min_charge_power = {0, 0};
        req_out.max_charge_current = {125, 0};
        req_out.min_charge_current = {0, 0};
        req_out.max_voltage = {400, 0};
        req_out.min_voltage = {0, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("FAILED_UnknownSession is preserved") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::DC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());
            REQUIRE(response_message->response_code == dt::ResponseCode::FAILED_UnknownSession);
        }
    }
}
