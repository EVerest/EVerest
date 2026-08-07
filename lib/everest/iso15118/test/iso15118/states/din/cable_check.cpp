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

        const auto res = din::state::handle_request(req, false, false, session);
        THEN("EVSEProcessing is Ongoing, isolation monitoring active") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EvseProcessing::Ongoing);
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_IsolationMonitoringActive);
        }
    }

    GIVEN("Cable check reports an isolation fault") {
        message_din::CableCheckRequest req;
        req.header.session_id = session;
        // [V2G-DC-890]: a finished-but-failed cable check answers FAILED with Invalid isolation.
        const auto res =
            din::state::handle_request(req, /*cable_check_done=*/false, /*cable_check_fault=*/true, session);
        THEN("FAILED, isolation Invalid, EVSE_Malfunction") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(res.dc_evse_status.evse_isolation_status == dt::IsolationLevel::Invalid);
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Malfunction);
        }
    }

    GIVEN("Cable check finished") {
        message_din::CableCheckRequest req;
        req.header.session_id = session;

        const auto res = din::state::handle_request(req, true, false, session);
        THEN("EVSEProcessing is Finished with a valid isolation and EVSE_Ready") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EvseProcessing::Finished);
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Ready);
            REQUIRE(res.dc_evse_status.evse_isolation_status.has_value());
            REQUIRE(res.dc_evse_status.evse_isolation_status.value() == dt::IsolationLevel::Valid);
        }
    }

    GIVEN("A module reporting an isolation warning at the end of the cable check") {
        message_din::CableCheckRequest req;
        req.header.session_id = session;
        // The module value wins over the level derived from the cable check's own progress. DIN has no
        // No_IMD enumerator, so a charger without an IMD arrives here already mapped to Valid.
        const auto res =
            din::state::handle_request(req, /*cable_check_done=*/true, /*cable_check_fault=*/false, session,
                                       std::nullopt, /*charger_stop=*/false, dt::IsolationLevel::Warning);
        THEN("CableCheckRes reports Warning rather than a plain Valid") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_status.evse_isolation_status.value() == dt::IsolationLevel::Warning);
        }
    }

    GIVEN("A module reporting Fault together with a failed cable check") {
        message_din::CableCheckRequest req;
        req.header.session_id = session;
        const auto res =
            din::state::handle_request(req, /*cable_check_done=*/false, /*cable_check_fault=*/true, session,
                                       std::nullopt, /*charger_stop=*/false, dt::IsolationLevel::Fault);
        THEN("the negative CableCheckRes carries the reported Fault level") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(res.dc_evse_status.evse_isolation_status.value() == dt::IsolationLevel::Fault);
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Malfunction);
        }
    }

    GIVEN("A mismatching session id") {
        message_din::CableCheckRequest req;
        req.header.session_id = dt::SessionId{};

        const auto res = din::state::handle_request(req, true, false, session);
        THEN("ResponseCode is FAILED_UnknownSession") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
        }
    }

    GIVEN("A module-reported EVSE error while the cable check is ongoing") {
        message_din::CableCheckRequest req;
        req.header.session_id = session;
        // An active send_error status override wins over EVSE_IsolationMonitoringActive (EvseV2G parity).
        const auto res = din::state::handle_request(req, /*cable_check_done=*/false, /*cable_check_fault=*/false,
                                                    session, dt::DcEvseStatusCode::EVSE_UtilityInterruptEvent);
        THEN("the status code is overridden while the response stays OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_UtilityInterruptEvent);
        }
    }

    GIVEN("An EVSE-initiated stop during the cable check") {
        message_din::CableCheckRequest req;
        req.header.session_id = session;
        // The stop request reaches the EV in every state (EvseV2G parity): EVSE_Shutdown, whatever the
        // cable check's own progress. The EVSENotification stays None for DC [V2G-DC-500].
        const auto res = din::state::handle_request(req, /*cable_check_done=*/false, /*cable_check_fault=*/false,
                                                    session, std::nullopt, /*charger_stop=*/true);
        THEN("the response signals EVSE_Shutdown") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Shutdown);
            REQUIRE(res.dc_evse_status.evse_notification == dt::EvseNotification::None);
        }
    }
}
