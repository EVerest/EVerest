// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <device_model_test_helper.hpp>

#include <ocpp/v2/connection_discovery.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>

namespace ocpp {
namespace v2 {

namespace {
constexpr const char* SOURCE = "test";
// The shipped component config defines NetworkConfiguration slots 1 and 2, and gives the primary
// connection slot 1. Slot 2 is therefore the one a secondary connection can be given.
constexpr std::int32_t PRIMARY_SLOT = 1;
constexpr std::int32_t FREE_SLOT = 2;
constexpr std::int32_t UNDEFINED_SLOT = 9;
} // namespace

class ConnectionDiscoveryTest : public ::testing::Test {
protected:
    /// Rebuilds the device model seeded with an OCPPCommCtrlr instance per name in \p instances.
    void seed(const std::vector<std::string>& instances) {
        this->helper = std::make_unique<DeviceModelTestHelper>(DEVICE_MODEL_DB_IN_MEMORY_PATH, MIGRATION_FILES_PATH,
                                                               CONFIG_PATH, false, instances);
        this->dm = this->helper->get_device_model();
    }

    /// Switches \p instance on and gives it \p priority, which is what makes discovery report it.
    void configure(const std::string& instance, const std::string& priority) {
        const auto id = ConnectionId::secondary(instance);
        this->set(id, ConnectionComponentVariables::Enabled, "true");
        this->set(id, ConnectionComponentVariables::NetworkConfigurationPriority, priority);
    }

    void set(const ConnectionId& id, const Variable& variable, const std::string& value) {
        const auto cv = ConnectionComponentVariables::get_component_variable(id, variable);
        ASSERT_EQ(this->dm->set_value(cv.component, cv.variable.value(), AttributeEnum::Actual, value, SOURCE),
                  SetVariableStatusEnum::Accepted);
    }

    static const ConnectionInfo* find(const std::vector<ConnectionInfo>& connections, const ConnectionId& id) {
        for (const auto& connection : connections) {
            if (connection.id == id) {
                return &connection;
            }
        }
        return nullptr;
    }

