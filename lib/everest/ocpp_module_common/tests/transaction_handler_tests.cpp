// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <chrono>

#include <everest/ocpp_module_common/transaction_handler.hpp>

namespace ocpp_module_common {

class TransactionHandlerTest : public ::testing::Test {

protected:
    void SetUp() override {
    }

    void TearDown() override {
    }

    std::shared_ptr<TransactionData> transaction_data() {
        return std::make_shared<TransactionData>(1, "123", ocpp::DateTime(), ocpp::v2::TriggerReasonEnum::Authorized,
                                                 ocpp::v2::ChargingStateEnum::Idle);
    }
};

TEST_F(TransactionHandlerTest, test_authorized) {
    TransactionHandler transaction_handler(2, {TxStartStopPoint::Authorized}, {TxStartStopPoint::Authorized});

    transaction_handler.add_transaction_data(1, transaction_data());

    auto res = transaction_handler.submit_event(1, TxEvent::AUTHORIZED);
    ASSERT_EQ(res, TxEventEffect::START_TRANSACTION);
    transaction_handler.get_transaction_data(1)->started = true;

    res = transaction_handler.submit_event(1, TxEvent::AUTHORIZED);
    ASSERT_EQ(res, TxEventEffect::NONE);

    res = transaction_handler.submit_event(1, TxEvent::DEAUTHORIZED);
    ASSERT_EQ(res, TxEventEffect::STOP_TRANSACTION);
}

TEST_F(TransactionHandlerTest, test_power_path_closed) {
    TransactionHandler transaction_handler(2, {TxStartStopPoint::PowerPathClosed}, {TxStartStopPoint::PowerPathClosed});

    transaction_handler.add_transaction_data(1, transaction_data());

    auto res = transaction_handler.submit_event(1, TxEvent::AUTHORIZED);
    ASSERT_EQ(res, TxEventEffect::NONE);

    res = transaction_handler.submit_event(1, TxEvent::EV_CONNECTED);
    ASSERT_EQ(res, TxEventEffect::START_TRANSACTION);

    transaction_handler.get_transaction_data(1)->started = true;

    res = transaction_handler.submit_event(1, TxEvent::PARKING_BAY_UNOCCUPIED);
    ASSERT_EQ(res, TxEventEffect::NONE);

    res = transaction_handler.submit_event(1, TxEvent::EV_CONNECTED);
    ASSERT_EQ(res, TxEventEffect::NONE);

    res = transaction_handler.submit_event(1, TxEvent::EV_DISCONNECTED);
    ASSERT_EQ(res, TxEventEffect::STOP_TRANSACTION);
}

TEST_F(TransactionHandlerTest, test_ev_connected) {
    TransactionHandler transaction_handler(2, {TxStartStopPoint::EVConnected}, {TxStartStopPoint::EVConnected});

    transaction_handler.add_transaction_data(1, transaction_data());

    auto res = transaction_handler.submit_event(1, TxEvent::EV_CONNECTED);
    ASSERT_EQ(res, TxEventEffect::START_TRANSACTION);
    transaction_handler.get_transaction_data(1)->started = true;

    res = transaction_handler.submit_event(1, TxEvent::DEAUTHORIZED);
    ASSERT_EQ(res, TxEventEffect::NONE);

    res = transaction_handler.submit_event(1, TxEvent::EV_DISCONNECTED);
    ASSERT_EQ(res, TxEventEffect::STOP_TRANSACTION);
}

TEST_F(TransactionHandlerTest, test_parking_bay_occupied) {
    TransactionHandler transaction_handler(2, {TxStartStopPoint::ParkingBayOccupancy},
                                           {TxStartStopPoint::ParkingBayOccupancy});

    transaction_handler.add_transaction_data(1, transaction_data());

    auto res = transaction_handler.submit_event(1, TxEvent::PARKING_BAY_OCCUPIED);
    ASSERT_EQ(res, TxEventEffect::START_TRANSACTION);
    transaction_handler.get_transaction_data(1)->started = true;

    res = transaction_handler.submit_event(1, TxEvent::EV_DISCONNECTED);
    ASSERT_EQ(res, TxEventEffect::NONE);

    res = transaction_handler.submit_event(1, TxEvent::PARKING_BAY_UNOCCUPIED);
    ASSERT_EQ(res, TxEventEffect::STOP_TRANSACTION);
}

TEST_F(TransactionHandlerTest, test_multiple) {
    TransactionHandler transaction_handler(2, {TxStartStopPoint::EVConnected, TxStartStopPoint::Authorized},
                                           {TxStartStopPoint::PowerPathClosed});
    transaction_handler.add_transaction_data(1, transaction_data());

    auto res = transaction_handler.submit_event(1, TxEvent::EV_CONNECTED);
    ASSERT_EQ(res, TxEventEffect::START_TRANSACTION);
    transaction_handler.get_transaction_data(1)->started = true;

    res = transaction_handler.submit_event(1, TxEvent::DEAUTHORIZED);
    ASSERT_EQ(res, TxEventEffect::STOP_TRANSACTION);

    transaction_handler.reset_transaction_data(1);

    transaction_handler.add_transaction_data(1, transaction_data());

    res = transaction_handler.submit_event(1, TxEvent::AUTHORIZED);
    ASSERT_EQ(res, TxEventEffect::START_TRANSACTION);
    transaction_handler.get_transaction_data(1)->started = true;

    res = transaction_handler.submit_event(1, TxEvent::SIGNED_START_DATA_RECEIVED);
    ASSERT_EQ(res, TxEventEffect::NONE);

    res = transaction_handler.submit_event(1, TxEvent::EV_DISCONNECTED);
    ASSERT_EQ(res, TxEventEffect::STOP_TRANSACTION);
}

class EvConnectTimeoutTest : public TransactionHandlerTest {
protected:
    static constexpr std::chrono::seconds timeout{30};
    const std::chrono::steady_clock::time_point authorized_at{std::chrono::seconds(1000)};
    TransactionHandler transaction_handler{2, {TxStartStopPoint::Authorized}, {TxStartStopPoint::Authorized}};

