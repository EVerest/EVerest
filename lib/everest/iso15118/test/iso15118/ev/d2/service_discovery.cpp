// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/service_discovery.hpp>
#include <iso15118/ev/detail/d2/state/service_discovery.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/session_stop.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {

message_2::ServiceDiscoveryResponse make_response(const message_2::Header& header, dt::ResponseCode code,
                                                  bool eim = true, bool contract = false,
                                                  bool certificate_service = false) {
    message_2::ServiceDiscoveryResponse res;
    res.header = header;
    res.response_code = code;
    if (eim) {
        res.payment_option_list.push_back(dt::PaymentOption::ExternalPayment);
    }
    if (contract) {
        res.payment_option_list.push_back(dt::PaymentOption::Contract);
    }
    res.charge_service.service_id = dt::CHARGE_SERVICE_ID;
    res.charge_service.service_category = dt::ServiceCategory::EVCharging;
    res.charge_service.free_service = false;
    res.charge_service.supported_energy_transfer_mode.push_back(dt::EnergyTransferMode::DC_extended);
    if (certificate_service) {
        dt::Service service;
        service.service_id = dt::CERTIFICATE_SERVICE_ID;
        service.service_category = dt::ServiceCategory::ContractCertificate;
        service.free_service = true;
        res.service_list.emplace().push_back(service);
    }
    return res;
}

} // namespace

SCENARIO("ISO15118-2 EV ServiceDiscovery sends the request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ServiceDiscovery> primed{callbacks, d2_no_seed};

    const auto request = primed.take_requests().get<message_2::ServiceDiscoveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
    REQUIRE_FALSE(request->service_scope.has_value());
    REQUIRE_FALSE(request->service_category.has_value());
}

SCENARIO("ISO15118-2 EV ServiceDiscovery transitions to PaymentServiceSelection") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ServiceDiscovery> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, true, true, true));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PaymentServiceSelection);
    REQUIRE(primed.ctx.evse_info.selected_charge_service_id == dt::CHARGE_SERVICE_ID);
    REQUIRE(primed.ctx.evse_info.contract_offered == true);
    REQUIRE(primed.ctx.evse_info.certificate_service_offered == true);
}

SCENARIO("ISO15118-2 EV ServiceDiscovery diverts to SessionStop without a usable payment option") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ServiceDiscovery> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, false, true, false));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_2::SessionStopRequest>().has_value());
}

SCENARIO("ISO15118-2 EV ServiceDiscovery diverts to SessionStop on a latched stop") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ServiceDiscovery> primed{callbacks, d2_no_seed};

    primed.ctx.set_stop_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
}

SCENARIO("ISO15118-2 EV ServiceDiscovery ignores control messages") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ServiceDiscovery> primed{callbacks, d2_no_seed};

    REQUIRE(primed.feed(ev::d2::Event::CONTROL_MESSAGE).output == ev::d2::Disposition::Ignored);
}

SCENARIO("ISO15118-2 EV ServiceDiscovery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{ctx.create_state<ev::d2::state::ServiceDiscovery>()};
    };
    const auto make_ok = [](const message_2::Header& header) { return make_response(header, dt::ResponseCode::OK); };
    message_2::AuthorizationResponse wrong;
    wrong.header = d2_header();
    wrong.response_code = dt::ResponseCode::OK;
    wrong.evse_processing = dt::EVSEProcessing::Finished;
    check_rejection_paths(callbacks, ev::d2::StateID::ServiceDiscovery, make_fsm, make_ok, wrong);
}

SCENARIO("ISO15118-2 EV ServiceDiscovery response parsing") {
    const auto res = make_response(d2_header(), dt::ResponseCode::OK, true, true, true);

    GIVEN("The requested mode is offered") {
        const auto result = ev::d2::state::service_discovery::handle_response(res, dt::EnergyTransferMode::DC_extended);
        REQUIRE(result.valid);
        REQUIRE(result.mode_supported);
        REQUIRE(result.eim_offered);
        REQUIRE(result.contract_offered);
        REQUIRE(result.certificate_service_offered);
    }

    GIVEN("The requested mode is not offered") {
        const auto result =
            ev::d2::state::service_discovery::handle_response(res, dt::EnergyTransferMode::AC_three_phase_core);
        REQUIRE(result.valid);
        REQUIRE_FALSE(result.mode_supported);
    }
}
