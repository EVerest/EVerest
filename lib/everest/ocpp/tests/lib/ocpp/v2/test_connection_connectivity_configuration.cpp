// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <device_model_test_helper.hpp>

#include <ocpp/v2/connection_connectivity_configuration.hpp>
#include <ocpp/v2/connection_id.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>

namespace ocpp {
namespace v2 {

namespace {
constexpr const char* SECONDARY_INSTANCE = "EMS";
constexpr const char* SOURCE = "test";
} // namespace

class ConnectionConnectivityConfigurationTest : public ::testing::Test {
protected:
    DeviceModelTestHelper device_model_test_helper{DEVICE_MODEL_DB_IN_MEMORY_PATH, MIGRATION_FILES_PATH, CONFIG_PATH,
                                                   false, SECONDARY_INSTANCE};
    DeviceModel* dm{device_model_test_helper.get_device_model()};

    ConnectionConnectivityConfiguration primary{*dm, ConnectionId::primary()};
    ConnectionConnectivityConfiguration secondary{*dm, ConnectionId::secondary(SECONDARY_INSTANCE)};

    /// Reads \p variable straight off the OCPPCommCtrlr component of \p connection, bypassing the
    /// adapter, so a test can tell where a write actually landed.
    template <typename T> std::optional<T> read_direct(const ConnectionId& connection, const Variable& variable) const {
        return dm->get_optional_value<T>(ConnectionComponentVariables::get_component_variable(connection, variable));
    }
};

TEST_F(ConnectionConnectivityConfigurationTest, reports_its_connection) {
    EXPECT_TRUE(primary.connection().is_primary());
    EXPECT_EQ(secondary.connection(), ConnectionId::secondary(SECONDARY_INSTANCE));
}

// --- NetworkConfigurationPriority ---------------------------------------------------------------

TEST_F(ConnectionConnectivityConfigurationTest, primary_priority_delegates_to_the_device_model) {
    EXPECT_EQ(primary.get_network_configuration_priority(), dm->get_network_configuration_priority());
}

TEST_F(ConnectionConnectivityConfigurationTest, secondary_priority_is_empty_until_configured) {
    // An unconfigured secondary must not inherit the primary's slots: overlapping slot sets are
    // exactly what application note section 3.4.1 forbids.
    EXPECT_FALSE(primary.get_network_configuration_priority().empty());
    EXPECT_EQ(secondary.get_network_configuration_priority(), "");
}

TEST_F(ConnectionConnectivityConfigurationTest, each_connection_reads_its_own_priority) {
    secondary.set_network_configuration_priority("7", SOURCE);

    EXPECT_EQ(secondary.get_network_configuration_priority(), "7");
    EXPECT_EQ(primary.get_network_configuration_priority(), "1");
}

TEST_F(ConnectionConnectivityConfigurationTest, secondary_priority_is_written_to_its_own_instance) {
    secondary.set_network_configuration_priority("7", SOURCE);

    EXPECT_EQ(read_direct<std::string>(ConnectionId::secondary(SECONDARY_INSTANCE),
                                       ConnectionComponentVariables::NetworkConfigurationPriority),
              "7");
    EXPECT_EQ(
        read_direct<std::string>(ConnectionId::primary(), ConnectionComponentVariables::NetworkConfigurationPriority),
        "1");
}

// --- security profile ---------------------------------------------------------------------------

TEST_F(ConnectionConnectivityConfigurationTest, primary_security_profile_delegates_to_the_device_model) {
    EXPECT_EQ(primary.get_security_profile(), dm->get_security_profile());
}

TEST_F(ConnectionConnectivityConfigurationTest, unconnected_secondary_reports_no_confirmed_security_profile) {
    // Zero makes the ConnectivityManager prune nothing, so a secondary that has never connected
    // imposes no downgrade protection of its own.
    EXPECT_EQ(secondary.get_security_profile(), 0);
}

TEST_F(ConnectionConnectivityConfigurationTest, secondary_security_profile_is_independent_of_the_primary) {
    const auto primary_profile = primary.get_security_profile();
    ASSERT_EQ(primary_profile, 1);

    // Application note section 3.4.1: a secondary may run at a lower security profile than the
    // primary, so recording one must not disturb the primary's.
    secondary.set_active_security_profile(3, SOURCE);

    EXPECT_EQ(secondary.get_security_profile(), 3);
    EXPECT_EQ(primary.get_security_profile(), primary_profile);
    EXPECT_EQ(dm->get_security_profile(), primary_profile);
}

TEST_F(ConnectionConnectivityConfigurationTest, secondary_security_profile_is_written_to_its_own_instance) {
    secondary.set_active_security_profile(2, SOURCE);

    EXPECT_EQ(
        read_direct<int>(ConnectionId::secondary(SECONDARY_INSTANCE), ConnectionComponentVariables::SecurityProfile),
        2);
}

TEST_F(ConnectionConnectivityConfigurationTest, primary_security_profile_write_reaches_security_ctrlr) {
    primary.set_active_security_profile(2, SOURCE);
    EXPECT_EQ(dm->get_security_profile(), 2);
}

// --- ActiveNetworkProfile -----------------------------------------------------------------------

TEST_F(ConnectionConnectivityConfigurationTest, secondary_active_slot_is_written_to_its_own_instance) {
    const auto primary_before = primary.get_active_network_profile_slot();

    secondary.set_active_network_profile_slot(7, SOURCE);

    EXPECT_EQ(secondary.get_active_network_profile_slot(), 7);
    EXPECT_EQ(read_direct<int>(ConnectionId::secondary(SECONDARY_INSTANCE),
                               ConnectionComponentVariables::ActiveNetworkProfile),
              7);
    EXPECT_EQ(primary.get_active_network_profile_slot(), primary_before);
}

TEST_F(ConnectionConnectivityConfigurationTest, primary_active_slot_delegates_to_the_device_model) {
    primary.set_active_network_profile_slot(2, SOURCE);

    EXPECT_EQ(primary.get_active_network_profile_slot(), 2);
    EXPECT_EQ(dm->get_active_network_profile_slot(), 2);
}

// --- SecurityCtrlr writes are suppressed for a secondary ----------------------------------------

TEST_F(ConnectionConnectivityConfigurationTest, secondary_does_not_write_security_ctrlr_security_profile) {
    const auto before = dm->get_security_profile();

    secondary.set_security_ctrl_security_profile(3, SOURCE);

    EXPECT_EQ(dm->get_security_profile(), before);
}

TEST_F(ConnectionConnectivityConfigurationTest, secondary_does_not_write_security_ctrlr_identity) {
    const auto identity_cv = ControllerComponentVariables::SecurityCtrlrIdentity;
    const auto before = dm->get_value<std::string>(identity_cv);

    secondary.set_security_ctrl_identity("EnergyMS-1234", SOURCE);

    EXPECT_EQ(dm->get_value<std::string>(identity_cv), before);
}

TEST_F(ConnectionConnectivityConfigurationTest, primary_still_writes_security_ctrlr) {
    primary.set_security_ctrl_security_profile(2, SOURCE);
    primary.set_security_ctrl_identity("cp-primary", SOURCE);

    EXPECT_EQ(dm->get_security_profile(), 2);
    EXPECT_EQ(dm->get_value<std::string>(ControllerComponentVariables::SecurityCtrlrIdentity), "cp-primary");
}

// --- slot scoped and station wide calls are delegated unchanged ---------------------------------

TEST_F(ConnectionConnectivityConfigurationTest, station_wide_reads_are_delegated) {
    EXPECT_EQ(secondary.get_allow_security_level_zero_connections(), dm->get_allow_security_level_zero_connections());
    EXPECT_EQ(secondary.get_network_config_timeout(), dm->get_network_config_timeout());
}

TEST_F(ConnectionConnectivityConfigurationTest, network_connection_profiles_are_not_redirected_per_connection) {
    // Slots are numbered globally and partitioned between connections by their priority lists, so
    // both adapters must see the very same NetworkConfiguration[<slot>] data.
    NetworkConnectionProfile profile;
    profile.ocppCsmsUrl = "ws://localhost:9000";
    profile.securityProfile = 1;
    profile.ocppInterface = OCPPInterfaceEnum::Wired0;
    profile.ocppTransport = OCPPTransportEnum::JSON;
    profile.messageTimeout = 30;

    ASSERT_TRUE(secondary.write_network_connection_profile(2, profile, SOURCE));

    const auto from_secondary = secondary.read_network_connection_profile(2);
    const auto from_primary = primary.read_network_connection_profile(2);
    ASSERT_TRUE(from_secondary.has_value());
    ASSERT_TRUE(from_primary.has_value());
    EXPECT_EQ(from_secondary->ocppCsmsUrl, from_primary->ocppCsmsUrl);
    EXPECT_EQ(from_secondary->ocppCsmsUrl.get(), "ws://localhost:9000");
}

} // namespace v2
} // namespace ocpp
