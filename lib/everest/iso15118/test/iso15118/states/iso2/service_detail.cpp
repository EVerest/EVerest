// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/state/service_detail.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("ISO 15118-2 SECC ServiceDetail handling") {
    const dt::SessionId id{};
    constexpr uint16_t charge_service_id = 1;

    GIVEN("A request for the advertised charge service") {
        message_2::ServiceDetailRequest req;
        req.service_id = charge_service_id;
        const auto res = d2::state::handle_request(req, id, charge_service_id);
        THEN("OK and the service id is echoed") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service_id == charge_service_id);
        }
    }

    GIVEN("A request for an unknown service id") {
        message_2::ServiceDetailRequest req;
        req.service_id = 42;
        const auto res = d2::state::handle_request(req, id, charge_service_id);
        THEN("FAILED_ServiceIDInvalid and the service id is echoed") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ServiceIDInvalid);
            REQUIRE(res.service_id == 42);
        }
    }

    GIVEN("A request for the Certificate service while it is offered (PnC)") {
        message_2::ServiceDetailRequest req;
        req.service_id = dt::CERTIFICATE_SERVICE_ID;
        const auto res = d2::state::handle_request(req, id, charge_service_id, /*cert_service_offered=*/true);
        THEN("OK with the Table 106 Installation and Update parameter sets") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.service_id == dt::CERTIFICATE_SERVICE_ID);
            REQUIRE(res.service_parameter_list.has_value());
            REQUIRE(res.service_parameter_list->size() == 2);
            REQUIRE(res.service_parameter_list->at(0).parameter_set_id == 1);
            REQUIRE(res.service_parameter_list->at(0).parameter.front().name == "Service");
            REQUIRE(res.service_parameter_list->at(0).parameter.front().string_value == "Installation");
            REQUIRE(res.service_parameter_list->at(1).parameter_set_id == 2);
            REQUIRE(res.service_parameter_list->at(1).parameter.front().name == "Service");
            REQUIRE(res.service_parameter_list->at(1).parameter.front().string_value == "Update");
        }
    }

    GIVEN("A request for the Certificate service while it is NOT offered") {
        message_2::ServiceDetailRequest req;
        req.service_id = dt::CERTIFICATE_SERVICE_ID;
        const auto res = d2::state::handle_request(req, id, charge_service_id, /*cert_service_offered=*/false);
        THEN("FAILED_ServiceIDInvalid") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ServiceIDInvalid);
        }
    }
}
