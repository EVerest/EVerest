// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/ev/state/service_detail.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

SCENARIO("EVCC ServiceDetail control-mode extraction") {

    GIVEN("A ServiceDetail request for DC") {
        const auto req =
            d20::ev::state::service_detail::create_request(message_20::to_underlying_value(dt::ServiceCategory::DC));
        THEN("The service id is set") {
            REQUIRE(req.service == message_20::to_underlying_value(dt::ServiceCategory::DC));
        }
    }

    GIVEN("A Dynamic DC parameter set (SECC get_default_dc_parameter_list shape)") {
        message_20::ServiceDetailResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.service = message_20::to_underlying_value(dt::ServiceCategory::DC);
        res.service_parameter_list.clear();
        const dt::DcParameterList list{dt::DcConnector::Extended, dt::ControlMode::Dynamic,
                                       dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing};
        res.service_parameter_list.push_back(dt::ParameterSet(7, list));

        const auto result = d20::ev::state::service_detail::handle_response(res, dt::ControlMode::Dynamic);
        THEN("The dynamic parameter set is selected with its control/mobility modes") {
            REQUIRE(result.valid == true);
            REQUIRE(result.control_mode_found == true);
            REQUIRE(result.parameter_set_id == 7);
            REQUIRE(result.control_mode == dt::ControlMode::Dynamic);
            REQUIRE(result.mobility_needs_mode == dt::MobilityNeedsMode::ProvidedByEvcc);
        }
    }

    GIVEN("A parameter set without a control mode") {
        message_20::ServiceDetailResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.service = message_20::to_underlying_value(dt::ServiceCategory::DC);
        res.service_parameter_list = {dt::ParameterSet(0)}; // default set holds only an empty bool parameter

        const auto result = d20::ev::state::service_detail::handle_response(res, dt::ControlMode::Dynamic);
        THEN("No control mode is found (session must stop)") {
            REQUIRE(result.valid == true);
            REQUIRE(result.control_mode_found == false);
        }
    }

    GIVEN("A failed response") {
        message_20::ServiceDetailResponse res;
        res.response_code = dt::ResponseCode::FAILED_ServiceIDInvalid;
        const auto result = d20::ev::state::service_detail::handle_response(res, dt::ControlMode::Dynamic);
        THEN("The result is invalid") {
            REQUIRE(result.valid == false);
        }
    }
}
