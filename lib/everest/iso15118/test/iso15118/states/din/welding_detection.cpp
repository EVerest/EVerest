// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/state/welding_detection.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;

SCENARIO("DIN SECC WeldingDetection state handling") {
    const dt::SessionId session{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("A welding detection request") {
        message_din::WeldingDetectionRequest req;
        req.header.session_id = session;

        const auto res = din::state::handle_request(req, 42.0f, session);
        THEN("The present voltage is reported with EVSE_Ready") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_present_voltage == 42.0);
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Ready);
        }
    }

    GIVEN("A module-reported EVSE error") {
        message_din::WeldingDetectionRequest req;
        req.header.session_id = session;
        // An active send_error status override wins over EVSE_Ready (EvseV2G parity).
        const auto res = din::state::handle_request(req, 42.0f, session, dt::DcEvseStatusCode::EVSE_Malfunction);
        THEN("the status code is EVSE_Malfunction") {
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Malfunction);
        }
    }
}
