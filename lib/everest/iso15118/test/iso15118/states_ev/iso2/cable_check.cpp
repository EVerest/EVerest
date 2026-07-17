// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/ev/state/cable_check.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;
namespace cable_check = d2::ev::state::cable_check;

namespace {
message_2::CableCheckResponse make_finished_response(dt::DC_EVSEStatusCode status_code,
                                                     std::optional<dt::IsolationLevel> isolation) {
    message_2::CableCheckResponse res;
    res.response_code = dt::ResponseCode::OK;
    res.evse_processing = dt::EVSEProcessing::Finished;
    res.dc_evse_status.status_code = status_code;
    res.dc_evse_status.isolation_status = isolation;
    return res;
}
} // namespace

SCENARIO("EVCC ISO-2 CableCheck request/response handling") {

    GIVEN("A request built from a DC_EVStatus with the cached SoC") {
        dt::DC_EVStatus status;
        status.ev_ready = true;
        status.ev_ress_soc = 42;
        const auto req = cable_check::create_request(status);
        THEN("The EV reports ready with no error and the SoC is echoed") {
            REQUIRE(req.dc_ev_status.ev_ready);
            REQUIRE(req.dc_ev_status.ev_error_code == dt::DC_EVErrorCode::NO_ERROR);
            REQUIRE(req.dc_ev_status.ev_ress_soc == 42);
        }
    }

    GIVEN("An Ongoing response") {
        message_2::CableCheckResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EVSEProcessing::Ongoing;
        const auto result = cable_check::handle_response(res);
        THEN("The action is Retry") {
            REQUIRE(result.action == cable_check::Action::Retry);
        }
    }

    GIVEN("A Finished response with EVSE_Ready and Valid isolation") {
        const auto result =
            cable_check::handle_response(make_finished_response(dt::DC_EVSEStatusCode::EVSE_Ready, dt::IsolationLevel::Valid));
        THEN("It is Done and safe to proceed") {
            REQUIRE(result.action == cable_check::Action::Done);
            REQUIRE(result.evse_ready);
            REQUIRE(result.isolation_ok);
        }
    }

    GIVEN("A Finished response with EVSE_Ready and Warning isolation") {
        const auto result = cable_check::handle_response(
            make_finished_response(dt::DC_EVSEStatusCode::EVSE_Ready, dt::IsolationLevel::Warning));
        THEN("Warning is accepted (EvseV2G parity)") {
            REQUIRE(result.action == cable_check::Action::Done);
            REQUIRE(result.evse_ready);
            REQUIRE(result.isolation_ok);
        }
    }

    GIVEN("A Finished response with Invalid isolation") {
        const auto result = cable_check::handle_response(
            make_finished_response(dt::DC_EVSEStatusCode::EVSE_Ready, dt::IsolationLevel::Invalid));
        THEN("Isolation is not ok, so the caller must stop") {
            REQUIRE(result.action == cable_check::Action::Done);
            REQUIRE(not result.isolation_ok);
        }
    }

    GIVEN("A Finished response with Fault isolation") {
        const auto result = cable_check::handle_response(
            make_finished_response(dt::DC_EVSEStatusCode::EVSE_Ready, dt::IsolationLevel::Fault));
        THEN("Isolation is not ok") {
            REQUIRE(not result.isolation_ok);
        }
    }

    GIVEN("A Finished response with absent isolation") {
        const auto result =
            cable_check::handle_response(make_finished_response(dt::DC_EVSEStatusCode::EVSE_Ready, std::nullopt));
        THEN("Absent isolation is not ok") {
            REQUIRE(not result.isolation_ok);
        }
    }

    GIVEN("A Finished response with EVSE_NotReady but Valid isolation") {
        const auto result = cable_check::handle_response(
            make_finished_response(dt::DC_EVSEStatusCode::EVSE_NotReady, dt::IsolationLevel::Valid));
        THEN("EVSE is not ready, so the caller must stop") {
            REQUIRE(result.action == cable_check::Action::Done);
            REQUIRE(not result.evse_ready);
            REQUIRE(result.isolation_ok);
        }
    }

    GIVEN("A failed response") {
        message_2::CableCheckResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        res.evse_processing = dt::EVSEProcessing::Finished;
        const auto result = cable_check::handle_response(res);
        THEN("The action is Failed") {
            REQUIRE(result.action == cable_check::Action::Failed);
        }
    }
}
