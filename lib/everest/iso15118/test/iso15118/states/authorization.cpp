// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/state/authorization.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

using AuthStatus = message_20::datatypes::AuthStatus;

SCENARIO("Authorization state handling") {

    GIVEN("Bad Case - Unknown session") {
        d20::Session session = d20::Session();

        message_20::AuthorizationRequest req;
        req.header.session_id = session.get_id();
        req.header.timestamp = 1691411798;

        req.selected_authorization_service = dt::Authorization::EIM;
        req.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();

        const auto res = d20::state::handle_request(req, d20::Session(), AuthStatus::Pending, false);

        THEN("ResponseCode: FAILED_UnknownSession, mandatory fields should be set") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(res.evse_processing == dt::Processing::Finished);
        }
    }

    GIVEN("Warning - Authorization selection is invalid") {

        d20::Session session = d20::Session();
        session.offered_services.auth_services = {dt::Authorization::PnC};

        message_20::AuthorizationRequest req;
        req.header.session_id = session.get_id();
        req.header.timestamp = 1691411798;

        req.selected_authorization_service = dt::Authorization::EIM;
        req.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();

        const auto res = d20::state::handle_request(req, session, AuthStatus::Pending, false);

        THEN("ResponseCode: FAILED_UnknownSession, EvseProcessing: Finished") {
            REQUIRE(res.response_code == dt::ResponseCode::WARNING_AuthorizationSelectionInvalid);
            REQUIRE(res.evse_processing == dt::Processing::Finished);
        }
    }

    // EIM test cases

    GIVEN("Warning - EIM Authorization Failure") { // [V2G20-2219]

        d20::Session session = d20::Session();
        session.offered_services.auth_services = {dt::Authorization::EIM, dt::Authorization::PnC};

        message_20::AuthorizationRequest req;
        req.header.session_id = session.get_id();
        req.header.timestamp = 1691411798;

        req.selected_authorization_service = dt::Authorization::EIM;
        req.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();

        const auto res = d20::state::handle_request(req, session, AuthStatus::Rejected, false);

        THEN("ResponseCode: WARNING_EIMAuthorizationFailure, EvseProcessing: Finished") {
            REQUIRE(res.response_code == dt::ResponseCode::WARNING_EIMAuthorizationFailure);
            REQUIRE(res.evse_processing == dt::Processing::Finished);
        }
    }

    GIVEN("Good case - EIM waiting for authorization") {

        d20::Session session = d20::Session();
        session.offered_services.auth_services = {dt::Authorization::EIM, dt::Authorization::PnC};

        message_20::AuthorizationRequest req;
        req.header.session_id = session.get_id();
        req.header.timestamp = 1691411798;

        req.selected_authorization_service = dt::Authorization::EIM;
        req.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();

        const auto res = d20::state::handle_request(req, session, AuthStatus::Pending, false);

        THEN("ResponseCode: Ok, EvseProcessing: Ongoing") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::Processing::Ongoing);
        }
    }

    GIVEN("Good case - EIM authorized") {

        d20::Session session = d20::Session();
        session.offered_services.auth_services = {dt::Authorization::EIM, dt::Authorization::PnC};

        message_20::AuthorizationRequest req;
        req.header.session_id = session.get_id();
        req.header.timestamp = 1691411798;

        req.selected_authorization_service = dt::Authorization::EIM;
        req.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();

        const auto res = d20::state::handle_request(req, session, AuthStatus::Accepted, false);

        THEN("ResponseCode: Ok, EvseProcessing: Finished") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::Processing::Finished);
        }
    }

    GIVEN("Bad Case - Ongoing timeout reached") {

        d20::Session session = d20::Session();
        session.offered_services.auth_services = {dt::Authorization::EIM, dt::Authorization::PnC};

        message_20::AuthorizationRequest req;
        req.header.session_id = session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_authorization_service = dt::Authorization::EIM;
        req.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();

        const auto res = d20::state::handle_request(req, session, AuthStatus::Pending, true);

        THEN("ResponseCode: FAILED") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
        }
    }

    // PnC test cases

    GIVEN("PnC - the outcome of the checks drives the response") {

        d20::Session session = d20::Session();
        session.offered_services.auth_services = {dt::Authorization::EIM, dt::Authorization::PnC};

        message_20::AuthorizationRequest req;
        req.header.session_id = session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_authorization_service = dt::Authorization::PnC;
        req.authorization_mode.emplace<dt::PnC_ASReqAuthorizationMode>();

        THEN("Pending backend: OK, Ongoing") {
            const auto res =
                d20::state::handle_request(req, session, AuthStatus::Pending, false,
                                           d20::state::PncOutcome{dt::ResponseCode::OK, dt::Processing::Ongoing});
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::Processing::Ongoing);
        }

        THEN("Expiring soon: OK_CertificateExpiresSoon, Finished") {
            const auto res = d20::state::handle_request(
                req, session, AuthStatus::Accepted, false,
                d20::state::PncOutcome{dt::ResponseCode::OK_CertificateExpiresSoon, dt::Processing::Finished});
            REQUIRE(res.response_code == dt::ResponseCode::OK_CertificateExpiresSoon);
            REQUIRE(res.evse_processing == dt::Processing::Finished);
        }

        THEN("Challenge mismatch: WARNING_ChallengeInvalid, Finished") {
            const auto res = d20::state::handle_request(
                req, session, AuthStatus::Pending, false,
                d20::state::PncOutcome{dt::ResponseCode::WARNING_ChallengeInvalid, dt::Processing::Finished});
            REQUIRE(res.response_code == dt::ResponseCode::WARNING_ChallengeInvalid);
            REQUIRE(res.evse_processing == dt::Processing::Finished);
        }

        THEN("Without an outcome the request is not authorized") {
            const auto res = d20::state::handle_request(req, session, AuthStatus::Pending, false);
            REQUIRE(res.response_code == dt::ResponseCode::WARNING_GeneralPnCAuthorizationError);
            REQUIRE(res.evse_processing == dt::Processing::Finished);
        }
    }

    GIVEN("PnC - backend rejections map onto the -20 WARNING codes") { // [V2G20-2210..2217]
        using d20::CertificateStatus;
        const auto code = [](CertificateStatus status, bool token_unknown = false) {
            return d20::state::pnc_rejection_code(d20::AuthorizationResponse{false, status, token_unknown});
        };

        THEN("Certificate status and unknown token are distinguished") {
            REQUIRE(code(CertificateStatus::CertificateExpired) == dt::ResponseCode::WARNING_CertificateExpired);
            REQUIRE(code(CertificateStatus::CertificateRevoked) == dt::ResponseCode::WARNING_CertificateRevoked);
            REQUIRE(code(CertificateStatus::CertChainError) == dt::ResponseCode::WARNING_CertificateValidationError);
            REQUIRE(code(CertificateStatus::SignatureError) == dt::ResponseCode::WARNING_CertificateValidationError);
            REQUIRE(code(CertificateStatus::NoCertificateAvailable) ==
                    dt::ResponseCode::WARNING_CertificateValidationError);
            REQUIRE(code(CertificateStatus::ContractCancelled) ==
                    dt::ResponseCode::WARNING_GeneralPnCAuthorizationError);
            REQUIRE(code(CertificateStatus::Accepted) == dt::ResponseCode::WARNING_GeneralPnCAuthorizationError);
            REQUIRE(code(CertificateStatus::Accepted, true) == dt::ResponseCode::WARNING_eMSPUnknown);
            REQUIRE(d20::AuthorizationResponse{false, true}.is_certificate_revoked());
        }
    }
}
