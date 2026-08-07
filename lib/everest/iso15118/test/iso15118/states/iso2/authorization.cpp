// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/state/authorization.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("ISO 15118-2 SECC Authorization (EIM) handling") {
    const dt::SessionId id{};
    message_2::AuthorizationRequest req;

    GIVEN("Not yet authorized (EIM)") {
        // [V2G2-854]: EIM pending -> Ongoing_WaitingForCustomerInteraction (contract_selected = false).
        const auto res = d2::state::handle_request(req, id, false, false, false, false);
        THEN("OK, EVSEProcessing Ongoing_WaitingForCustomerInteraction") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EVSEProcessing::Ongoing_WaitingForCustomerInteraction);
        }
    }

    GIVEN("Not yet authorized (PnC / Contract)") {
        // [V2G2-855]: PnC pending -> Ongoing (contract_selected = true).
        const auto res = d2::state::handle_request(req, id, false, false, false, true);
        THEN("OK, EVSEProcessing Ongoing") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EVSEProcessing::Ongoing);
        }
    }

    GIVEN("Authorized") {
        const auto res = d2::state::handle_request(req, id, true, false);
        THEN("OK, EVSEProcessing Finished") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EVSEProcessing::Finished);
        }
    }

    GIVEN("PnC rejected because the contract certificate is revoked") {
        const auto res = d2::state::handle_request(req, id, false, false, true, true, true);
        THEN("FAILED_CertificateRevoked names the reason [V2G2-475]") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_CertificateRevoked);
            REQUIRE(res.evse_processing == dt::EVSEProcessing::Finished);
        }
    }

    GIVEN("PnC rejected for another reason") {
        const auto res = d2::state::handle_request(req, id, false, false, true, true, false);
        THEN("A plain FAILED") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
        }
    }

    GIVEN("EIM rejected with a stray revoked flag") {
        // The certificate status only means something for a Contract session.
        const auto res = d2::state::handle_request(req, id, false, false, true, false, true);
        THEN("A plain FAILED") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
        }
    }

    GIVEN("Ongoing timeout reached") {
        const auto res = d2::state::handle_request(req, id, false, true);
        THEN("FAILED") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
        }
    }

    GIVEN("Authorization rejected") {
        const auto res = d2::state::handle_request(req, id, false, false, true);
        THEN("FAILED and Finished (not Ongoing forever)") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(res.evse_processing == dt::EVSEProcessing::Finished);
        }
    }
}
