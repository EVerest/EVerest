// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/ev/state/cable_check.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;
using namespace din::ev::state::cable_check;

SCENARIO("EVCC DIN CableCheck request/response handling") {
    GIVEN("A request with EVReady=true") {
        dt::DcEvStatus status;
        status.ev_ready = true;
        const auto req = create_request(status);
        THEN("EVReady is set") {
            REQUIRE(req.dc_ev_status.ev_ready);
        }
    }

    GIVEN("An Ongoing response") {
        message_din::CableCheckResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Ongoing;
        THEN("The action is Retry") {
            REQUIRE(handle_response(res).action == Action::Retry);
        }
    }

    GIVEN("A Finished response with EVSE ready and valid isolation") {
        message_din::CableCheckResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
        const auto result = handle_response(res);
        THEN("Done with ready and valid isolation") {
            REQUIRE(result.action == Action::Done);
            REQUIRE(result.evse_ready);
            REQUIRE(result.isolation_valid);
        }
    }

    GIVEN("A Finished response with warning isolation") {
        message_din::CableCheckResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Warning;
        const auto result = handle_response(res);
        THEN("Done and isolation is accepted (EvseV2G proceeds on Warning)") {
            REQUIRE(result.action == Action::Done);
            REQUIRE(result.evse_ready);
            REQUIRE(result.isolation_valid);
        }
    }

    GIVEN("A Finished response with fault isolation") {
        message_din::CableCheckResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Fault;
        const auto result = handle_response(res);
        THEN("Done but isolation is not valid") {
            REQUIRE(result.action == Action::Done);
            REQUIRE(not result.isolation_valid);
        }
    }

    GIVEN("A Finished response with invalid isolation") {
        message_din::CableCheckResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Invalid;
        const auto result = handle_response(res);
        THEN("Done but isolation is not valid") {
            REQUIRE(result.action == Action::Done);
            REQUIRE(not result.isolation_valid);
        }
    }

    GIVEN("A failed response") {
        message_din::CableCheckResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        THEN("The action is Failed") {
            REQUIRE(handle_response(res).action == Action::Failed);
        }
    }
}
