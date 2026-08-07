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

    GIVEN("Cable check reports an isolation fault") {
        // [V2G-DC-890] parity: a finished-but-failed cable check answers FAILED with Invalid isolation.
        const auto res = d2::state::handle_request(req, id, /*cable_check_done=*/false, /*cable_check_fault=*/true);
        THEN("FAILED, isolation Invalid, EVSE_Malfunction") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(res.dc_evse_status.isolation_status.value() == dt::IsolationLevel::Invalid);
            REQUIRE(res.dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Malfunction);
        }
    }

    GIVEN("A module-reported EVSE error while the cable check is ongoing") {
        // An active send_error status override wins over EVSE_IsolationMonitoringActive (EvseV2G parity).
        const auto res = d2::state::handle_request(req, id, /*cable_check_done=*/false, /*cable_check_fault=*/false,
                                                   dt::DC_EVSEStatusCode::EVSE_UtilityInterruptEvent);
        THEN("the status code is overridden while the response stays OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_UtilityInterruptEvent);
        }
    }

    GIVEN("A charger without an insulation monitoring device") {
        // EvseManager reports No_IMD and then cable_check_finished(true) straight away; the module value
        // must reach the EV instead of the Valid the progress derivation would produce on its own.
        const auto res = d2::state::handle_request(req, id, /*cable_check_done=*/true, /*cable_check_fault=*/false,
                                                   std::nullopt, /*charger_stop=*/false, dt::IsolationLevel::No_IMD);
        THEN("CableCheckRes reports No_IMD") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EVSEProcessing::Finished);
            REQUIRE(res.dc_evse_status.isolation_status.value() == dt::IsolationLevel::No_IMD);
            REQUIRE(res.dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Ready);
        }
    }

    GIVEN("A module reporting an isolation warning at the end of the cable check") {
        const auto res = d2::state::handle_request(req, id, /*cable_check_done=*/true, /*cable_check_fault=*/false,
                                                   std::nullopt, /*charger_stop=*/false, dt::IsolationLevel::Warning);
        THEN("CableCheckRes reports Warning rather than a plain Valid") {
            REQUIRE(res.dc_evse_status.isolation_status.value() == dt::IsolationLevel::Warning);
        }
    }

    GIVEN("A module reporting Fault together with a failed cable check") {
        const auto res = d2::state::handle_request(req, id, /*cable_check_done=*/false, /*cable_check_fault=*/true,
                                                   std::nullopt, /*charger_stop=*/false, dt::IsolationLevel::Fault);
        THEN("the negative CableCheckRes carries the reported Fault level") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(res.dc_evse_status.isolation_status.value() == dt::IsolationLevel::Fault);
            REQUIRE(res.dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Malfunction);
        }
    }

    GIVEN("An EVSE-initiated stop during the cable check") {
        // The stop request reaches the EV in every state (EvseV2G parity): notification StopCharging and
        // EVSE_Shutdown, whatever the cable check's own progress.
        const auto res = d2::state::handle_request(req, id, /*cable_check_done=*/false, /*cable_check_fault=*/false,
                                                   std::nullopt, /*charger_stop=*/true);
        THEN("the response signals EVSENotification StopCharging and EVSE_Shutdown") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_status.notification == dt::EVSENotification::StopCharging);
            REQUIRE(res.dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Shutdown);
        }
    }
}
