// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/state/cable_check.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;

SCENARIO("DIN SECC CableCheck state handling") {
    const dt::SessionId session{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("Cable check ongoing") {
        message_din::CableCheckRequest req;
        req.header.session_id = session;

        const auto res = din::state::handle_request(req, false, session);
        THEN("EVSEProcessing is Ongoing, isolation monitoring active") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EvseProcessing::Ongoing);
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_IsolationMonitoringActive);
        }
    }

    GIVEN("Cable check finished") {
        message_din::CableCheckRequest req;
        req.header.session_id = session;

        const auto res = din::state::handle_request(req, true, session);
        THEN("EVSEProcessing is Finished with a valid isolation and EVSE_Ready") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EvseProcessing::Finished);
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Ready);
            REQUIRE(res.dc_evse_status.evse_isolation_status.has_value());
            REQUIRE(res.dc_evse_status.evse_isolation_status.value() == dt::IsolationLevel::Valid);
        }
    }

    GIVEN("A mismatching session id") {
        message_din::CableCheckRequest req;
        req.header.session_id = dt::SessionId{};

        const auto res = din::state::handle_request(req, true, session);
        THEN("ResponseCode is FAILED_UnknownSession") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
        }
    }
}
