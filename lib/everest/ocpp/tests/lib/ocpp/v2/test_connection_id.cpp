// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <ocpp/v2/connection_id.hpp>

namespace ocpp {
namespace v2 {

TEST(ConnectionIdTest, default_constructed_is_primary) {
    const ConnectionId connection;
    EXPECT_TRUE(connection.is_primary());
    EXPECT_FALSE(connection.instance().has_value());
}

TEST(ConnectionIdTest, primary_carries_no_instance) {
    const auto connection = ConnectionId::primary();
    EXPECT_TRUE(connection.is_primary());
    EXPECT_FALSE(connection.instance().has_value());
    EXPECT_EQ(connection.to_string(), "primary");
}

TEST(ConnectionIdTest, secondary_carries_its_instance_name) {
    const auto connection = ConnectionId::secondary("EMS");
    EXPECT_FALSE(connection.is_primary());
    ASSERT_TRUE(connection.instance().has_value());
    EXPECT_EQ(connection.instance().value(), "EMS");
    EXPECT_EQ(connection.to_string(), "OCPPCommCtrlr[EMS]");
}

/// Instance names are free to choose (application note section 3.3.1), so nothing may be special
/// cased about the conventional names.
TEST(ConnectionIdTest, instance_names_are_not_special_cased) {
    for (const auto& name : {"EMS", "MAINT", "MONITOR", "DATACOLLECTION", "anything-else", "a", "A.b_c-1"}) {
        const auto connection = ConnectionId::secondary(name);
        EXPECT_FALSE(connection.is_primary()) << name;
        EXPECT_EQ(connection.instance().value(), name);
    }
}

TEST(ConnectionIdTest, valid_instance_names) {
    EXPECT_TRUE(ConnectionId::is_valid_instance_name("EMS"));
    EXPECT_TRUE(ConnectionId::is_valid_instance_name("a"));
    EXPECT_TRUE(ConnectionId::is_valid_instance_name("A-b_c.9"));
    EXPECT_TRUE(ConnectionId::is_valid_instance_name(std::string(ConnectionId::MAX_INSTANCE_LENGTH, 'x')));
}

TEST(ConnectionIdTest, invalid_instance_names) {
    EXPECT_FALSE(ConnectionId::is_valid_instance_name(""));
    EXPECT_FALSE(ConnectionId::is_valid_instance_name(std::string(ConnectionId::MAX_INSTANCE_LENGTH + 1, 'x')));
    // Characters that carry meaning in the device model hierarchy notation used by AllowSetVariables
    EXPECT_FALSE(ConnectionId::is_valid_instance_name("a/b"));
    EXPECT_FALSE(ConnectionId::is_valid_instance_name("a#b"));
    EXPECT_FALSE(ConnectionId::is_valid_instance_name("a&b"));
    // Separators of the comma separated variables that reference connections
    EXPECT_FALSE(ConnectionId::is_valid_instance_name("a,b"));
    EXPECT_FALSE(ConnectionId::is_valid_instance_name("a b"));
    // Non printable and non ASCII
    EXPECT_FALSE(ConnectionId::is_valid_instance_name(std::string("a\0b", 3)));
    EXPECT_FALSE(ConnectionId::is_valid_instance_name("\xc3\xa4"));
}

TEST(ConnectionIdTest, secondary_rejects_an_invalid_instance_name) {
    EXPECT_THROW(ConnectionId::secondary(""), std::invalid_argument);
    EXPECT_THROW(ConnectionId::secondary("a/b"), std::invalid_argument);
    EXPECT_THROW(ConnectionId::secondary(std::string(ConnectionId::MAX_INSTANCE_LENGTH + 1, 'x')),
                 std::invalid_argument);
}

TEST(ConnectionIdTest, equality) {
    EXPECT_EQ(ConnectionId::primary(), ConnectionId::primary());
    EXPECT_EQ(ConnectionId::secondary("EMS"), ConnectionId::secondary("EMS"));
    EXPECT_NE(ConnectionId::primary(), ConnectionId::secondary("EMS"));
    EXPECT_NE(ConnectionId::secondary("EMS"), ConnectionId::secondary("MAINT"));
    // Instance names are matched exactly; the device model treats component names case
    // insensitively but the connection identity itself is the string that was configured.
    EXPECT_NE(ConnectionId::secondary("EMS"), ConnectionId::secondary("ems"));
}

TEST(ConnectionIdTest, ordering_puts_primary_first_then_sorts_by_name) {
    const std::map<ConnectionId, int> connections{
        {ConnectionId::secondary("MAINT"), 2},
        {ConnectionId::secondary("EMS"), 1},
        {ConnectionId::primary(), 0},
    };

    std::vector<int> order;
    order.reserve(connections.size());
    for (const auto& entry : connections) {
        order.push_back(entry.second);
    }
    EXPECT_EQ(order, (std::vector<int>{0, 1, 2}));
}

} // namespace v2
} // namespace ocpp
