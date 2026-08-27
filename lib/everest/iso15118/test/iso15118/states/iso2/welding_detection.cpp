// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/state/welding_detection.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("ISO 15118-2 SECC WeldingDetection handling") {
    const dt::SessionId id{};
    message_2::WeldingDetectionRequest req;

    const auto res = d2::state::handle_request(req, id, 42.0f);

    THEN("OK, present voltage reported") {
        REQUIRE(res.response_code == dt::ResponseCode::OK);
        REQUIRE(dt::from_physical_value(res.evse_present_voltage) == 42.0);
        REQUIRE(res.dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Ready);
    }

    GIVEN("A module-reported EVSE error") {
        // An active send_error status override wins over EVSE_Ready (EvseV2G parity).
        const auto err = d2::state::handle_request(req, id, 42.0f, dt::DC_EVSEStatusCode::EVSE_Malfunction);
        THEN("the status code is EVSE_Malfunction") {
            REQUIRE(err.dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Malfunction);
        }
    }
}