    std::unique_ptr<DeviceModelTestHelper> helper;
    DeviceModel* dm{nullptr};
};

TEST_F(ConnectionDiscoveryTest, reports_only_the_primary_when_nothing_else_is_configured) {
    seed({});

    const auto connections = discover_connections(*dm);

    ASSERT_EQ(connections.size(), 1u);
    EXPECT_TRUE(connections.at(0).id.is_primary());
    EXPECT_TRUE(connections.at(0).enabled);
    EXPECT_EQ(connections.at(0).network_configuration_slots, std::vector<std::int32_t>{PRIMARY_SLOT});
}

TEST_F(ConnectionDiscoveryTest, reports_an_enabled_secondary) {
    seed({"EMS"});
    configure("EMS", std::to_string(FREE_SLOT));

    const auto connections = discover_connections(*dm);

    ASSERT_EQ(connections.size(), 2u);
    EXPECT_TRUE(connections.at(0).id.is_primary());
    const auto& secondary = connections.at(1);
    EXPECT_EQ(secondary.id, ConnectionId::secondary("EMS"));
    EXPECT_TRUE(secondary.enabled);
    EXPECT_EQ(secondary.network_configuration_slots, std::vector<std::int32_t>{FREE_SLOT});
}

TEST_F(ConnectionDiscoveryTest, instance_names_are_free_to_choose) {
    // Section 3.3.1: the conventional names carry no meaning in the implementation.
    seed({"WhateverName"});
    configure("WhateverName", std::to_string(FREE_SLOT));

    const auto connections = discover_connections(*dm);

    ASSERT_EQ(connections.size(), 2u);
    EXPECT_EQ(connections.at(1).id, ConnectionId::secondary("WhateverName"));
}

TEST_F(ConnectionDiscoveryTest, skips_a_secondary_that_is_not_enabled) {
    seed({"EMS"});
    // Given a slot but left switched off.
    set(ConnectionId::secondary("EMS"), ConnectionComponentVariables::NetworkConfigurationPriority,
        std::to_string(FREE_SLOT));

    const auto connections = discover_connections(*dm);

    EXPECT_EQ(connections.size(), 1u);
}

TEST_F(ConnectionDiscoveryTest, skips_an_enabled_secondary_without_a_slot) {
    seed({"EMS"});
    set(ConnectionId::secondary("EMS"), ConnectionComponentVariables::Enabled, "true");

    const auto connections = discover_connections(*dm);

    EXPECT_EQ(connections.size(), 1u);
}

TEST_F(ConnectionDiscoveryTest, skips_a_slot_that_has_no_network_configuration_component) {
    seed({"EMS"});
    configure("EMS", std::to_string(UNDEFINED_SLOT));

    const auto connections = discover_connections(*dm);

    // The only slot it asked for cannot be dialled, so the connection is dropped rather than
    // reported as connectable.
    EXPECT_EQ(connections.size(), 1u);
}

TEST_F(ConnectionDiscoveryTest, keeps_the_usable_slots_of_a_partly_misconfigured_secondary) {
    seed({"EMS"});
    configure("EMS", std::to_string(UNDEFINED_SLOT) + "," + std::to_string(FREE_SLOT));

    const auto connections = discover_connections(*dm);

    ASSERT_EQ(connections.size(), 2u);
    EXPECT_EQ(connections.at(1).network_configuration_slots, std::vector<std::int32_t>{FREE_SLOT});
}

TEST_F(ConnectionDiscoveryTest, a_secondary_cannot_take_a_slot_from_the_primary) {
    // Section 3.4.1: slot sets must not overlap. The primary must never lose a slot to a secondary.
    seed({"EMS"});
    configure("EMS", std::to_string(PRIMARY_SLOT));

    const auto connections = discover_connections(*dm);

    ASSERT_EQ(connections.size(), 1u);
    EXPECT_TRUE(connections.at(0).id.is_primary());
    EXPECT_EQ(connections.at(0).network_configuration_slots, std::vector<std::int32_t>{PRIMARY_SLOT});
}

TEST_F(ConnectionDiscoveryTest, two_secondaries_cannot_share_a_slot) {
    seed({"AAA", "ZZZ"});
    configure("AAA", std::to_string(FREE_SLOT));
    configure("ZZZ", std::to_string(FREE_SLOT));

    const auto connections = discover_connections(*dm);

    // Resolved by name order so the outcome does not depend on device model iteration order.
    ASSERT_EQ(connections.size(), 2u);
    EXPECT_EQ(connections.at(1).id, ConnectionId::secondary("AAA"));
    EXPECT_EQ(connections.at(1).network_configuration_slots, std::vector<std::int32_t>{FREE_SLOT});
}

TEST_F(ConnectionDiscoveryTest, skips_an_instance_name_that_is_not_usable) {
    seed({"not a valid name"});
    const auto cv = ComponentVariable{Component{"OCPPCommCtrlr", std::nullopt, "not a valid name"}, Variable{"Enabled"},
                                      std::nullopt};
    ASSERT_EQ(dm->set_value(cv.component, cv.variable.value(), AttributeEnum::Actual, "true", SOURCE),
              SetVariableStatusEnum::Accepted);

    const auto connections = discover_connections(*dm);

    EXPECT_EQ(connections.size(), 1u);
}

TEST_F(ConnectionDiscoveryTest, ignores_a_non_integer_slot_token) {
    seed({"EMS"});
    configure("EMS", "not-a-slot," + std::to_string(FREE_SLOT));

    const auto connections = discover_connections(*dm);

    ASSERT_EQ(connections.size(), 2u);
    EXPECT_EQ(connections.at(1).network_configuration_slots, std::vector<std::int32_t>{FREE_SLOT});
}

TEST_F(ConnectionDiscoveryTest, reports_the_active_network_profile_slot) {
    seed({"EMS"});
    configure("EMS", std::to_string(FREE_SLOT));

    const auto id = ConnectionId::secondary("EMS");
    const auto cv =
        ConnectionComponentVariables::get_component_variable(id, ConnectionComponentVariables::ActiveNetworkProfile);
    ASSERT_EQ(dm->set_read_only_value(cv.component, cv.variable.value(), AttributeEnum::Actual,
                                      std::to_string(FREE_SLOT), "internal"),
              SetVariableStatusEnum::Accepted);

    const auto connections = discover_connections(*dm);

    ASSERT_EQ(connections.size(), 2u);
    EXPECT_EQ(connections.at(1).active_network_profile_slot, FREE_SLOT);
}

TEST_F(ConnectionDiscoveryTest, reports_several_secondaries_ordered_by_name) {
    seed({"ZZZ", "AAA"});
    configure("AAA", std::to_string(FREE_SLOT));
    // The primary keeps slot 1 and AAA takes slot 2, so ZZZ has nothing left to claim and is
    // dropped; give it a slot that does exist to show ordering instead.
    configure("ZZZ", std::to_string(PRIMARY_SLOT));

    const auto connections = discover_connections(*dm);

    ASSERT_EQ(connections.size(), 2u);
    EXPECT_TRUE(connections.at(0).id.is_primary());
    EXPECT_EQ(connections.at(1).id, ConnectionId::secondary("AAA"));
}

} // namespace v2
} // namespace ocpp
