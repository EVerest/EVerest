// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/state/power_delivery.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;

SCENARIO("DIN SECC PowerDelivery state handling") {
    const dt::SessionId session{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("A power delivery request (ready to charge)") {
        message_din::PowerDeliveryRequest req;
        req.header.session_id = session;
        req.ready_to_charge_state = true;

        const auto res = din::state::handle_request(req, session);
        THEN("ResponseCode is OK and the DC EVSE status is present") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_status.has_value());
            REQUIRE(res.dc_evse_status->evse_status_code == dt::DcEvseStatusCode::EVSE_Ready);
        }
    }

    GIVEN("A mismatching session id") {
        message_din::PowerDeliveryRequest req;
        req.header.session_id = dt::SessionId{};

        const auto res = din::state::handle_request(req, session);
        THEN("ResponseCode is FAILED_UnknownSession") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
        }
    }
}
