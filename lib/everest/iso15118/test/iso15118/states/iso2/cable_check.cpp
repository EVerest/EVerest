// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/state/cable_check.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("ISO 15118-2 SECC CableCheck handling") {
    const dt::SessionId id{};
    message_2::CableCheckRequest req;

    GIVEN("Cable check ongoing") {
        const auto res = d2::state::handle_request(req, id, false);
        THEN("OK, Ongoing, isolation Invalid") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EVSEProcessing::Ongoing);
            REQUIRE(res.dc_evse_status.isolation_status.value() == dt::IsolationLevel::Invalid);
        }
    }

    GIVEN("Cable check finished") {
        const auto res = d2::state::handle_request(req, id, true);
        THEN("OK, Finished, isolation Valid, EVSE_Ready") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EVSEProcessing::Finished);
            REQUIRE(res.dc_evse_status.isolation_status.value() == dt::IsolationLevel::Valid);
            REQUIRE(res.dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Ready);
        }
    }
}
