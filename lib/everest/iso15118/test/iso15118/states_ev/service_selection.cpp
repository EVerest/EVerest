// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/ev/state/service_selection.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

SCENARIO("EVCC ServiceSelection request/response handling") {

    GIVEN("A request for the DC service with parameter set 3") {
        const auto req = d20::ev::state::service_selection::create_request(dt::ServiceCategory::DC, 3);
        THEN("The selected energy transfer service is filled and no VAS list is sent") {
            REQUIRE(req.selected_energy_transfer_service.service_id == dt::ServiceCategory::DC);
            REQUIRE(req.selected_energy_transfer_service.parameter_set_id == 3);
            REQUIRE(req.selected_vas_list.has_value() == false);
        }
    }

    GIVEN("An OK response") {
        message_20::ServiceSelectionResponse res;
        res.response_code = dt::ResponseCode::OK;
        const auto result = d20::ev::state::service_selection::handle_response(res);
        THEN("The result is valid") {
            REQUIRE(result.valid == true);
        }
    }

    GIVEN("A service-selection-invalid response") {
        message_20::ServiceSelectionResponse res;
        res.response_code = dt::ResponseCode::FAILED_ServiceSelectionInvalid;
        const auto result = d20::ev::state::service_selection::handle_response(res);
        THEN("The result is invalid") {
            REQUIRE(result.valid == false);
        }
    }
}
