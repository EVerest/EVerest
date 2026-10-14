// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include <connectivity_manager_mock.hpp>
#include <message_dispatcher_mock.hpp>
#include <test_temp_paths.hpp>

#include <ocpp/common/call_types.hpp>
#include <ocpp/v2/connection_id.hpp>
#include <ocpp/v2/connection_registry.hpp>
#include <ocpp/v2/connection_routing_dispatcher.hpp>
#include <ocpp/v2/connection_scope.hpp>
#include <ocpp/v2/database_handler.hpp>
#include <ocpp/v2/ocpp_connection.hpp>

using ::testing::_;
using ::testing::NiceMock;
using ::testing::SaveArg;

namespace ocpp {
namespace v2 {

namespace {
constexpr const char* SECONDARY_INSTANCE = "EMS";
constexpr const char* ORIGINAL_MESSAGE_ID = "original-message-id";

/// A CALL as the functional blocks build it: [MessageTypeId, messageId, action, payload].
json make_call(const std::string& action, const std::string& message_id = ORIGINAL_MESSAGE_ID) {
    return json::array({2, message_id, action, json{{"some"}, {"payload"}}});
}
} // namespace

/// Fixture wiring real OcppConnections around mock leaf dispatchers, so the routing decisions are
/// observed exactly where the bytes would leave for one connection's queue.
class ConnectionRoutingDispatcherTest : public ::testing::Test {
protected:
    ConnectionRegistry registry;
    ConnectionRoutingDispatcher dispatcher{registry};

    // Kept so the test can set expectations after the connections have been registered.
    MockMessageDispatcher* primary_leaf{nullptr};
    MockMessageDispatcher* secondary_leaf{nullptr};

    std::vector<std::shared_ptr<DatabaseHandler>> database_handlers;

    void add_primary() {
        this->primary_leaf = this->add_connection(ConnectionId::primary());
    }

    void add_secondary() {
        this->secondary_leaf = this->add_connection(ConnectionId::secondary(SECONDARY_INSTANCE));
    }

