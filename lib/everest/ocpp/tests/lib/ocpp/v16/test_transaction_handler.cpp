// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <ocpp/v16/transaction.hpp>

TEST(TransactionHandlerTest, StoppedTransactionsSurviveConcurrentStopsAndLookups) {
    constexpr std::int32_t number_of_connectors = 2;
    constexpr int iterations = 20000;
    ocpp::v16::TransactionHandler handler(number_of_connectors);
    const auto make_transaction = [](std::int32_t connector, const std::string& stop_message_id) {
        auto transaction = std::make_shared<ocpp::v16::Transaction>(
            connector, connector, "session-" + std::to_string(connector), ocpp::CiString<20>("TAG"), 0.0, std::nullopt,
            ocpp::DateTime(), nullptr);
        transaction->set_stop_transaction_message_id(stop_message_id);
        return transaction;
    };
    std::atomic<bool> stopping{false};

    std::thread lookup([&]() {
        while (not stopping) {
            handler.get_transaction(std::string("unknown"));
            handler.get_transaction_from_id_tag("unknown");
        }
    });

    std::thread stop_connector_2([&]() {
        for (int i = 0; i < iterations; i++) {
            const auto message_id = "stop-2-" + std::to_string(i);
            handler.add_transaction(make_transaction(2, message_id));
            handler.add_stopped_transaction(2);
            handler.erase_stopped_transaction(message_id);
        }
    });

    for (int i = 0; i < iterations; i++) {
        const auto message_id = "stop-1-" + std::to_string(i);
        handler.add_transaction(make_transaction(1, message_id));
        handler.add_stopped_transaction(1);
        handler.erase_stopped_transaction(message_id);
    }

    stop_connector_2.join();
    stopping = true;
    lookup.join();

    EXPECT_EQ(handler.get_transaction_from_id_tag("TAG"), nullptr);
}
