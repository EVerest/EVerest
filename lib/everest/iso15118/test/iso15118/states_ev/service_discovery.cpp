// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/ev/state/service_discovery.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

namespace {
message_20::ServiceDiscoveryResponse make_response(const std::vector<dt::ServiceCategory>& offered) {
    message_20::ServiceDiscoveryResponse res;
    res.response_code = dt::ResponseCode::OK;
    res.energy_transfer_service_list.clear();
    for (const auto service : offered) {
        res.energy_transfer_service_list.push_back({service, false});
    }
    return res;
}
} // namespace

SCENARIO("EVCC ServiceDiscovery intersection") {

    GIVEN("The request omits the supported service ids") {
        const auto req = d20::ev::state::service_discovery::create_request();
        THEN("supported_service_ids is not set") {
            REQUIRE(req.supported_service_ids.has_value() == false);
        }
    }

    GIVEN("A matching DC service") {
        const auto res = make_response({dt::ServiceCategory::DC});
        const auto result = d20::ev::state::service_discovery::handle_response(res, {dt::ServiceCategory::DC});
        THEN("The DC service is selected") {
            REQUIRE(result.valid == true);
            REQUIRE(result.match_found == true);
            REQUIRE(result.selected_service == dt::ServiceCategory::DC);
        }
    }

    GIVEN("Overlapping services in different priority order") {
        // SECC offers AC first, then DC; EV prefers DC over AC.
        const auto res = make_response({dt::ServiceCategory::AC, dt::ServiceCategory::DC});
        const auto result =
            d20::ev::state::service_discovery::handle_response(res, {dt::ServiceCategory::DC, dt::ServiceCategory::AC});
        THEN("The EV's highest-priority match (DC) is selected") {
            REQUIRE(result.match_found == true);
            REQUIRE(result.selected_service == dt::ServiceCategory::DC);
        }
    }

    GIVEN("No overlapping service") {
        const auto res = make_response({dt::ServiceCategory::AC});
        const auto result = d20::ev::state::service_discovery::handle_response(res, {dt::ServiceCategory::DC});
        THEN("No match is found (session must stop)") {
            REQUIRE(result.valid == true);
            REQUIRE(result.match_found == false);
        }
    }

    GIVEN("A failed response") {
        message_20::ServiceDiscoveryResponse res;
        res.response_code = dt::ResponseCode::FAILED_UnknownSession;
        const auto result = d20::ev::state::service_discovery::handle_response(res, {dt::ServiceCategory::DC});
        THEN("The result is invalid") {
            REQUIRE(result.valid == false);
        }
    }
}