    /// Builds a connection with a real, never started message queue and a mock leaf dispatcher.
    /// \return the leaf dispatcher, owned by the connection.
    MockMessageDispatcher* add_connection(const ConnectionId& id) {
        auto database_handler =
            std::make_shared<DatabaseHandler>(std::make_unique<everest::db::sqlite::Connection>(
                                                  libocpp_test::unique_temp_path("connection_routing", ".db")),
                                              MIGRATION_FILES_LOCATION_V2);
        database_handler->open_connection();
        this->database_handlers.push_back(database_handler);

        auto message_queue = std::make_shared<MessageQueue<MessageType>>(
            [](const json&) { return true; }, MessageQueueConfig<MessageType>{}, database_handler);

        auto leaf = std::make_unique<NiceMock<MockMessageDispatcher>>();
        auto* leaf_ptr = leaf.get();

        this->registry.add(std::make_unique<OcppConnection>(
            id, nullptr, std::make_shared<NiceMock<ConnectivityManagerMock>>(), message_queue, std::move(leaf)));
        return leaf_ptr;
    }
};

// --- one connection: nothing may change ---------------------------------------------------------

TEST_F(ConnectionRoutingDispatcherTest, single_connection_forwards_a_call_verbatim) {
    add_primary();
    const auto call = make_call("Heartbeat");

    json forwarded;
    EXPECT_CALL(*primary_leaf, dispatch_call(_, false)).WillOnce(SaveArg<0>(&forwarded));

    dispatcher.dispatch_call(call);

    // Same body and, critically, the same messageId: with no secondary connection configured the
    // primary's traffic has to be indistinguishable from before.
    EXPECT_EQ(forwarded, call);
}

TEST_F(ConnectionRoutingDispatcherTest, single_connection_forwards_the_triggered_flag) {
    add_primary();
    EXPECT_CALL(*primary_leaf, dispatch_call(_, true));
    dispatcher.dispatch_call(make_call("StatusNotification"), true);
}

TEST_F(ConnectionRoutingDispatcherTest, responses_reach_the_primary_when_no_scope_is_open) {
    add_primary();
    EXPECT_CALL(*primary_leaf, dispatch_call_result(_));
    EXPECT_CALL(*primary_leaf, dispatch_call_error(_));

    dispatcher.dispatch_call_result(json::array({3, ORIGINAL_MESSAGE_ID, json::object()}));
    dispatcher.dispatch_call_error(json::array({4, ORIGINAL_MESSAGE_ID, "GenericError", "", json::object()}));
}

TEST_F(ConnectionRoutingDispatcherTest, responses_reach_the_primary_inside_a_primary_scope) {
    add_primary();
    EXPECT_CALL(*primary_leaf, dispatch_call_result(_));

    const ConnectionScope scope(ConnectionId::primary());
    dispatcher.dispatch_call_result(json::array({3, ORIGINAL_MESSAGE_ID, json::object()}));
}

// --- scope bookkeeping --------------------------------------------------------------------------

TEST_F(ConnectionRoutingDispatcherTest, connection_scope_is_empty_outside_inbound_handling) {
    EXPECT_FALSE(ConnectionScope::current().has_value());
}

TEST_F(ConnectionRoutingDispatcherTest, connection_scope_nests_and_restores) {
    {
        const ConnectionScope outer(ConnectionId::primary());
        EXPECT_EQ(ConnectionScope::current(), ConnectionId::primary());
        {
            const ConnectionScope inner(ConnectionId::secondary(SECONDARY_INSTANCE));
            EXPECT_EQ(ConnectionScope::current(), ConnectionId::secondary(SECONDARY_INSTANCE));
        }
        EXPECT_EQ(ConnectionScope::current(), ConnectionId::primary());
    }
    EXPECT_FALSE(ConnectionScope::current().has_value());
}

TEST_F(ConnectionRoutingDispatcherTest, target_scope_nests_and_restores) {
    {
        const ConnectionTargetScope outer(ConnectionId::primary());
        EXPECT_EQ(ConnectionTargetScope::current(), ConnectionId::primary());
        {
            const ConnectionTargetScope inner(ConnectionId::secondary(SECONDARY_INSTANCE));
            EXPECT_EQ(ConnectionTargetScope::current(), ConnectionId::secondary(SECONDARY_INSTANCE));
        }
        EXPECT_EQ(ConnectionTargetScope::current(), ConnectionId::primary());
    }
    EXPECT_FALSE(ConnectionTargetScope::current().has_value());
}

// --- two connections: fan out and origin routing ------------------------------------------------

TEST_F(ConnectionRoutingDispatcherTest, a_call_fans_out_to_every_connection) {
    add_primary();
    add_secondary();

    json to_primary;
    json to_secondary;
    EXPECT_CALL(*primary_leaf, dispatch_call(_, false)).WillOnce(SaveArg<0>(&to_primary));
    EXPECT_CALL(*secondary_leaf, dispatch_call(_, false)).WillOnce(SaveArg<0>(&to_secondary));

    // A message type the secondary connection is allowed to be sent; what it is allowed is the
    // subject of the allow list tests below.
    const auto call = make_call("Heartbeat");
    dispatcher.dispatch_call(call);

    EXPECT_EQ(to_primary, call);
    EXPECT_EQ(to_secondary.at(CALL_ACTION), call.at(CALL_ACTION));
    EXPECT_EQ(to_secondary.at(3), call.at(3));
}

TEST_F(ConnectionRoutingDispatcherTest, a_secondary_copy_gets_its_own_message_id) {
    add_primary();
    add_secondary();

    json to_primary;
    json to_secondary;
    EXPECT_CALL(*primary_leaf, dispatch_call(_, false)).WillOnce(SaveArg<0>(&to_primary));
    EXPECT_CALL(*secondary_leaf, dispatch_call(_, false)).WillOnce(SaveArg<0>(&to_secondary));

    dispatcher.dispatch_call(make_call("Heartbeat"));

    EXPECT_EQ(to_primary.at(MESSAGE_ID), ORIGINAL_MESSAGE_ID);
    EXPECT_NE(to_secondary.at(MESSAGE_ID), ORIGINAL_MESSAGE_ID);
    EXPECT_FALSE(to_secondary.at(MESSAGE_ID).get<std::string>().empty());
}

TEST_F(ConnectionRoutingDispatcherTest, a_response_goes_back_to_the_connection_that_asked) {
    add_primary();
    add_secondary();

    EXPECT_CALL(*primary_leaf, dispatch_call_result(_)).Times(0);
    EXPECT_CALL(*secondary_leaf, dispatch_call_result(_));

    const ConnectionScope scope(ConnectionId::secondary(SECONDARY_INSTANCE));
    dispatcher.dispatch_call_result(json::array({3, ORIGINAL_MESSAGE_ID, json::object()}));
}

TEST_F(ConnectionRoutingDispatcherTest, an_error_goes_back_to_the_connection_that_asked) {
    add_primary();
    add_secondary();

    EXPECT_CALL(*primary_leaf, dispatch_call_error(_)).Times(0);
    EXPECT_CALL(*secondary_leaf, dispatch_call_error(_));

    const ConnectionScope scope(ConnectionId::secondary(SECONDARY_INSTANCE));
    dispatcher.dispatch_call_error(json::array({4, ORIGINAL_MESSAGE_ID, "SecurityError", "", json::object()}));
}

TEST_F(ConnectionRoutingDispatcherTest, a_target_scope_pins_a_call_to_one_connection) {
    add_primary();
    add_secondary();

    json to_secondary;
    EXPECT_CALL(*primary_leaf, dispatch_call(_, _)).Times(0);
    EXPECT_CALL(*secondary_leaf, dispatch_call(_, false)).WillOnce(SaveArg<0>(&to_secondary));

    const auto call = make_call("Heartbeat");
    const ConnectionTargetScope target(ConnectionId::secondary(SECONDARY_INSTANCE));
    dispatcher.dispatch_call(call);

    // Pinned rather than copied, so it keeps the messageId it was built with.
    EXPECT_EQ(to_secondary, call);
}

TEST_F(ConnectionRoutingDispatcherTest, an_asynchronous_call_is_never_fanned_out) {
    add_primary();
    add_secondary();

    EXPECT_CALL(*primary_leaf, dispatch_call_async(_, _)).WillOnce([](const json&, bool) {
        std::promise<EnhancedMessage<MessageType>> promise;
        promise.set_value(EnhancedMessage<MessageType>{});
        return promise.get_future();
    });
    EXPECT_CALL(*secondary_leaf, dispatch_call_async(_, _)).Times(0);

    dispatcher.dispatch_call_async(make_call("DataTransfer"));
}

TEST_F(ConnectionRoutingDispatcherTest, an_asynchronous_call_follows_the_connection_being_handled) {
    add_primary();
    add_secondary();

    EXPECT_CALL(*primary_leaf, dispatch_call_async(_, _)).Times(0);
    EXPECT_CALL(*secondary_leaf, dispatch_call_async(_, _)).WillOnce([](const json&, bool) {
        std::promise<EnhancedMessage<MessageType>> promise;
        promise.set_value(EnhancedMessage<MessageType>{});
        return promise.get_future();
    });

    const ConnectionScope scope(ConnectionId::secondary(SECONDARY_INSTANCE));
    dispatcher.dispatch_call_async(make_call("DataTransfer"));
}

TEST_F(ConnectionRoutingDispatcherTest, an_unknown_target_drops_the_call_rather_than_misrouting_it) {
    add_primary();

    EXPECT_CALL(*primary_leaf, dispatch_call(_, _)).Times(0);

    const ConnectionTargetScope target(ConnectionId::secondary("NotRegistered"));
    dispatcher.dispatch_call(make_call("Heartbeat"));
}

TEST_F(ConnectionRoutingDispatcherTest, an_unknown_origin_answers_on_the_primary) {
    add_primary();

    EXPECT_CALL(*primary_leaf, dispatch_call_result(_));

    const ConnectionScope scope(ConnectionId::secondary("NotRegistered"));
    dispatcher.dispatch_call_result(json::array({3, ORIGINAL_MESSAGE_ID, json::object()}));
}

// --- what a secondary connection is allowed to be sent -----------------------------------------

TEST_F(ConnectionRoutingDispatcherTest, a_secondary_is_sent_nothing_it_was_not_granted) {
    add_primary();
    add_secondary();

    // Application note section 3.6: a secondary system gets nothing by default. Until an
    // UpstreamMsgs allow list says otherwise, station business never reaches it.
    EXPECT_CALL(*primary_leaf, dispatch_call(_, _)).Times(3);
    EXPECT_CALL(*secondary_leaf, dispatch_call(_, _)).Times(0);

    dispatcher.dispatch_call(make_call("TransactionEvent"));
    dispatcher.dispatch_call(make_call("MeterValues"));
    dispatcher.dispatch_call(make_call("StatusNotification"));
}

TEST_F(ConnectionRoutingDispatcherTest, a_secondary_is_still_sent_what_keeps_its_own_link_alive) {
    add_primary();
    add_secondary();

    // These are about the pipe rather than about the charging station's business, so a connection
    // that exists at all may send them: otherwise it could neither announce itself nor stay alive.
    EXPECT_CALL(*primary_leaf, dispatch_call(_, _)).Times(2);
    EXPECT_CALL(*secondary_leaf, dispatch_call(_, _)).Times(2);

    dispatcher.dispatch_call(make_call("BootNotification"));
    dispatcher.dispatch_call(make_call("Heartbeat"));
}

TEST_F(ConnectionRoutingDispatcherTest, a_pinned_call_still_respects_what_the_connection_may_send) {
    add_primary();
    add_secondary();

    EXPECT_CALL(*secondary_leaf, dispatch_call(_, _)).Times(0);

    const ConnectionTargetScope target(ConnectionId::secondary(SECONDARY_INSTANCE));
    dispatcher.dispatch_call(make_call("TransactionEvent"));
}

// --- registry -----------------------------------------------------------------------------------

TEST_F(ConnectionRoutingDispatcherTest, registry_orders_the_primary_first) {
    add_secondary();
    add_primary();

    const auto ids = registry.connection_ids();
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_TRUE(ids.at(0).is_primary());
    EXPECT_EQ(ids.at(1), ConnectionId::secondary(SECONDARY_INSTANCE));
    EXPECT_EQ(registry.secondaries().size(), 1u);
}

TEST_F(ConnectionRoutingDispatcherTest, registry_rejects_a_duplicate_connection) {
    add_primary();
    EXPECT_THROW(add_primary(), std::invalid_argument);
}

TEST_F(ConnectionRoutingDispatcherTest, registry_without_a_primary_reports_a_logic_error) {
    add_secondary();
    EXPECT_THROW(registry.primary(), std::logic_error);
}

} // namespace v2
} // namespace ocpp
