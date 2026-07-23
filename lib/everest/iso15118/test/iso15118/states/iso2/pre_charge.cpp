// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/state/pre_charge.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("ISO 15118-2 SECC PreCharge handling") {
    const dt::SessionId id{};
    message_2::PreChargeRequest req;
    req.ev_target_voltage = dt::to_physical_value(400.0f, dt::Unit::V);

    const auto res = d2::state::handle_request(req, id, 398.0f);

    THEN("OK, present voltage reported, isolation Valid") {
        REQUIRE(res.response_code == dt::ResponseCode::OK);
        REQUIRE(dt::from_physical_value(res.evse_present_voltage) == 398.0);
        REQUIRE(res.dc_evse_status.isolation_status.value() == dt::IsolationLevel::Valid);
    }

    GIVEN("A module-reported EVSE error") {
        // An active send_error status override wins over EVSE_Ready (EvseV2G parity).
        const auto err = d2::state::handle_request(req, id, 398.0f, dt::DC_EVSEStatusCode::EVSE_Malfunction);
        THEN("the status code is EVSE_Malfunction") {
            REQUIRE(err.dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Malfunction);
        }
    }
}
