// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/state/pre_charge.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;

SCENARIO("DIN SECC PreCharge state handling") {
    const dt::SessionId session{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("A pre-charge request") {
        message_din::PreChargeRequest req;
        req.header.session_id = session;
        req.ev_target_voltage = 400.0;

        const auto res = din::state::handle_request(req, 390.0f, session);
        THEN("The present voltage is reported with EVSE_Ready") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_present_voltage == 390.0);
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Ready);
        }
    }
}