    void SetUp() override {
        transaction_handler.add_transaction_data(1, transaction_data());
        ASSERT_EQ(transaction_handler.submit_event(1, TxEvent::AUTHORIZED, authorized_at),
                  TxEventEffect::START_TRANSACTION);
        transaction_handler.get_transaction_data(1)->started = true;
    }
};

TEST_F(EvConnectTimeoutTest, at_timeout) {
    EXPECT_TRUE(transaction_handler.is_ev_connect_timeout(1, timeout, authorized_at + timeout));
}

TEST_F(EvConnectTimeoutTest, within_tolerance_before_timeout) {
    EXPECT_TRUE(
        transaction_handler.is_ev_connect_timeout(1, timeout, authorized_at + timeout - std::chrono::seconds(1)));
}

TEST_F(EvConnectTimeoutTest, beyond_tolerance_before_timeout) {
    EXPECT_FALSE(transaction_handler.is_ev_connect_timeout(1, timeout,
                                                           authorized_at + timeout - std::chrono::milliseconds(1500)));
}

TEST_F(EvConnectTimeoutTest, repeated_authorization_keeps_first_time) {
    transaction_handler.submit_event(1, TxEvent::AUTHORIZED, authorized_at + std::chrono::seconds(20));
    EXPECT_TRUE(transaction_handler.is_ev_connect_timeout(1, timeout, authorized_at + timeout));
}

TEST_F(EvConnectTimeoutTest, ev_connected) {
    transaction_handler.submit_event(1, TxEvent::EV_CONNECTED, authorized_at + std::chrono::seconds(5));
    EXPECT_FALSE(transaction_handler.is_ev_connect_timeout(1, timeout, authorized_at + timeout));
}

TEST_F(EvConnectTimeoutTest, no_transaction) {
    transaction_handler.reset_transaction_data(1);
    EXPECT_FALSE(transaction_handler.is_ev_connect_timeout(1, timeout, authorized_at + timeout));
    EXPECT_FALSE(transaction_handler.is_ev_connect_timeout(2, timeout, authorized_at + timeout));
}

TEST_F(EvConnectTimeoutTest, deauthorized_clears_authorization_time) {
    transaction_handler.submit_event(1, TxEvent::DEAUTHORIZED, authorized_at + std::chrono::seconds(10));
    EXPECT_FALSE(transaction_handler.is_ev_connect_timeout(1, timeout, authorized_at + timeout));

    const auto reauthorized_at = authorized_at + std::chrono::seconds(20);
    transaction_handler.submit_event(1, TxEvent::AUTHORIZED, reauthorized_at);
    EXPECT_FALSE(transaction_handler.is_ev_connect_timeout(1, timeout, authorized_at + timeout));
    EXPECT_TRUE(transaction_handler.is_ev_connect_timeout(1, timeout, reauthorized_at + timeout));
}

TEST_F(TransactionHandlerTest, test_invalid_params) {
    TransactionHandler transaction_handler(2, {TxStartStopPoint::EVConnected, TxStartStopPoint::Authorized},
                                           {TxStartStopPoint::PowerPathClosed});
    ASSERT_THROW(transaction_handler.get_transaction_data(3), std::out_of_range);
    ASSERT_THROW(transaction_handler.reset_transaction_data(3), std::out_of_range);
    ASSERT_THROW(transaction_handler.add_transaction_data(-1, transaction_data()), std::out_of_range);
    ASSERT_THROW(transaction_handler.add_transaction_data(3, transaction_data()), std::out_of_range);
}

} // namespace ocpp_module_common
