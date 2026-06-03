// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// cmds:
//   reserve_now:
//   cancel_reservation:
//   exists_reservation:
//
// vars:
//   reservation_update:

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;

TEST_F(GenericOcppRequiresTester, callReserveNow) {
    // call_reserve_now() used in cb_reserve_now()

    using ocpp::DateTime;
    using ocpp::v2::IdToken;
    using ocpp::v2::ReserveNowRequest;
    using ocpp::v2::ReserveNowStatusEnum;

    std::vector<json> received;
    interfaces->subscribe_var("reservation", "call_reserve_now",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    IdToken token;
    token.idToken = "12345678";
    token.type = "RFID";
    // std::optional<std::vector<AdditionalInfo>> additionalInfo;
    // std::optional<CustomData> customData;

    ReserveNowRequest request;
    request.id = 1845;
    request.expiryDateTime = DateTime{"2026-01-01T12:00:00Z"};
    request.idToken = token;
    // std::int32_t id;
    // ocpp::DateTime expiryDateTime;
    // IdToken idToken;
    // std::optional<CiString<20>> connectorType;
    // std::optional<std::int32_t> evseId;
    // std::optional<IdToken> groupIdToken;
    // std::optional<CustomData> customData;

    interfaces->add_cmd_result(R"("Occupied")"_json);
    // - Accepted
    // - Faulted
    // - Occupied
    // - Rejected
    // - Unavailable

    const auto result = ocpp->cb_reserve_now(request);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(
        received[0],
        R"({"request":{"expiry_time":"2026-01-01T12:00:00.000Z","id_token":"12345678","reservation_id":1845}})"_json);

    EXPECT_EQ(result, ReserveNowStatusEnum::Occupied);
}

TEST_F(GenericOcppRequiresTester, callCancelReservation) {
    // call_cancel_reservation() used in cb_cancel_reservation()

    std::vector<json> received;
    interfaces->subscribe_var("reservation", "call_cancel_reservation",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    interfaces->add_cmd_result(R"(true)"_json);

    const auto result = ocpp->cb_cancel_reservation(1845);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], R"({"reservation_id":1845})"_json);

    EXPECT_TRUE(result);
}

TEST_F(GenericOcppRequiresTester, callExistsReservation) {
    // call_exists_reservation() used in cb_is_reservation_for_token()

    using ocpp::ReservationCheckStatus;

    std::vector<json> received;
    interfaces->subscribe_var("reservation", "call_exists_reservation",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    interfaces->add_cmd_result(R"("ReservedForToken")"_json);
    // NotReserved
    // ReservedForToken
    // ReservedForOtherToken
    // ReservedForOtherTokenAndHasParentToken

    const auto result = ocpp->cb_is_reservation_for_token(1, "12345678", std::nullopt);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], R"({"request":{"evse_id":1,"id_token":"12345678"}})"_json);

    EXPECT_EQ(result, ReservationCheckStatus::ReservedForToken);
}

TEST_F(GenericOcppRequiresTester, subscribeSupportedEnergyTransferModes) {
    // subscribe_reservation_update() calls cb_reservation_update()

    using ocpp::v2::ReservationUpdateStatusEnum;
    using types::reservation::Reservation_status;
    using types::reservation::ReservationUpdateStatus;

    ReservationUpdateStatus status;
    status.reservation_id = 12358;
    status.reservation_status = Reservation_status::Expired;

    EXPECT_CALL(chargepoint, on_reservation_status(status.reservation_id, ReservationUpdateStatusEnum::Expired))
        .Times(1);

    interfaces->publish(0, "reservation_update", status);
}

} // namespace
