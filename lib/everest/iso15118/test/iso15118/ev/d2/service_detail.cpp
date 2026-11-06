// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/service_detail.hpp>
#include <iso15118/ev/d2/state/service_discovery.hpp>
#include <iso15118/ev/d2/timeouts.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/service_detail.hpp>
#include <iso15118/message_2/service_discovery.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {
message_2::ServiceDiscoveryResponse discovery(bool eim = true) {
    auto res = d2_ok<message_2::ServiceDiscoveryResponse>();
    res.payment_option_list.push_back(dt::PaymentOption::Contract);
    if (eim) {
        res.payment_option_list.push_back(dt::PaymentOption::ExternalPayment);
    }
    res.charge_service.service_id = dt::CHARGE_SERVICE_ID;
    res.charge_service.supported_energy_transfer_mode.push_back(dt::EnergyTransferMode::DC_extended);
    dt::Service service{};
    service.service_id = dt::CERTIFICATE_SERVICE_ID;
    service.service_category = dt::ServiceCategory::ContractCertificate;
    res.service_list.emplace().push_back(service);
    return res;
}

message_2::ServiceDetailResponse detail(int16_t parameter_set_id) {
    auto res = d2_ok<message_2::ServiceDetailResponse>();
    res.service_id = dt::CERTIFICATE_SERVICE_ID;
    dt::ParameterSet set{};
    set.parameter_set_id = parameter_set_id;
    res.service_parameter_list.emplace().push_back(set);
    return res;
}

ev::params::EvSessionParams installation_params() {
    ev::params::EvSessionParams params;
    params.secure_transport = true;
    params.pnc.prefer_contract = true;
    return params;
}
} // namespace

SCENARIO("ISO15118-2 EV selects the installation parameter set after ServiceDetail") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ServiceDiscovery> primed{callbacks, installation_params(), false, d2_no_seed};
    primed.handle_response(discovery());
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned());
    REQUIRE(primed.take_requests().types() == std::vector<message_2::Type>{message_2::Type::ServiceDetailReq});
    primed.handle_response(detail(1));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned());
    const auto request = primed.take_requests().get<message_2::PaymentServiceSelectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->selected_payment_option == dt::PaymentOption::Contract);
    REQUIRE(request->selected_service_list.size() == 2);
    REQUIRE(request->selected_service_list[0].service_id == dt::CHARGE_SERVICE_ID);
    REQUIRE_FALSE(request->selected_service_list[0].parameter_set_id.has_value());
    REQUIRE(request->selected_service_list[1].service_id == dt::CERTIFICATE_SERVICE_ID);
    REQUIRE(request->selected_service_list[1].parameter_set_id == 1);
}

SCENARIO("ISO15118-2 EV falls back when ServiceDetail does not offer installation") {
    const ev::feedback::Callbacks callbacks{};
    const auto reply = GENERATE(std::string{"update only"}, "missing list", "empty list", "wrong service");
    auto response = detail(reply == "wrong service" ? 1 : 2);
    if (reply == "missing list") {
        response.service_parameter_list.reset();
    } else if (reply == "empty list") {
        response.service_parameter_list->clear();
    } else if (reply == "wrong service") {
        response.service_id = 3;
    }

    auto params = installation_params();
    const bool eim = GENERATE(true, false);
    const auto contract = GENERATE(std::string{"absent"}, "enforced", "on hand");
    CAPTURE(reply, eim, contract);
    dt::PaymentOption expected = dt::PaymentOption::ExternalPayment;
    if (contract == "enforced") {
        params.pnc.enforce_contract = true;
        expected = dt::PaymentOption::Contract;
    } else if (contract == "on hand") {
        params.pnc.force_cert_install = true;
        params.pnc.contract = ev::d2::ContractMaterial{{0x30, 0x01}, {}, "key", std::nullopt, "DEPNXCONTRACT1"};
        expected = dt::PaymentOption::Contract;
    }

    PrimedState<ev::d2::state::ServiceDiscovery> primed{callbacks, params, false, d2_no_seed};
    primed.handle_response(discovery(eim));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned());
    REQUIRE(primed.take_requests().types() == std::vector<message_2::Type>{message_2::Type::ServiceDetailReq});
    primed.handle_response(response);
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned());
    const auto requests = primed.take_requests();
    if (not eim and expected == dt::PaymentOption::ExternalPayment) {
        REQUIRE(requests.types() == std::vector<message_2::Type>{message_2::Type::SessionStopReq});
    } else {
        const auto request = requests.get<message_2::PaymentServiceSelectionRequest>();
        REQUIRE(request.has_value());
        REQUIRE(request->selected_payment_option == expected);
        REQUIRE(request->selected_service_list.size() == 1);
        REQUIRE(request->selected_service_list.front().service_id == dt::CHARGE_SERVICE_ID);
        primed.handle_response(d2_ok<message_2::PaymentServiceSelectionResponse>());
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(primed.take_requests().types() ==
                std::vector<message_2::Type>{expected == dt::PaymentOption::Contract
                                                 ? message_2::Type::PaymentDetailsReq
                                                 : message_2::Type::AuthorizationReq});
    }
}

SCENARIO("ISO15118-2 EV validates ServiceDetail before selecting a service") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ServiceDiscovery> primed{callbacks, installation_params(), false, d2_no_seed};
    primed.handle_response(discovery());
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned());
    REQUIRE(primed.take_requests().types() == std::vector<message_2::Type>{message_2::Type::ServiceDetailReq});
    REQUIRE(primed.feed(ev::d2::Event::CONTROL_MESSAGE).output == ev::d2::Disposition::Ignored);
    REQUIRE(primed.take_requests().empty());

    SECTION("a latched stop sends SessionStop") {
        primed.ctx.set_stop_charging_requested(true);
        primed.handle_response(detail(1));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(primed.take_requests().types() == std::vector<message_2::Type>{message_2::Type::SessionStopReq});
    }
}

SCENARIO("ISO15118-2 EV ServiceDetail rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{ctx.create_state<ev::d2::state::ServiceDetail>()};
    };
    const auto make_ok = [](const message_2::Header& header) {
        auto res = detail(1);
        res.header = header;
        return res;
    };
    check_rejection_paths(callbacks, ev::d2::StateID::ServiceDetail, make_fsm, make_ok, discovery(),
                          installation_params());
}

SCENARIO("ISO15118-2 EV allows five seconds for ServiceDetail responses") {
    REQUIRE(ev::d2::timeouts::response_timeout(ev::d2::StateID::ServiceDetail) == std::chrono::milliseconds{5000});
}
