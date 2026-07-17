// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>

#include <boost/asio/io_context.hpp>

#include <component_state_manager_mock.hpp>
#include <connectivity_manager_mock.hpp>
#include <device_model_test_helper.hpp>
#include <evse_manager_fake.hpp>
#include <evse_security_mock.hpp>
#include <message_dispatcher_mock.hpp>
#include <mocks/database_handler_mock.hpp>
#include <ocpp/common/connectivity_manager.hpp>
#include <ocpp/common/constants.hpp>
#include <ocpp/common/message_queue.hpp>
#include <ocpp/common/ocpp_logging.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/functional_blocks/availability.hpp>
#include <ocpp/v2/functional_blocks/diagnostics.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/functional_blocks/meter_values.hpp>
#include <ocpp/v2/functional_blocks/provisioning.hpp>
#include <ocpp/v2/functional_blocks/security.hpp>
#include <ocpp/v2/functional_blocks/tariff_and_cost.hpp>
#include <ocpp/v2/functional_blocks/transaction.hpp>
#include <ocpp/v2/init_device_model_db.hpp>
#include <ocpp/v2/messages/Get15118EVCertificate.hpp>
#include <ocpp/v2/messages/SetNetworkProfile.hpp>
#include <ocpp/v2/ocpp_types.hpp>
#include <ocsp_updater_mock.hpp>

using json = nlohmann::json;

namespace ocpp::v2 {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static NetworkConnectionProfile make_basic_profile(int security_profile = 1,
                                                   const std::string& url = "wss://csms.example.com/ocpp") {
    NetworkConnectionProfile p;
    p.ocppCsmsUrl = url;
    p.securityProfile = security_profile;
    p.ocppInterface = OCPPInterfaceEnum::Wired0;
    p.ocppTransport = OCPPTransportEnum::JSON;
    p.messageTimeout = 30;
    return p;
}

static APN make_test_apn() {
    APN apn;
    apn.apn = "internet";
    apn.apnAuthentication = APNAuthenticationEnum::AUTO;
    apn.apnUserName = "user";
    apn.apnPassword = "pass";
    return apn;
}

static VPN make_test_vpn() {
    VPN vpn;
    vpn.server = "vpn.example.com";
    vpn.user = "vpnuser";
    vpn.password = "vpnpass";
    vpn.key = "vpnkey";
    vpn.type = VPNEnum::IKEv2;
    return vpn;
}

// Build a JSON array blob from a list of SetNetworkProfileRequests.
static json make_blob(std::initializer_list<SetNetworkProfileRequest> reqs) {
    json arr = json::array();
    for (const auto& req : reqs) {
        arr.push_back(json(req));
    }
    return arr;
}

// Seed a JSON blob with the given profiles into the device model so that
// sync_json_blob_from_device_model has an existing blob to merge into.
static void seed_blob(DeviceModel& dm, const json& profiles) {
    ASSERT_TRUE(ControllerComponentVariables::NetworkConnectionProfiles.variable.has_value());
    const auto status = dm.set_value(ControllerComponentVariables::NetworkConnectionProfiles.component,
                                     ControllerComponentVariables::NetworkConnectionProfiles.variable.value(),
                                     AttributeEnum::Actual, profiles.dump(), "test");
    ASSERT_EQ(status, SetVariableStatusEnum::Accepted);
}

// Read the current NetworkConnectionProfiles JSON blob from the device model.
static json read_blob(DeviceModel& dm) {
    const auto str = dm.get_value<std::string>(ControllerComponentVariables::NetworkConnectionProfiles);
    if (str.empty()) {
        return json::array();
    }
    return json::parse(str);
}

// ---------------------------------------------------------------------------
// Fixture: real in-memory SQLite device model with example_config
// (includes InternalCtrlr, OCPPCommCtrlr, NetworkConfiguration_1, _2)
// ---------------------------------------------------------------------------

class NetworkConfigSyncTest : public ::testing::Test {
protected:
    DeviceModelTestHelper dm_helper;
    DeviceModel* dm{nullptr};

    NetworkConfigSyncTest() : dm_helper() {
        dm = dm_helper.get_device_model();
    }
};

// ---------------------------------------------------------------------------
// write_profile_to_device_model
// ---------------------------------------------------------------------------

TEST_F(NetworkConfigSyncTest, WriteBasicProfileSucceeds) {
    auto profile = make_basic_profile();
    EXPECT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"));
}

TEST_F(NetworkConfigSyncTest, WriteProfileWithApnSucceeds) {
    auto profile = make_basic_profile();
    profile.apn = make_test_apn();
    EXPECT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"));
}

TEST_F(NetworkConfigSyncTest, WriteProfileWithVpnSucceeds) {
    auto profile = make_basic_profile();
    profile.vpn = make_test_vpn();
    EXPECT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"));
}

// ---------------------------------------------------------------------------
// read_profile_from_device_model
// ---------------------------------------------------------------------------

TEST_F(NetworkConfigSyncTest, ReadFromEmptySlotReturnsNullopt) {
    // Slot 2 has no default OcppCsmsUrl, so reading without writing first returns nullopt
    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 2);
    EXPECT_FALSE(result.has_value());
}

TEST_F(NetworkConfigSyncTest, WriteAndReadBasicProfileRoundtrip) {
    auto original = make_basic_profile(2, "wss://test.server/ocpp");
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, original, "test"));

    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ocppCsmsUrl.get(), "wss://test.server/ocpp");
    EXPECT_EQ(result->securityProfile, 2);
    EXPECT_EQ(result->ocppInterface, OCPPInterfaceEnum::Wired0);
    EXPECT_EQ(result->messageTimeout, 30);
    EXPECT_FALSE(result->apn.has_value());
    EXPECT_FALSE(result->vpn.has_value());
}

// WriteAndReadProfileWithApnRoundtrip removed: ApnEnabled is ReadOnly (B09.FR.13)

TEST_F(NetworkConfigSyncTest, WriteAndReadProfileWithIdentityRoundtrip) {
    auto original = make_basic_profile();
    original.identity = "per_slot_identity";
    original.basicAuthPassword = "secret";
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, original, "test"));

    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->identity.has_value());
    EXPECT_EQ(result->identity->get(), "per_slot_identity");
}

TEST_F(NetworkConfigSyncTest, OverwriteProfileUpdatesFields) {
    auto first = make_basic_profile(1, "wss://first.example.com/ocpp");
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, first, "test"));

    auto second = make_basic_profile(2, "wss://second.example.com/ocpp");
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, second, "test"));

    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ocppCsmsUrl.get(), "wss://second.example.com/ocpp");
    EXPECT_EQ(result->securityProfile, 2);
}

TEST_F(NetworkConfigSyncTest, TwoSlotsAreIndependent) {
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(
        *dm, 1, make_basic_profile(1, "wss://slot1.example.com"), "test"));
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(
        *dm, 2, make_basic_profile(2, "wss://slot2.example.com"), "test"));

    auto r1 = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    auto r2 = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 2);

    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1->ocppCsmsUrl.get(), "wss://slot1.example.com");
    EXPECT_EQ(r2->ocppCsmsUrl.get(), "wss://slot2.example.com");
    EXPECT_EQ(r1->securityProfile, 1);
    EXPECT_EQ(r2->securityProfile, 2);
}

// ---------------------------------------------------------------------------
// migrate_from_blob_if_needed
// ---------------------------------------------------------------------------

// Note: slot 1 has a default OcppCsmsUrl in the test DM config ("wss://ocpp.example.com"),
// so it is never considered "empty" unless explicitly cleared first.
// Import-from-blob tests clear slot 1 before calling migrate to simulate a fresh deployment.

TEST_F(NetworkConfigSyncTest, MigrateFromBlobPopulatesSlot1) {
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1); // remove defaults so DM looks fresh

    SetNetworkProfileRequest req;
    req.configurationSlot = 1;
    req.connectionData = make_basic_profile(1, "wss://seeded.example.com/ocpp");
    seed_blob(*dm, make_blob({req}));

    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ocppCsmsUrl.get(), "wss://seeded.example.com/ocpp");
    EXPECT_EQ(result->securityProfile, 1);
}

TEST_F(NetworkConfigSyncTest, MigrateFromBlobClearsBlob) {
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    SetNetworkProfileRequest req;
    req.configurationSlot = 1;
    req.connectionData = make_basic_profile(1, "wss://migrated.example.com");
    seed_blob(*dm, make_blob({req}));

    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    // Blob must be cleared after migration — empty blob is the idempotency marker
    const auto blob = read_blob(*dm);
    EXPECT_EQ(blob.size(), 0u);
}

TEST_F(NetworkConfigSyncTest, MigrateSkippedWhenDmAlreadyPopulated) {
    // Explicitly populate slot 1 so migration must be skipped
    auto existing_profile = make_basic_profile(1, "wss://already-set.example.com");
    ASSERT_TRUE(
        NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, existing_profile, "test"));

    auto before = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(before.has_value()) << "Test precondition: slot 1 must have data after explicit write";

    SetNetworkProfileRequest req;
    req.configurationSlot = 1;
    req.connectionData = make_basic_profile(1, "wss://blob-set.example.com");
    seed_blob(*dm, make_blob({req}));
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(result.has_value());
    // DM value preserved — migration skipped because DM slot was already populated
    EXPECT_EQ(result->ocppCsmsUrl.get(), before->ocppCsmsUrl.get())
        << "DM value must be preserved when slot is already populated";

    // Blob must be cleared even when migration is skipped
    const auto blob = read_blob(*dm);
    EXPECT_EQ(blob.size(), 0u) << "Blob must be cleared after skipped migration";
}

TEST_F(NetworkConfigSyncTest, MigrateFromEmptyBlobIsNoop) {
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    // Seed an empty blob — no import should happen
    seed_blob(*dm, json::array());
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    // Slot 1 must still be empty
    EXPECT_FALSE(NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1).has_value())
        << "Empty blob must not populate any DM slot";
}

TEST_F(NetworkConfigSyncTest, MigrateFromBlobIsIdempotent) {
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    SetNetworkProfileRequest req;
    req.configurationSlot = 1;
    req.connectionData = make_basic_profile(1, "wss://idempotent.example.com");
    seed_blob(*dm, make_blob({req}));

    // First call: blob is non-empty, imports from blob, clears blob
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    auto r1 = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->ocppCsmsUrl.get(), "wss://idempotent.example.com");

    // Verify blob is cleared
    EXPECT_EQ(read_blob(*dm).size(), 0u) << "Blob must be cleared after migration";

    // Second call: blob is empty (already migrated), must be a no-op
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    auto r2 = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(r2.has_value());
    // DM value is unchanged — blob was empty so no re-import occurred
    EXPECT_EQ(r2->ocppCsmsUrl.get(), "wss://idempotent.example.com");
}

TEST_F(NetworkConfigSyncTest, MigrateFromBlobPopulatesBothSlots) {
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    SetNetworkProfileRequest req1;
    req1.configurationSlot = 1;
    req1.connectionData = make_basic_profile(1, "wss://primary.example.com");

    SetNetworkProfileRequest req2;
    req2.configurationSlot = 2;
    req2.connectionData = make_basic_profile(2, "wss://backup.example.com");

    seed_blob(*dm, make_blob({req1, req2}));
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    auto r1 = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    auto r2 = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 2);

    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1->ocppCsmsUrl.get(), "wss://primary.example.com");
    EXPECT_EQ(r2->ocppCsmsUrl.get(), "wss://backup.example.com");
    EXPECT_EQ(r2->securityProfile, 2);
}

// MigrateFromBlobWithApnPopulatesApnFields removed: ApnEnabled is ReadOnly (B09.FR.13)

TEST_F(NetworkConfigSyncTest, MigrateFromBlobPullsBasicAuthPasswordFromSecurityCtrlr) {
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    // Set a known BasicAuthPassword in SecurityCtrlr (the global password)
    const std::string expected_password = "MySecurePassword1234";
    dm->set_value(ControllerComponentVariables::BasicAuthPassword.component,
                  ControllerComponentVariables::BasicAuthPassword.variable.value(), AttributeEnum::Actual,
                  expected_password, "test");

    // Build a blob whose profile does NOT contain a basicAuthPassword (typical legacy format)
    SetNetworkProfileRequest req;
    req.configurationSlot = 1;
    req.connectionData = make_basic_profile(1, "wss://auth.example.com/ocpp");
    ASSERT_FALSE(req.connectionData.basicAuthPassword.has_value());

    seed_blob(*dm, make_blob({req}));
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    // After migration, the per-slot BasicAuthPassword must have been populated from SecurityCtrlr
    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->basicAuthPassword.has_value())
        << "Migration must populate per-slot BasicAuthPassword from SecurityCtrlr when blob has none";
    EXPECT_EQ(result->basicAuthPassword->get(), expected_password);
}

TEST_F(NetworkConfigSyncTest, MigrateFromBlobPreservesBlobBasicAuthPassword) {
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    // Set a different password in SecurityCtrlr
    dm->set_value(ControllerComponentVariables::BasicAuthPassword.component,
                  ControllerComponentVariables::BasicAuthPassword.variable.value(), AttributeEnum::Actual,
                  "GlobalPassword12345678", "test");

    // Build a blob whose profile DOES contain a basicAuthPassword
    SetNetworkProfileRequest req;
    req.configurationSlot = 1;
    req.connectionData = make_basic_profile(1, "wss://auth.example.com/ocpp");
    req.connectionData.basicAuthPassword = CiString<64>("SlotSpecificPassword!");

    seed_blob(*dm, make_blob({req}));
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    // The per-slot password from the blob must be used, not the SecurityCtrlr password
    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->basicAuthPassword.has_value());
    EXPECT_EQ(result->basicAuthPassword->get(), "SlotSpecificPassword!");
}

TEST_F(NetworkConfigSyncTest, MigrateFromBlobPullsIdentityFromSecurityCtrlr) {
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    // Set a known Identity in SecurityCtrlr (the global charging-station identifier)
    const std::string expected_identity = "CP-001-station-alpha";
    dm->set_read_only_value(ControllerComponentVariables::SecurityCtrlrIdentity.component,
                            ControllerComponentVariables::SecurityCtrlrIdentity.variable.value(), AttributeEnum::Actual,
                            expected_identity, "test");

    // Build a blob whose profile does NOT contain an identity (typical legacy format)
    SetNetworkProfileRequest req;
    req.configurationSlot = 1;
    req.connectionData = make_basic_profile(1, "wss://id-migration.example.com/ocpp");
    ASSERT_FALSE(req.connectionData.identity.has_value());

    seed_blob(*dm, make_blob({req}));
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    // After migration, the per-slot Identity must have been populated from SecurityCtrlr
    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->identity.has_value())
        << "Migration must populate per-slot Identity from SecurityCtrlr when blob has none";
    EXPECT_EQ(result->identity->get(), expected_identity);
}

TEST_F(NetworkConfigSyncTest, MigrateFromBlobPreservesBlobIdentity) {
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    // Set a different Identity in SecurityCtrlr
    dm->set_read_only_value(ControllerComponentVariables::SecurityCtrlrIdentity.component,
                            ControllerComponentVariables::SecurityCtrlrIdentity.variable.value(), AttributeEnum::Actual,
                            "global-station-identity", "test");

    // Build a blob whose profile DOES contain an identity
    SetNetworkProfileRequest req;
    req.configurationSlot = 1;
    req.connectionData = make_basic_profile(1, "wss://id-migration.example.com/ocpp");
    req.connectionData.identity = CiString<48>("slot-specific-identity");

    seed_blob(*dm, make_blob({req}));
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    // The per-slot identity from the blob must be used, not the SecurityCtrlr identity
    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->identity.has_value());
    EXPECT_EQ(result->identity->get(), "slot-specific-identity");
}

// ---------------------------------------------------------------------------
// Optional variable clearing on overwrite (Step 3 / Piet comment 4)
// ---------------------------------------------------------------------------

// Writing a profile with Identity and then overwriting with a profile that has
// no Identity must clear the Identity variable so it is not visible in the next read.
TEST_F(NetworkConfigSyncTest, OverwriteWithoutIdentityClearsIdentity) {
    auto with_identity = make_basic_profile();
    with_identity.identity = "slot_identity";
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, with_identity, "test"));

    auto r1 = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r1->identity.has_value());
    EXPECT_EQ(r1->identity->get(), "slot_identity");

    // Overwrite with a profile that has no identity
    auto without_identity = make_basic_profile();
    ASSERT_TRUE(
        NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, without_identity, "test"));

    auto r2 = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(r2.has_value());
    // Identity must be absent — clearing writes "" which is treated as unset
    EXPECT_FALSE(r2->identity.has_value()) << "Identity must be cleared after overwrite without identity";
}

// Writing a profile with APN and then overwriting without APN must not leave
// stale APN sub-fields in the device model.
// APN/VPN overwrite tests removed: ApnEnabled/VpnEnabled are ReadOnly (B09.FR.13/FR.15)
// since APN/VPN connections are not supported.

// ---------------------------------------------------------------------------
// clear_slot_in_device_model (Step 7)
// ---------------------------------------------------------------------------

TEST_F(NetworkConfigSyncTest, ClearSlotMakesReadReturnNullopt) {
    // Write a full profile to slot 1
    auto profile = make_basic_profile(1, "wss://to-be-cleared.example.com");
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"));

    // Verify it can be read back
    ASSERT_TRUE(NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1).has_value());

    // Clear the slot
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    // After clearing, required fields (OcppCsmsUrl) are empty -> read returns nullopt
    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    EXPECT_FALSE(result.has_value()) << "read_profile_from_device_model must return nullopt after clear";
}

TEST_F(NetworkConfigSyncTest, ClearSlotDoesNotAffectOtherSlot) {
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(
        *dm, 1, make_basic_profile(1, "wss://s1.example.com"), "test"));
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(
        *dm, 2, make_basic_profile(2, "wss://s2.example.com"), "test"));

    // Clear only slot 1
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    // Slot 1 must be gone, slot 2 must remain intact
    EXPECT_FALSE(NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1).has_value());
    auto r2 = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 2);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->ocppCsmsUrl.get(), "wss://s2.example.com");
}

// ---------------------------------------------------------------------------
// Legacy blob cleared after migration — full lifecycle test.
// Verifies that a populated NetworkConnectionProfiles blob is fully migrated
// into NetworkConfiguration DM components and the blob is cleared afterward.
// ---------------------------------------------------------------------------

TEST_F(NetworkConfigSyncTest, LegacyBlobClearedAfterMigration) {
    // Set up a database with a populated NetworkConnectionProfiles blob.
    // Clear default DM data so migration actually imports from the blob.
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    // Build a legacy blob with two profiles.
    SetNetworkProfileRequest req1;
    req1.configurationSlot = 1;
    req1.connectionData = make_basic_profile(1, "wss://legacy-primary.example.com/ocpp");

    SetNetworkProfileRequest req2;
    req2.configurationSlot = 2;
    req2.connectionData = make_basic_profile(2, "wss://legacy-backup.example.com/ocpp");

    seed_blob(*dm, make_blob({req1, req2}));

    // Verify precondition: blob is non-empty before migration
    ASSERT_GT(read_blob(*dm).size(), 0u) << "Precondition: blob must be non-empty before migration";

    // Run migration
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    // After migration, the blob variable must be empty/cleared.
    const auto blob_after = read_blob(*dm);
    EXPECT_EQ(blob_after.size(), 0u) << "Blob must be cleared after migration";

    // After migration, NetworkConfiguration DM slots contain the equivalent profile data.
    auto r1 = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(r1.has_value()) << "Slot 1 must be populated after migration";
    EXPECT_EQ(r1->ocppCsmsUrl.get(), "wss://legacy-primary.example.com/ocpp");
    EXPECT_EQ(r1->securityProfile, 1);
    EXPECT_EQ(r1->ocppInterface, OCPPInterfaceEnum::Wired0);
    EXPECT_EQ(r1->ocppTransport, OCPPTransportEnum::JSON);
    EXPECT_EQ(r1->messageTimeout, 30);

    auto r2 = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 2);
    ASSERT_TRUE(r2.has_value()) << "Slot 2 must be populated after migration";
    EXPECT_EQ(r2->ocppCsmsUrl.get(), "wss://legacy-backup.example.com/ocpp");
    EXPECT_EQ(r2->securityProfile, 2);
}

// No runtime code path reads the deprecated NetworkConnectionProfiles blob.
// Verified by grep: the only get_value/get_optional_value call for NetworkConnectionProfiles
// outside test code is in migrate_from_blob_if_needed() itself (ctrlr_component_variables.cpp).
// No other runtime code (charge_point.cpp, connectivity_manager, etc.) reads the blob.
// The charge_point.cpp call at line 430 only invokes migrate_from_blob_if_needed().

// ---------------------------------------------------------------------------
// Reboot persistence — only new DM component used after reinit
// Simulates a reboot by destroying the DeviceModel and creating a new one
// against the same database.  Verifies that:
//   - NetworkConfiguration DM slots are still populated
//   - Legacy blob remains empty
//   - Calling migrate_from_blob_if_needed() again is a no-op
// ---------------------------------------------------------------------------

TEST_F(NetworkConfigSyncTest, RebootPersistence_DmPopulatedAndBlobEmptyAfterReinit) {
    // --- Phase 1: initial boot with legacy blob ---

    // Clear default DM data so migration actually imports from the blob.
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    // Seed a legacy blob with two profiles.
    SetNetworkProfileRequest req1;
    req1.configurationSlot = 1;
    req1.connectionData = make_basic_profile(1, "wss://reboot-primary.example.com/ocpp");

    SetNetworkProfileRequest req2;
    req2.configurationSlot = 2;
    req2.connectionData = make_basic_profile(2, "wss://reboot-backup.example.com/ocpp");

    seed_blob(*dm, make_blob({req1, req2}));

    // Trigger migration (first boot)
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    // Verify migration happened before simulating reboot
    ASSERT_TRUE(NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1).has_value())
        << "Precondition: slot 1 must be populated after first-boot migration";
    ASSERT_EQ(read_blob(*dm).size(), 0u) << "Precondition: blob must be cleared after first-boot migration";

    // --- Phase 2: simulated reboot ---
    auto rebooted_storage = std::make_unique<DeviceModelStorageSqlite>(DEVICE_MODEL_DB_IN_MEMORY_PATH);
    DeviceModel rebooted_dm(std::move(rebooted_storage));

    // After reinit, NetworkConfiguration DM slots are populated with correct profile data.
    auto r1 = NetworkConfigurationComponentVariables::read_profile_from_device_model(rebooted_dm, 1);
    ASSERT_TRUE(r1.has_value()) << "Slot 1 must be populated after reboot";
    EXPECT_EQ(r1->ocppCsmsUrl.get(), "wss://reboot-primary.example.com/ocpp");
    EXPECT_EQ(r1->securityProfile, 1);
    EXPECT_EQ(r1->ocppInterface, OCPPInterfaceEnum::Wired0);
    EXPECT_EQ(r1->ocppTransport, OCPPTransportEnum::JSON);
    EXPECT_EQ(r1->messageTimeout, 30);

    auto r2 = NetworkConfigurationComponentVariables::read_profile_from_device_model(rebooted_dm, 2);
    ASSERT_TRUE(r2.has_value()) << "Slot 2 must be populated after reboot";
    EXPECT_EQ(r2->ocppCsmsUrl.get(), "wss://reboot-backup.example.com/ocpp");
    EXPECT_EQ(r2->securityProfile, 2);

    // After reinit, the legacy NetworkConnectionProfiles blob remains empty.
    const auto blob_after_reboot = read_blob(rebooted_dm);
    EXPECT_EQ(blob_after_reboot.size(), 0u) << "Blob must remain empty after reboot";

    // No re-migration occurs — calling migrate again is idempotent / skipped.
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(rebooted_dm);

    auto r1_after = NetworkConfigurationComponentVariables::read_profile_from_device_model(rebooted_dm, 1);
    ASSERT_TRUE(r1_after.has_value()) << "Slot 1 must still be populated after second migration call";
    EXPECT_EQ(r1_after->ocppCsmsUrl.get(), "wss://reboot-primary.example.com/ocpp")
        << "DM values must not change when migration is called again (idempotent)";

    auto r2_after = NetworkConfigurationComponentVariables::read_profile_from_device_model(rebooted_dm, 2);
    ASSERT_TRUE(r2_after.has_value()) << "Slot 2 must still be populated after second migration call";
    EXPECT_EQ(r2_after->ocppCsmsUrl.get(), "wss://reboot-backup.example.com/ocpp");

    // Blob must still be empty after the no-op migration
    EXPECT_EQ(read_blob(rebooted_dm).size(), 0u) << "Blob must remain empty after no-op migration on reboot";
}

// ---------------------------------------------------------------------------
// Legacy struct validation tests (kept for completeness)
// ---------------------------------------------------------------------------

class NetworkConnectionProfileTest : public ::testing::Test {
protected:
    static NetworkConnectionProfile CreateTestProfile(int32_t security_profile = 1) {
        NetworkConnectionProfile profile;
        profile.ocppCsmsUrl = "wss://csms.example.com/ocpp";
        profile.securityProfile = security_profile;
        profile.ocppInterface = OCPPInterfaceEnum::Wired0;
        profile.ocppTransport = OCPPTransportEnum::JSON;
        profile.messageTimeout = 30;
        return profile;
    }
};

TEST_F(NetworkConnectionProfileTest, ProfileStructureIsValid) {
    auto profile = CreateTestProfile(1);
    EXPECT_EQ(profile.ocppCsmsUrl.get(), "wss://csms.example.com/ocpp");
    EXPECT_EQ(profile.securityProfile, 1);
    EXPECT_EQ(profile.ocppInterface, OCPPInterfaceEnum::Wired0);
    EXPECT_EQ(profile.ocppTransport, OCPPTransportEnum::JSON);
    EXPECT_EQ(profile.messageTimeout, 30);
}

TEST_F(NetworkConnectionProfileTest, DifferentSecurityProfilesCanBeAssigned) {
    for (int sec = 0; sec <= 3; ++sec) {
        auto profile = CreateTestProfile(sec);
        EXPECT_EQ(profile.securityProfile, sec);
    }
}

TEST_F(NetworkConnectionProfileTest, ProfileWithAPNCanBeCreated) {
    auto profile = CreateTestProfile();
    profile.apn = make_test_apn();
    ASSERT_TRUE(profile.apn.has_value());
    EXPECT_EQ(profile.apn->apn.get(), "internet");
}

TEST_F(NetworkConnectionProfileTest, ComponentVariablesAreValid) {
    auto& url_var = NetworkConfigurationComponentVariables::OcppCsmsUrl;
    auto& sec_var = NetworkConfigurationComponentVariables::SecurityProfile;
    auto& iface_var = NetworkConfigurationComponentVariables::OcppInterface;
    (void)url_var;
    (void)sec_var;
    (void)iface_var;
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Edge cases: invalid inputs and slot overflow
// ---------------------------------------------------------------------------

TEST_F(NetworkConfigSyncTest, WriteInvalidSecurityProfileAboveRangeIsRejected) {
    auto profile = make_basic_profile(5, "wss://csms.example.com/ocpp");
    EXPECT_FALSE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"))
        << "SecurityProfile=5 is outside the valid range [0,3] and must be rejected";
}

TEST_F(NetworkConfigSyncTest, WriteInvalidSecurityProfileNegativeIsRejected) {
    auto profile = make_basic_profile(-1, "wss://csms.example.com/ocpp");
    EXPECT_FALSE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"))
        << "SecurityProfile=-1 is outside the valid range [0,3] and must be rejected";
}

TEST_F(NetworkConfigSyncTest, WriteInvalidSecurityProfileBoundaryAboveIsRejected) {
    auto profile = make_basic_profile(4, "wss://csms.example.com/ocpp");
    EXPECT_FALSE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"))
        << "SecurityProfile=4 is outside the valid range [0,3] and must be rejected";
}

TEST_F(NetworkConfigSyncTest, WriteValidSecurityProfileBoundariesSucceed) {
    // Boundary values 0 and 3 must be accepted
    auto profile0 = make_basic_profile(0, "wss://csms0.example.com/ocpp");
    EXPECT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile0, "test"))
        << "SecurityProfile=0 is valid and must be accepted";

    auto profile3 = make_basic_profile(3, "wss://csms3.example.com/ocpp");
    EXPECT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile3, "test"))
        << "SecurityProfile=3 is valid and must be accepted";
}

TEST_F(NetworkConfigSyncTest, WriteUrlExceedingMaxLimitIsRejected) {
    // OcppCsmsUrl is CiString<2000> — setting a value longer than 2000 chars must fail.
    // The CiString constructor throws on overflow, which write_profile_to_device_model catches.
    // We also verify that the DM itself rejects too-long values via set_value directly.
    const auto url_cv = NetworkConfigurationComponentVariables::get_component_variable(
        1, NetworkConfigurationComponentVariables::OcppCsmsUrl);
    std::string long_url = "wss://csms.example.com/" + std::string(2000, 'x');
    auto status = dm->set_value(url_cv.component, url_cv.variable.value(), AttributeEnum::Actual, long_url, "test");
    EXPECT_NE(status, SetVariableStatusEnum::Accepted)
        << "URL exceeding maxLimit=2000 must be rejected by the device model";
}

TEST_F(NetworkConfigSyncTest, WriteEmptyUrlSucceedsButReadReturnsNullopt) {
    // An empty URL can be set (clearing the slot effectively), but read treats it as unconfigured
    auto profile = make_basic_profile(1, "");
    // write_profile_to_device_model may succeed (empty string is valid for the DM string type)
    // but read_profile_from_device_model treats empty URL as "slot not configured"
    NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test");
    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    EXPECT_FALSE(result.has_value()) << "Empty URL must be treated as unconfigured slot";
}

TEST_F(NetworkConfigSyncTest, WriteToSlotBeyondConfiguredMaxFails) {
    // Only NetworkConfiguration_1 and _2 exist in the test DM config.
    // Writing to slot 3 should fail because the component does not exist.
    auto profile = make_basic_profile(1, "wss://overflow.example.com/ocpp");
    EXPECT_FALSE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 3, profile, "test"))
        << "Slot 3 does not exist in the device model and write must fail";
}

TEST_F(NetworkConfigSyncTest, ReadFromSlotBeyondConfiguredMaxReturnsNullopt) {
    // Reading from a non-existent slot should return nullopt, not crash
    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 3);
    EXPECT_FALSE(result.has_value()) << "Slot 3 does not exist and read must return nullopt";
}

TEST_F(NetworkConfigSyncTest, WriteToSlotZeroFails) {
    // Slot 0 is not a valid NetworkConfiguration instance
    auto profile = make_basic_profile(1, "wss://slot0.example.com/ocpp");
    EXPECT_FALSE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 0, profile, "test"))
        << "Slot 0 does not exist in the device model and write must fail";
}

TEST_F(NetworkConfigSyncTest, WriteToNegativeSlotFails) {
    auto profile = make_basic_profile(1, "wss://negative.example.com/ocpp");
    EXPECT_FALSE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, -1, profile, "test"))
        << "Negative slot does not exist in the device model and write must fail";
}

TEST_F(NetworkConfigSyncTest, ClearNonExistentSlotDoesNotCrash) {
    // Clearing a non-existent slot should not throw or crash
    EXPECT_NO_THROW(NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 3))
        << "Clearing a non-existent slot must not crash";
}

// ---------------------------------------------------------------------------
// ConnectivityManager cache rebuild callback tests
// Verifies that reload_network_profiles() / set_network_profile() keep the
// ConnectivityManager's in-memory cache in sync with the device model.
// ---------------------------------------------------------------------------

class ConnectivityManagerCacheTest : public ::testing::Test {
protected:
    DeviceModelTestHelper dm_helper;
    DeviceModel* dm{nullptr};
    std::shared_ptr<ocpp::EvseSecurityMock> evse_security;
    std::shared_ptr<ocpp::MessageLogging> logging;
    std::unique_ptr<ConnectivityManager> cm;

    ConnectivityManagerCacheTest() : dm_helper() {
        dm = dm_helper.get_device_model();
        // Seed slot 1 so the cache has something to work with (default OcppCsmsUrl is empty)
        auto seed_profile = make_basic_profile(1, "wss://ocpp.example.com");
        NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, seed_profile, "test");
        evse_security = std::make_shared<ocpp::EvseSecurityMock>();
        logging = std::make_shared<ocpp::MessageLogging>(false, "", "", false, false, false, false, false, false, false,
                                                         nullptr);
        cm = std::make_unique<ConnectivityManager>(*dm, evse_security, "");
        cm->set_logging(logging);
        cm->set_message_callback([](const std::string&) {});
    }

    // Invoke the cache-prune routine directly so the prune logic is exercised in
    // isolation, without going through connect()/try_connect_websocket() (which
    // would attempt to create a real websocket).
    void invoke_check_cache_for_invalid_security_profiles() {
        cm->check_cache_for_invalid_security_profiles();
    }
};

// Verify initial cache reflects the DM's default state (slot 1 populated from config)
TEST_F(ConnectivityManagerCacheTest, InitialCacheReflectsDeviceModel) {
    // The test DM config has NetworkConfigurationPriority="1" and slot 1 has a default URL
    auto slots = cm->get_network_connection_slots();
    ASSERT_FALSE(slots.empty()) << "Priority list must not be empty";
    EXPECT_EQ(slots.at(0), 1);

    auto profile = cm->get_network_connection_profile(1);
    ASSERT_TRUE(profile.has_value()) << "Slot 1 must be cached from DM defaults";
    EXPECT_EQ(profile->ocppCsmsUrl.get(), "wss://ocpp.example.com");
}

// Modifying a profile in the DM + reload_network_profiles() updates the cache
TEST_F(ConnectivityManagerCacheTest, ReloadAfterDmUpdateRefreshesCache) {
    // Directly write a new profile to the DM (bypassing ConnectivityManager)
    auto updated = make_basic_profile(2, "wss://updated.example.com/ocpp");
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, updated, "test"));

    // Cache still has the old value before reload
    auto before = cm->get_network_connection_profile(1);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(before->ocppCsmsUrl.get(), "wss://ocpp.example.com") << "Cache must still have old value before reload";

    // Reload the cache (simulates what Provisioning FB does after SetVariables)
    cm->reload_network_profiles();

    // After reload, cache reflects the updated value
    auto after = cm->get_network_connection_profile(1);
    ASSERT_TRUE(after.has_value()) << "Slot 1 must still be in cache after reload";
    EXPECT_EQ(after->ocppCsmsUrl.get(), "wss://updated.example.com/ocpp");
    EXPECT_EQ(after->securityProfile, 2);
}

// set_network_profile() writes to DM and automatically refreshes cache
TEST_F(ConnectivityManagerCacheTest, SetNetworkProfileUpdatesCache) {
    auto new_profile = make_basic_profile(3, "wss://new-csms.example.com/ocpp");
    ASSERT_TRUE(cm->set_network_profile(1, new_profile, "test"));

    auto cached = cm->get_network_connection_profile(1);
    ASSERT_TRUE(cached.has_value()) << "Profile must be in cache after set_network_profile";
    EXPECT_EQ(cached->ocppCsmsUrl.get(), "wss://new-csms.example.com/ocpp");
    EXPECT_EQ(cached->securityProfile, 3);
}

// Clear/delete scenario — clearing a slot in the DM + reload removes it from cache
TEST_F(ConnectivityManagerCacheTest, ReloadAfterClearSlotRemovesFromCache) {
    // Precondition: slot 1 is in the cache
    ASSERT_TRUE(cm->get_network_connection_profile(1).has_value());

    // Clear slot 1 directly in the DM
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    // Reload the cache
    cm->reload_network_profiles();

    // Slot 1 is still in the priority list but its profile is gone (read returns nullopt for empty slot)
    auto profile = cm->get_network_connection_profile(1);
    EXPECT_FALSE(profile.has_value()) << "Cleared slot must not have a cached profile after reload";
}

// Add a profile to a previously-empty slot via set_network_profile
TEST_F(ConnectivityManagerCacheTest, SetNetworkProfileAddsNewSlotToCache) {
    // Slot 2 has no default URL in the test config, so it should not be in the cache initially
    auto initial = cm->get_network_connection_profile(2);
    EXPECT_FALSE(initial.has_value()) << "Slot 2 must not be cached initially (no default URL)";

    // Add a profile to slot 2 via the ConnectivityManager
    auto profile = make_basic_profile(1, "wss://slot2.example.com/ocpp");
    ASSERT_TRUE(cm->set_network_profile(2, profile, "test"));

    // Slot 2 must now be in the cache
    auto cached = cm->get_network_connection_profile(2);
    ASSERT_TRUE(cached.has_value()) << "Newly added slot 2 must be in cache after set_network_profile";
    EXPECT_EQ(cached->ocppCsmsUrl.get(), "wss://slot2.example.com/ocpp");

    // Slot 2 must also appear in the priority list
    auto slots = cm->get_network_connection_slots();
    EXPECT_NE(std::find(slots.begin(), slots.end(), 2), slots.end())
        << "Slot 2 must be in the priority list after set_network_profile";
}

// B09.FR.18: set_network_profile must not inherit per-slot Identity from the currently
// active slot — each slot's per-slot Identity is independent. If the new profile has no
// identity field, reads on that slot must fall back to SecurityCtrlr.Identity (FR.16),
// not be overwritten with a copy of the active slot's value.
TEST_F(ConnectivityManagerCacheTest, SetNetworkProfileDoesNotInheritIdentityFromActiveSlot) {
    // Arrange: slot 1 (the active slot in cm) has a per-slot Identity
    auto active_profile = make_basic_profile(1, "wss://active.example.com/ocpp");
    active_profile.identity = "id1";
    ASSERT_TRUE(cm->set_network_profile(1, active_profile, "test"));
    auto active_read = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(active_read.has_value());
    ASSERT_TRUE(active_read->identity.has_value());
    ASSERT_EQ(active_read->identity->get(), "id1");

    // Act: set a new profile on slot 2 with no identity
    auto new_profile = make_basic_profile(1, "wss://other.example.com/ocpp");
    ASSERT_FALSE(new_profile.identity.has_value());
    ASSERT_TRUE(cm->set_network_profile(2, new_profile, "test"));

    // Assert: per-slot Identity for slot 2 was not inherited from slot 1
    auto slot2 = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 2);
    ASSERT_TRUE(slot2.has_value());
    EXPECT_FALSE(slot2->identity.has_value())
        << "set_network_profile must not inherit Identity from the active slot (B09.FR.18)";
}

// B09.FR.18: set_network_profile must not inherit per-slot BasicAuthPassword from the
// currently active slot — each slot's per-slot password is independent. If the new
// profile has no basicAuthPassword, reads on that slot must fall back to
// SecurityCtrlr.BasicAuthPassword (FR.16), not a copy of the active slot's password.
TEST_F(ConnectivityManagerCacheTest, SetNetworkProfileDoesNotInheritBasicAuthFromActiveSlot) {
    // Arrange: slot 1 (the active slot in cm) has a per-slot BasicAuthPassword
    auto active_profile = make_basic_profile(1, "wss://active.example.com/ocpp");
    active_profile.basicAuthPassword = CiString<64>("ActiveSlotPassword!!");
    ASSERT_TRUE(cm->set_network_profile(1, active_profile, "test"));
    auto active_read = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(active_read.has_value());
    ASSERT_TRUE(active_read->basicAuthPassword.has_value());
    ASSERT_EQ(active_read->basicAuthPassword->get(), "ActiveSlotPassword!!");

    // Act: set a new profile on slot 2 with no basicAuthPassword
    auto new_profile = make_basic_profile(1, "wss://other.example.com/ocpp");
    ASSERT_FALSE(new_profile.basicAuthPassword.has_value());
    ASSERT_TRUE(cm->set_network_profile(2, new_profile, "test"));

    // Assert: per-slot BasicAuthPassword for slot 2 was not inherited from slot 1
    auto slot2 = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 2);
    ASSERT_TRUE(slot2.has_value());
    EXPECT_FALSE(slot2->basicAuthPassword.has_value())
        << "set_network_profile must not inherit BasicAuthPassword from the active slot (B09.FR.18)";
}

// Full lifecycle — add, update, then clear a profile
TEST_F(ConnectivityManagerCacheTest, AddUpdateClearLifecycle) {
    // Step 1: Add profile to slot 2
    auto added = make_basic_profile(1, "wss://added.example.com/ocpp");
    ASSERT_TRUE(cm->set_network_profile(2, added, "test"));
    ASSERT_TRUE(cm->get_network_connection_profile(2).has_value());

    // Step 2: Update slot 2 via set_network_profile
    auto updated = make_basic_profile(2, "wss://updated-slot2.example.com/ocpp");
    ASSERT_TRUE(cm->set_network_profile(2, updated, "test"));
    auto after_update = cm->get_network_connection_profile(2);
    ASSERT_TRUE(after_update.has_value());
    EXPECT_EQ(after_update->ocppCsmsUrl.get(), "wss://updated-slot2.example.com/ocpp");
    EXPECT_EQ(after_update->securityProfile, 2);

    // Step 3: Clear slot 2 in DM and reload
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 2);
    cm->reload_network_profiles();
    EXPECT_FALSE(cm->get_network_connection_profile(2).has_value())
        << "Cleared slot 2 must not be in cache after reload";
}

// Regression: check_cache_for_invalid_security_profiles must keep state->slots
// and state->cached_profiles in sync. Pruning slots without pruning the cache
// caused get_network_connection_profile(<pruned slot>) to return stale data
// after a SecurityProfile bump.
TEST_F(ConnectivityManagerCacheTest, CheckCacheInvalidSecurityProfilesPrunesCacheNotJustSlots) {
    // Arrange: slot 1 already seeded by the fixture with securityProfile=1.
    // Add slot 2 with securityProfile=3 and include both in NetworkConfigurationPriority.
    auto high_sec_profile = make_basic_profile(3, "wss://high-sec.example.com/ocpp");
    ASSERT_TRUE(
        NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 2, high_sec_profile, "test"));
    ASSERT_EQ(dm->set_value(ControllerComponentVariables::NetworkConfigurationPriority.component,
                            ControllerComponentVariables::NetworkConfigurationPriority.variable.value(),
                            AttributeEnum::Actual, "1,2", "test"),
              SetVariableStatusEnum::Accepted);
    cm->reload_network_profiles();

    // Sanity: both slots are present in the cache before the prune.
    ASSERT_TRUE(cm->get_network_connection_profile(1).has_value());
    ASSERT_TRUE(cm->get_network_connection_profile(2).has_value());

    // Bump the global SecurityProfile so slot 1 (sp=1) is now below the threshold.
    // SecurityProfile is ReadOnly in the standardized config, mirroring how
    // confirm_successful_connection() raises it via set_read_only_value at runtime.
    ASSERT_EQ(dm->set_read_only_value(ControllerComponentVariables::SecurityProfile.component,
                                      ControllerComponentVariables::SecurityProfile.variable.value(),
                                      AttributeEnum::Actual, "3", "test"),
              SetVariableStatusEnum::Accepted);

    // Act: prune the cache.
    invoke_check_cache_for_invalid_security_profiles();

    // Assert: slot 1 must be removed from BOTH state->slots and state->cached_profiles.
    // get_network_connection_profile reads from cached_profiles, so it must return nullopt.
    auto pruned = cm->get_network_connection_profile(1);
    EXPECT_FALSE(pruned.has_value())
        << "Slot 1 (securityProfile=1) must be pruned from cached_profiles after the bump to SecurityProfile=3";

    // Slot 2 must remain.
    auto retained = cm->get_network_connection_profile(2);
    ASSERT_TRUE(retained.has_value()) << "Slot 2 (securityProfile=3) must remain in the cache";
    EXPECT_EQ(retained->ocppCsmsUrl.get(), "wss://high-sec.example.com/ocpp");

    // Slot 1 must also be removed from the priority list.
    auto slots = cm->get_network_connection_slots();
    EXPECT_EQ(std::find(slots.begin(), slots.end(), 1), slots.end()) << "Slot 1 must be removed from priority list";
}

// H3: ActiveNetworkProfile is persisted on a confirmed successful connection, not at dial time.
// Persisting on success means a station that dials a broken slot and reboots does not seed the
// broken slot as the fallback target.
TEST_F(ConnectivityManagerCacheTest, ConfirmSuccessfulConnectionPersistsActiveSlot) {
    // The active slot is slot 1 (from the fixture priority list). Seed ActiveNetworkProfile with a
    // stale value so we can prove confirm_successful_connection() overwrites it with the slot that
    // is actually in use, rather than the value being left over from a dial.
    ASSERT_EQ(cm->get_network_connection_slots().at(0), 1);
    dm->set_active_network_profile_slot(999, "test");
    ASSERT_EQ(dm->get_optional_value<int>(ControllerComponentVariables::ActiveNetworkProfile).value(), 999);

    cm->confirm_successful_connection();

    const auto persisted = dm->get_optional_value<int>(ControllerComponentVariables::ActiveNetworkProfile);
    ASSERT_TRUE(persisted.has_value()) << "ActiveNetworkProfile must be persisted after a confirmed connection";
    EXPECT_EQ(persisted.value(), 1) << "confirm_successful_connection() must persist the active slot";
}

// ---------------------------------------------------------------------------
// Provisioning block — reject active slot modification (B09.FR.21/22)
// Verifies that validate_set_variable() rejects SetVariables targeting the
// currently-active connection profile slot and allows non-active slots.
// ---------------------------------------------------------------------------

// Minimal mock stubs for functional block interfaces needed by Provisioning constructor.
// validate_set_variable() only reads from device_model — these are never called.

class AvailabilityMock : public AvailabilityInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, status_notification_req, (std::int32_t, std::int32_t, ConnectorStatusEnum, bool), (override));
    MOCK_METHOD(void, heartbeat_req, (bool), (override));
    MOCK_METHOD(void, handle_scheduled_change_availability_requests, (std::int32_t), (override));
    MOCK_METHOD(void, set_scheduled_change_availability_requests, (std::int32_t, AvailabilityChange), (override));
    MOCK_METHOD(void, set_heartbeat_timer_interval, (const std::chrono::seconds&), (override));
    MOCK_METHOD(void, stop_heartbeat_timer, (), (override));
    MOCK_METHOD(ChangeAvailabilityResponse, change_availability_req, (bool&, const ChangeAvailabilityRequest&),
                (override));
    MOCK_METHOD(void, action_change_availability_req,
                (bool, const ChangeAvailabilityRequest&, const ChangeAvailabilityResponse&), (override));
};

class MeterValuesMock : public MeterValuesInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, update_aligned_data_interval, (), (override));
    MOCK_METHOD(void, on_meter_value, (std::int32_t, const MeterValue&), (override));
    MOCK_METHOD(MeterValue, get_latest_meter_value_filtered,
                (const MeterValue&, ReadingContextEnum, const RequiredComponentVariable&), (override));
    MOCK_METHOD(void, meter_values_req, (std::int32_t, const std::vector<MeterValue>&, bool), (override));
};

class SecurityMock : public SecurityInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, security_event_notification_req,
                (const CiString<50>&, const std::optional<CiString<255>>&, bool, bool, const std::optional<DateTime>&),
                (override));
    MOCK_METHOD(void, sign_certificate_req, (const ocpp::CertificateSigningUseEnum&, bool), (override));
    MOCK_METHOD(bool, is_sign_certificate_possible, (const ocpp::CertificateSigningUseEnum&), (const, override));
    MOCK_METHOD(void, stop_certificate_signed_timer, (), (override));
    MOCK_METHOD(void, init_certificate_expiration_check_timers, (), (override));
    MOCK_METHOD(void, stop_certificate_expiration_check_timers, (), (override));
    MOCK_METHOD(Get15118EVCertificateResponse, on_get_15118_ev_certificate_request,
                (const Get15118EVCertificateRequest&), (override));
};

class DiagnosticsMock : public DiagnosticsInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, notify_event_req, (const std::vector<EventData>&), (override));
    MOCK_METHOD(void, stop_monitoring, (), (override));
    MOCK_METHOD(void, start_monitoring, (), (override));
    MOCK_METHOD(void, process_triggered_monitors, (), (override));
};

class TransactionMock : public TransactionInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, on_transaction_started,
                (std::int32_t, std::int32_t, const std::string&, const DateTime&, TriggerReasonEnum, const MeterValue&,
                 const std::optional<IdToken>&, const std::optional<IdToken>&, const std::optional<std::int32_t>&,
                 const std::optional<std::int32_t>&, ChargingStateEnum),
                (override));
    MOCK_METHOD(void, on_transaction_finished,
                (std::int32_t, const DateTime&, const MeterValue&, ReasonEnum, TriggerReasonEnum,
                 const std::optional<IdToken>&, const std::optional<std::string>&, ChargingStateEnum,
                 const std::optional<SignedMeterValue>&),
                (override));
    MOCK_METHOD(void, transaction_event_req,
                (const TransactionEventEnum&, const DateTime&, const Transaction&, const TriggerReasonEnum&,
                 std::int32_t, const std::optional<std::int32_t>&, const std::optional<EVSE>&,
                 const std::optional<IdToken>&, const std::optional<std::vector<MeterValue>>&,
                 const std::optional<std::int32_t>&, bool, const std::optional<std::int32_t>&, bool),
                (override));
    MOCK_METHOD(void, set_remote_start_id_for_evse, (std::int32_t, IdToken, std::int32_t), (override));
    MOCK_METHOD(bool, is_id_token_awaiting_remote_start, (const IdToken&), (const, override));
    MOCK_METHOD(void, schedule_reset, (std::optional<std::int32_t>), (override));
};

// Helper: build a SetVariableData targeting a NetworkConfiguration slot variable
static SetVariableData make_set_variable_data(int slot, const std::string& variable_name, const std::string& value) {
    SetVariableData svd;
    svd.attributeValue = value;
    svd.component.name = "NetworkConfiguration";
    svd.component.instance = CiString<50>(std::to_string(slot));
    svd.variable.name = variable_name;
    return svd;
}

// Helper: check if a SetVariableResult was rejected due to priority-slot protection (B09.FR.22).
// The spec mandates a single reasonCode "PriorityNetworkConf" for every slot listed in
// NetworkConfigurationPriority, including the currently active slot.
static bool is_rejected_by_active_slot(const SetVariableResult& result) {
    return result.attributeStatus == SetVariableStatusEnum::Rejected && result.attributeStatusInfo.has_value() &&
           result.attributeStatusInfo->reasonCode.get() == "PriorityNetworkConf";
}

class ProvisioningActiveSlotTest : public ::testing::Test {
protected:
    DeviceModelTestHelper dm_helper;
    DeviceModel* dm{nullptr};

    // Mocks for FunctionalBlockContext
    ::testing::NiceMock<MockMessageDispatcher> mock_dispatcher;
    ::testing::NiceMock<ConnectivityManagerMock> connectivity_manager;
    std::unique_ptr<EvseManagerFake> evse_manager;
    ::testing::NiceMock<DatabaseHandlerMock> db_handler;
    ocpp::EvseSecurityMock evse_security;
    ::testing::NiceMock<ComponentStateManagerMock> component_state_manager;
    std::atomic<OcppProtocolVersion> ocpp_version{OcppProtocolVersion::v201};

    // Mocks for Provisioning dependencies
    ::testing::NiceMock<OcspUpdaterMock> ocsp_updater;
    ::testing::NiceMock<AvailabilityMock> availability;
    ::testing::NiceMock<MeterValuesMock> meter_values;
    ::testing::NiceMock<SecurityMock> security;
    ::testing::NiceMock<DiagnosticsMock> diagnostics;
    ::testing::NiceMock<TransactionMock> transaction;
    std::atomic<RegistrationStatusEnum> registration_status{RegistrationStatusEnum::Accepted};

    // TariffAndCost dependencies — callbacks are taken by non-const reference, must outlive tariff_and_cost
    boost::asio::io_context io_context;
    std::optional<TariffMessageCallback> tariff_message_cb;
    std::optional<SetRunningCostCallback> set_running_cost_cb;
    std::optional<DefaultPriceCallback> default_price_cb;

    std::unique_ptr<FunctionalBlockContext> fb_context;
    std::unique_ptr<MessageQueue<MessageType>> message_queue;
    std::unique_ptr<TariffAndCost> tariff_and_cost;
    std::unique_ptr<Provisioning> provisioning;

    ProvisioningActiveSlotTest() : dm_helper() {
        dm = dm_helper.get_device_model();
        evse_manager = std::make_unique<EvseManagerFake>(1);

        fb_context =
            std::make_unique<FunctionalBlockContext>(mock_dispatcher, *dm, connectivity_manager, *evse_manager,
                                                     db_handler, evse_security, component_state_manager, ocpp_version);

        // MessageQueue with no-op send callback and nullptr db handler (never started)
        MessageQueueConfig<MessageType> mq_config;
        message_queue = std::make_unique<MessageQueue<MessageType>>([](json) { return false; }, mq_config, nullptr);

        tariff_and_cost = std::make_unique<TariffAndCost>(*fb_context, meter_values, tariff_message_cb,
                                                          set_running_cost_cb, default_price_cb, io_context);

        provisioning = std::make_unique<Provisioning>(
            *fb_context, *message_queue, ocsp_updater, availability, meter_values, security, diagnostics, transaction,
            std::nullopt,                                                    // time_sync_callback
            std::nullopt,                                                    // boot_notification_callback
            std::nullopt,                                                    // validate_network_profile_callback
            [](auto, auto) { return true; },                                 // is_reset_allowed
            [](auto, auto) {},                                               // reset_callback
            [](auto, auto) { return RequestStartStopStatusEnum::Accepted; }, // stop_transaction
            std::nullopt,                                                    // variable_changed_callback
            *tariff_and_cost, registration_status);
    }

    // Set the ActiveNetworkProfile in the DM to simulate an active connection on the given slot
    void set_active_slot(int slot) {
        ASSERT_TRUE(ControllerComponentVariables::ActiveNetworkProfile.variable.has_value());
        auto status = dm->set_read_only_value(ControllerComponentVariables::ActiveNetworkProfile.component,
                                              ControllerComponentVariables::ActiveNetworkProfile.variable.value(),
                                              AttributeEnum::Actual, std::to_string(slot), "internal");
        ASSERT_EQ(status, SetVariableStatusEnum::Accepted)
            << "Precondition: must be able to set ActiveNetworkProfile to " << slot;
    }

    // Call set_variables and return the result for the single variable
    SetVariableResult set_single_variable(const SetVariableData& svd) {
        auto results = provisioning->set_variables({svd}, "test");
        EXPECT_EQ(results.size(), 1u);
        return results.begin()->second;
    }
};

// SetVariables targeting OcppCsmsUrl on the active slot is rejected
TEST_F(ProvisioningActiveSlotTest, RejectOcppCsmsUrlOnActiveSlot) {
    set_active_slot(1);
    auto result = set_single_variable(make_set_variable_data(1, "OcppCsmsUrl", "wss://new.example.com/ocpp"));
    EXPECT_TRUE(is_rejected_by_active_slot(result)) << "OcppCsmsUrl on active slot must be rejected";
}

// SetVariables targeting SecurityProfile on the active slot is rejected
TEST_F(ProvisioningActiveSlotTest, RejectSecurityProfileOnActiveSlot) {
    set_active_slot(1);
    auto result = set_single_variable(make_set_variable_data(1, "SecurityProfile", "2"));
    EXPECT_TRUE(is_rejected_by_active_slot(result)) << "SecurityProfile on active slot must be rejected";
}

// SetVariables targeting OcppTransport on the active slot is rejected
TEST_F(ProvisioningActiveSlotTest, RejectOcppTransportOnActiveSlot) {
    set_active_slot(1);
    auto result = set_single_variable(make_set_variable_data(1, "OcppTransport", "SOAP"));
    EXPECT_TRUE(is_rejected_by_active_slot(result)) << "OcppTransport on active slot must be rejected";
}

// SetVariables targeting OcppCsmsUrl on a non-active slot passes validation (not rejected by active-slot check)
TEST_F(ProvisioningActiveSlotTest, AllowOcppCsmsUrlOnNonActiveSlot) {
    set_active_slot(1);
    auto result = set_single_variable(make_set_variable_data(2, "OcppCsmsUrl", "wss://backup.example.com/ocpp"));
    EXPECT_FALSE(is_rejected_by_active_slot(result)) << "OcppCsmsUrl on non-active slot must not be blocked";
}

// SetVariables targeting SecurityProfile on a non-active slot passes validation
TEST_F(ProvisioningActiveSlotTest, AllowSecurityProfileOnNonActiveSlot) {
    set_active_slot(1);
    auto result = set_single_variable(make_set_variable_data(2, "SecurityProfile", "2"));
    EXPECT_FALSE(is_rejected_by_active_slot(result)) << "SecurityProfile on non-active slot must not be blocked";
}

// SetVariables targeting OcppTransport on a non-active slot passes validation
TEST_F(ProvisioningActiveSlotTest, AllowOcppTransportOnNonActiveSlot) {
    set_active_slot(1);
    auto result = set_single_variable(make_set_variable_data(2, "OcppTransport", "SOAP"));
    EXPECT_FALSE(is_rejected_by_active_slot(result)) << "OcppTransport on non-active slot must not be blocked";
}

// B09.FR.22: When slot 2 is active, slot 2 writes are rejected. If slot 1 is NOT in the current
// NetworkConfigurationPriority list, SetVariables on slot 1 must be allowed.
TEST_F(ProvisioningActiveSlotTest, ActiveSlot2RejectsSlot2AllowsSlot1) {
    // Narrow NetworkConfigurationPriority to only slot 2 so slot 1 is not a priority member.
    ASSERT_EQ(dm->set_value(ControllerComponentVariables::NetworkConfigurationPriority.component,
                            ControllerComponentVariables::NetworkConfigurationPriority.variable.value(),
                            AttributeEnum::Actual, "2", "test"),
              SetVariableStatusEnum::Accepted);
    set_active_slot(2);

    auto result2 = set_single_variable(make_set_variable_data(2, "OcppCsmsUrl", "wss://new.example.com"));
    EXPECT_TRUE(is_rejected_by_active_slot(result2)) << "Must reject SetVariables on active/priority slot 2";

    auto result1 = set_single_variable(make_set_variable_data(1, "OcppCsmsUrl", "wss://other.example.com"));
    EXPECT_FALSE(is_rejected_by_active_slot(result1))
        << "Must allow SetVariables on slot 1 when it is not in NetworkConfigurationPriority";
}

// ---------------------------------------------------------------------------
// URL/security profile consistency validation
// Tests validate_network_connection_profile (called from
// validate_set_network_configuration_slot) and
// validate_network_configuration_priority.
// ---------------------------------------------------------------------------

// Helper: check if a SetVariableResult was rejected with "InvalidNetworkConf"
static bool is_rejected_invalid_network_conf(const SetVariableResult& result) {
    return result.attributeStatus == SetVariableStatusEnum::Rejected && result.attributeStatusInfo.has_value() &&
           result.attributeStatusInfo->reasonCode.get() == "InvalidNetworkConf";
}

// Helper: write a profile directly to a slot in the device model (bypasses Provisioning validation)
static void write_slot(DeviceModel& dm, int slot, const std::string& url, int security_profile) {
    auto profile = make_basic_profile(security_profile, url);
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(dm, slot, profile, "test"));
}

// Helper: build a SetVariableData targeting NetworkConfigurationPriority
static SetVariableData make_priority_set_variable_data(const std::string& value) {
    SetVariableData svd;
    svd.attributeValue = value;
    svd.component = ControllerComponentVariables::NetworkConfigurationPriority.component;
    svd.variable = ControllerComponentVariables::NetworkConfigurationPriority.variable.value();
    return svd;
}

// --- URL/Security mismatch via SetVariables on individual slot variables ---

// Slot 2 has ws:// URL; raising SecurityProfile to 2 must be rejected (ws:// requires profile < 2)
TEST_F(ProvisioningActiveSlotTest, RejectSecurityProfileUpgradeWithWsUrl) {
    set_active_slot(1);
    write_slot(*dm, 2, "ws://csms.example.com/ocpp", 1);

    auto result = set_single_variable(make_set_variable_data(2, "SecurityProfile", "2"));
    EXPECT_TRUE(is_rejected_invalid_network_conf(result))
        << "SecurityProfile=2 with ws:// URL must be rejected as InvalidNetworkConf";
}

// Slot 2 has wss:// + SecurityProfile=2; changing URL to ws:// must be rejected
TEST_F(ProvisioningActiveSlotTest, RejectUrlDowngradeToWsWithHighSecurityProfile) {
    set_active_slot(1);
    write_slot(*dm, 2, "wss://csms.example.com/ocpp", 2);

    auto result = set_single_variable(make_set_variable_data(2, "OcppCsmsUrl", "ws://csms.example.com/ocpp"));
    EXPECT_TRUE(is_rejected_invalid_network_conf(result))
        << "ws:// URL with SecurityProfile=2 must be rejected as InvalidNetworkConf";
}

// Slot 2 has wss:// URL; raising SecurityProfile to 2 is valid (wss:// requires profile >= 2)
TEST_F(ProvisioningActiveSlotTest, AllowSecurityProfileUpgradeWithWssUrl) {
    set_active_slot(1);
    write_slot(*dm, 2, "wss://csms.example.com/ocpp", 1);

    // Mock certificates as available so the profile upgrade is not rejected for missing certs
    ON_CALL(evse_security, is_ca_certificate_installed(testing::_)).WillByDefault(testing::Return(true));

    auto result = set_single_variable(make_set_variable_data(2, "SecurityProfile", "2"));
    EXPECT_FALSE(is_rejected_invalid_network_conf(result)) << "SecurityProfile=2 with wss:// URL should be accepted";
}

// Slot 2 has SecurityProfile=1; changing URL to ws:// is valid (ws:// allowed with profile < 2)
TEST_F(ProvisioningActiveSlotTest, AllowUrlChangeWhenSecurityProfileConsistent) {
    set_active_slot(1);
    write_slot(*dm, 2, "wss://csms.example.com/ocpp", 1);

    auto result = set_single_variable(make_set_variable_data(2, "OcppCsmsUrl", "ws://new.example.com/ocpp"));
    EXPECT_FALSE(is_rejected_invalid_network_conf(result)) << "ws:// URL with SecurityProfile=1 should be accepted";
}

// --- Certificate requirement checks ---

// SecurityProfile=3 requires a CSMS Leaf Certificate; reject if not installed
TEST_F(ProvisioningActiveSlotTest, RejectSecurityProfile3WithoutLeafCert) {
    set_active_slot(1);
    write_slot(*dm, 2, "wss://csms.example.com/ocpp", 1);

    // Mock: leaf cert not available
    GetCertificateInfoResult no_leaf;
    no_leaf.status = GetCertificateInfoStatus::NotFound;
    ON_CALL(evse_security, get_leaf_certificate_info(testing::_, testing::_)).WillByDefault(testing::Return(no_leaf));
    ON_CALL(evse_security, is_ca_certificate_installed(testing::_)).WillByDefault(testing::Return(true));

    auto result = set_single_variable(make_set_variable_data(2, "SecurityProfile", "3"));
    EXPECT_TRUE(is_rejected_invalid_network_conf(result))
        << "SecurityProfile=3 without CSMS Leaf Certificate must be rejected";
}

// SecurityProfile=2 requires a CSMS Root CA; reject if not installed
TEST_F(ProvisioningActiveSlotTest, RejectSecurityProfile2WithoutRootCa) {
    set_active_slot(1);
    write_slot(*dm, 2, "wss://csms.example.com/ocpp", 1);

    // Mock: root CA not available
    ON_CALL(evse_security, is_ca_certificate_installed(testing::_)).WillByDefault(testing::Return(false));

    auto result = set_single_variable(make_set_variable_data(2, "SecurityProfile", "2"));
    EXPECT_TRUE(is_rejected_invalid_network_conf(result)) << "SecurityProfile=2 without CSMS Root CA must be rejected";
}

// --- Priority list validation ---

// Priority list referencing a slot with ws:// + SecurityProfile=2 must be rejected
TEST_F(ProvisioningActiveSlotTest, RejectPriorityWithMismatchedSlot) {
    set_active_slot(1);
    // Write an inconsistent profile to slot 2 directly in the DM (bypass validation)
    auto cv_url = NetworkConfigurationComponentVariables::get_component_variable(
        2, NetworkConfigurationComponentVariables::OcppCsmsUrl);
    dm->set_value(cv_url.component, cv_url.variable.value(), AttributeEnum::Actual, "ws://csms.example.com/ocpp",
                  "test");
    auto cv_sp = NetworkConfigurationComponentVariables::get_component_variable(
        2, NetworkConfigurationComponentVariables::SecurityProfile);
    dm->set_value(cv_sp.component, cv_sp.variable.value(), AttributeEnum::Actual, "2", "test");

    auto result = set_single_variable(make_priority_set_variable_data("1,2"));
    EXPECT_TRUE(is_rejected_invalid_network_conf(result))
        << "Priority list with ws:// + SecurityProfile=2 slot must be rejected";
}

// Priority list where all slots are consistent should be accepted
TEST_F(ProvisioningActiveSlotTest, AcceptPriorityWithConsistentSlots) {
    set_active_slot(1);
    // Ensure both slots have consistent URL/security configs (ws:// with SecurityProfile=1)
    write_slot(*dm, 1, "ws://csms.example.com/ocpp", 1);
    write_slot(*dm, 2, "ws://backup.example.com/ocpp", 1);

    auto result = set_single_variable(make_priority_set_variable_data("1,2"));
    EXPECT_NE(result.attributeStatus, SetVariableStatusEnum::Rejected)
        << "Priority list with consistent slots should not be rejected";
}

// ---------------------------------------------------------------------------
// B09.FR.26/.27: A SetVariablesRequest that changes the global SecurityCtrlr
// Identity / BasicAuthPassword SHALL clear the corresponding per-slot override
// on the active NetworkConfiguration slot, so subsequent reads fall back to the
// global per FR.16.
// ---------------------------------------------------------------------------

// Helper: build a SetVariableData targeting a SecurityCtrlr variable
static SetVariableData make_security_ctrlr_set_variable_data(const std::string& variable_name,
                                                             const std::string& value) {
    SetVariableData svd;
    svd.attributeValue = value;
    svd.component.name = "SecurityCtrlr";
    svd.variable.name = variable_name;
    return svd;
}

// B09.FR.26: changing global SecurityCtrlr.Identity must clear per-slot Identity on the active slot
TEST_F(ProvisioningActiveSlotTest, GlobalSecurityCtrlrIdentityChangeClearsActiveSlotPerSlotIdentity) {
    // Arrange: slot 1 has a per-slot Identity "old-per-slot"
    auto profile = make_basic_profile();
    profile.identity = "old-per-slot";
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"));
    set_active_slot(1);

    // Precondition: per-slot Identity is set
    auto before = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(before->identity.has_value());
    ASSERT_EQ(before->identity->get(), "old-per-slot");

    // Act: SetVariables on SecurityCtrlr.Identity (global)
    auto result = set_single_variable(make_security_ctrlr_set_variable_data("Identity", "new-global"));
    ASSERT_EQ(result.attributeStatus, SetVariableStatusEnum::Accepted);

    // Assert: per-slot Identity for slot 1 is cleared (not visible in read)
    auto after = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(after.has_value());
    EXPECT_FALSE(after->identity.has_value())
        << "Global SecurityCtrlr.Identity change must clear per-slot Identity (B09.FR.26)";
}

// B09.FR.27: changing global SecurityCtrlr.BasicAuthPassword must clear the per-slot override on
// the active slot. Baseline: no per-slot password set, global change must not be mirrored onto
// per-slot, and reads continue to fall back to SecurityCtrlr per B09.FR.16.
TEST_F(ProvisioningActiveSlotTest, GlobalBasicAuthPasswordChangeClearsActiveSlotPerSlotPassword) {
    // Arrange: no per-slot BasicAuthPassword
    auto profile = make_basic_profile();
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"));
    set_active_slot(1);

    auto before = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(before.has_value());
    ASSERT_FALSE(before->basicAuthPassword.has_value());

    // Act: SetVariables on SecurityCtrlr.BasicAuthPassword (global)
    auto result =
        set_single_variable(make_security_ctrlr_set_variable_data("BasicAuthPassword", "NewGlobalPassword12"));
    ASSERT_EQ(result.attributeStatus, SetVariableStatusEnum::Accepted);

    // Assert: per-slot BasicAuthPassword on slot 1 remains empty (clear semantic) — it was not
    // mirrored with the new global, so reads fall back to SecurityCtrlr per B09.FR.16.
    auto after = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(after.has_value());
    EXPECT_FALSE(after->basicAuthPassword.has_value())
        << "Global SecurityCtrlr.BasicAuthPassword change must not mirror onto per-slot (B09.FR.27)";
}

// B09.FR.27 regression: when the per-slot BasicAuthPassword has a non-empty value, a global
// SecurityCtrlr.BasicAuthPassword change must clear the per-slot row. The per-slot variable's
// minLimit=16 forbids setting "" via the regular validate_value path, so this exercises the
// clear_value bypass — without it the per-slot row would stay at the old password and FR.16
// fallback would never engage.
TEST_F(ProvisioningActiveSlotTest, GlobalBasicAuthPasswordChangeClearsNonEmptyActiveSlotPerSlotPassword) {
    // Arrange: slot 1 has a 20-char per-slot BasicAuthPassword (well above minLimit=16)
    auto profile = make_basic_profile();
    profile.basicAuthPassword = CiString<64>("OldPerSlotPassword12");
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"));
    set_active_slot(1);

    // Precondition: per-slot BasicAuthPassword is set
    auto before = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(before->basicAuthPassword.has_value());
    ASSERT_EQ(before->basicAuthPassword->get(), "OldPerSlotPassword12");

    // Act: SetVariables on SecurityCtrlr.BasicAuthPassword (global, also 16+ chars)
    auto result =
        set_single_variable(make_security_ctrlr_set_variable_data("BasicAuthPassword", "NewGlobalPassword12"));
    ASSERT_EQ(result.attributeStatus, SetVariableStatusEnum::Accepted);

    // Assert: per-slot BasicAuthPassword for slot 1 was cleared (read returns nullopt)
    auto after = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(after.has_value());
    EXPECT_FALSE(after->basicAuthPassword.has_value())
        << "Global SecurityCtrlr.BasicAuthPassword change must clear non-empty per-slot password (B09.FR.27)";

    // Assert FR.16 fallback: SecurityCtrlr.BasicAuthPassword now holds the new global, so any
    // resolve that consults it would surface the new value, not the stale per-slot password.
    const auto global_pwd = dm->get_value<std::string>(ControllerComponentVariables::BasicAuthPassword);
    EXPECT_EQ(global_pwd, "NewGlobalPassword12");
}

// B09.FR.16: After clearing the per-slot Identity (via FR.26), reads must fall back to the
// SecurityCtrlr global Identity. We verify this at the DM level: the global was updated and
// the per-slot Identity is empty, so the resolve fallback path produces the global.
TEST_F(ProvisioningActiveSlotTest, ResolveIdentityFallsBackToSecurityCtrlrAfterClear) {
    // Arrange: slot 1 has a per-slot Identity, SecurityCtrlr global Identity is something else
    auto profile = make_basic_profile();
    profile.identity = "old-per-slot";
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"));
    set_active_slot(1);

    // Act: change the global Identity via SetVariables (this must clear per-slot per FR.26)
    auto result = set_single_variable(make_security_ctrlr_set_variable_data("Identity", "new-global"));
    ASSERT_EQ(result.attributeStatus, SetVariableStatusEnum::Accepted);

    // Assert: SecurityCtrlr.Identity (the global) holds the new value
    const auto global_identity = dm->get_value<std::string>(ControllerComponentVariables::SecurityCtrlrIdentity);
    EXPECT_EQ(global_identity, "new-global");

    // Assert: per-slot Identity is cleared, so resolve_identity falls back to global per FR.16
    auto after = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(after.has_value());
    EXPECT_FALSE(after->identity.has_value())
        << "Per-slot Identity must be cleared so resolve_identity falls back to SecurityCtrlr (B09.FR.16)";
}

// ---------------------------------------------------------------------------
// B09.FR.28: GetVariables on SecurityCtrlr.Identity must return the per-slot
// Identity for the active NetworkConfiguration slot when that slot is listed in
// NetworkConfigurationPriority. If the active slot has no per-slot Identity, or
// has been pruned from NetworkConfigurationPriority, the global value is
// returned.
// ---------------------------------------------------------------------------

// Helper: build a GetVariableData targeting SecurityCtrlr.Identity
static GetVariableData make_security_ctrlr_identity_get_variable_data() {
    GetVariableData gvd;
    gvd.component = ControllerComponentVariables::SecurityCtrlrIdentity.component;
    gvd.variable = ControllerComponentVariables::SecurityCtrlrIdentity.variable.value();
    return gvd;
}

// FR.28: active slot is in NetworkConfigurationPriority and has a per-slot Identity
// → GetVariables returns the per-slot Identity, not the global SecurityCtrlr.Identity.
TEST_F(ProvisioningActiveSlotTest, GetVariablesIdentityReturnsPerSlotForActiveSlotInPriority) {
    // Arrange: global Identity = "global-id", per-slot Identity on slot 1 = "per-slot-id".
    // Default DM has NetworkConfigurationPriority="1".
    ASSERT_EQ(dm->set_read_only_value(ControllerComponentVariables::SecurityCtrlrIdentity.component,
                                      ControllerComponentVariables::SecurityCtrlrIdentity.variable.value(),
                                      AttributeEnum::Actual, "global-id", "test"),
              SetVariableStatusEnum::Accepted);
    auto profile = make_basic_profile();
    profile.identity = "per-slot-id";
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"));
    set_active_slot(1);

    // Act
    auto results = provisioning->get_variables({make_security_ctrlr_identity_get_variable_data()});

    // Assert
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].attributeStatus, GetVariableStatusEnum::Accepted);
    ASSERT_TRUE(results[0].attributeValue.has_value());
    EXPECT_EQ(results[0].attributeValue->get(), "per-slot-id")
        << "FR.28: per-slot Identity must override the global when active slot is in priority";
}

// FR.28: active slot is in NetworkConfigurationPriority but has no per-slot Identity
// → GetVariables returns the global SecurityCtrlr.Identity.
TEST_F(ProvisioningActiveSlotTest, GetVariablesIdentityReturnsGlobalWhenPerSlotEmpty) {
    // Arrange: global Identity = "global-id", no per-slot Identity on slot 1.
    ASSERT_EQ(dm->set_read_only_value(ControllerComponentVariables::SecurityCtrlrIdentity.component,
                                      ControllerComponentVariables::SecurityCtrlrIdentity.variable.value(),
                                      AttributeEnum::Actual, "global-id", "test"),
              SetVariableStatusEnum::Accepted);
    auto profile = make_basic_profile();
    // profile.identity intentionally not set
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"));
    set_active_slot(1);

    // Act
    auto results = provisioning->get_variables({make_security_ctrlr_identity_get_variable_data()});

    // Assert
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].attributeStatus, GetVariableStatusEnum::Accepted);
    ASSERT_TRUE(results[0].attributeValue.has_value());
    EXPECT_EQ(results[0].attributeValue->get(), "global-id")
        << "FR.28: with no per-slot Identity, the global value must be returned";
}

// FR.28: active slot has a per-slot Identity but is NOT in NetworkConfigurationPriority
// (e.g. after a security-escalation prune) → GetVariables returns the global, not the
// stale per-slot value.
TEST_F(ProvisioningActiveSlotTest, GetVariablesIdentityReturnsGlobalWhenActiveSlotNotInPriority) {
    // Arrange: write per-slot Identity on slot 1, then narrow priority to slot 2 only.
    ASSERT_EQ(dm->set_read_only_value(ControllerComponentVariables::SecurityCtrlrIdentity.component,
                                      ControllerComponentVariables::SecurityCtrlrIdentity.variable.value(),
                                      AttributeEnum::Actual, "global-id", "test"),
              SetVariableStatusEnum::Accepted);
    auto profile = make_basic_profile();
    profile.identity = "per-slot-id";
    ASSERT_TRUE(NetworkConfigurationComponentVariables::write_profile_to_device_model(*dm, 1, profile, "test"));
    ASSERT_EQ(dm->set_value(ControllerComponentVariables::NetworkConfigurationPriority.component,
                            ControllerComponentVariables::NetworkConfigurationPriority.variable.value(),
                            AttributeEnum::Actual, "2", "test"),
              SetVariableStatusEnum::Accepted);
    set_active_slot(1);

    // Act
    auto results = provisioning->get_variables({make_security_ctrlr_identity_get_variable_data()});

    // Assert
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].attributeStatus, GetVariableStatusEnum::Accepted);
    ASSERT_TRUE(results[0].attributeValue.has_value());
    EXPECT_EQ(results[0].attributeValue->get(), "global-id")
        << "FR.28: per-slot Identity must not leak when active slot is not in priority";
}

// ---------------------------------------------------------------------------
// handle_set_network_profile_req validation branches.
// Drives Provisioning::handle_message with SetNetworkProfile to cover the
// reasonCode taxonomy: InternalError (no callback / DM write fails),
// InvalidConfSlot, NoSecurityDowngrade, InvalidNetworkConf (callback /
// validation reject), Failed (DM write fails), Accepted (happy path).
// ---------------------------------------------------------------------------

class ProvisioningSetNetworkProfileTest : public ::testing::Test {
protected:
    DeviceModelTestHelper dm_helper;
    DeviceModel* dm{nullptr};

    ::testing::NiceMock<MockMessageDispatcher> mock_dispatcher;
    ::testing::NiceMock<ConnectivityManagerMock> connectivity_manager;
    std::unique_ptr<EvseManagerFake> evse_manager;
    ::testing::NiceMock<DatabaseHandlerMock> db_handler;
    ocpp::EvseSecurityMock evse_security;
    ::testing::NiceMock<ComponentStateManagerMock> component_state_manager;
    std::atomic<OcppProtocolVersion> ocpp_version{OcppProtocolVersion::v201};

    ::testing::NiceMock<OcspUpdaterMock> ocsp_updater;
    ::testing::NiceMock<AvailabilityMock> availability;
    ::testing::NiceMock<MeterValuesMock> meter_values;
    ::testing::NiceMock<SecurityMock> security;
    ::testing::NiceMock<DiagnosticsMock> diagnostics;
    ::testing::NiceMock<TransactionMock> transaction;
    std::atomic<RegistrationStatusEnum> registration_status{RegistrationStatusEnum::Accepted};

    boost::asio::io_context io_context;
    std::optional<TariffMessageCallback> tariff_message_cb;
    std::optional<SetRunningCostCallback> set_running_cost_cb;
    std::optional<DefaultPriceCallback> default_price_cb;

    std::unique_ptr<FunctionalBlockContext> fb_context;
    std::unique_ptr<MessageQueue<MessageType>> message_queue;
    std::unique_ptr<TariffAndCost> tariff_and_cost;
    std::unique_ptr<Provisioning> provisioning;

    ProvisioningSetNetworkProfileTest() {
        dm = dm_helper.get_device_model();
        evse_manager = std::make_unique<EvseManagerFake>(1);
        fb_context =
            std::make_unique<FunctionalBlockContext>(mock_dispatcher, *dm, connectivity_manager, *evse_manager,
                                                     db_handler, evse_security, component_state_manager, ocpp_version);
        MessageQueueConfig<MessageType> mq_config;
        message_queue = std::make_unique<MessageQueue<MessageType>>([](json) { return false; }, mq_config, nullptr);
        tariff_and_cost = std::make_unique<TariffAndCost>(*fb_context, meter_values, tariff_message_cb,
                                                          set_running_cost_cb, default_price_cb, io_context);
    }

    // Build the Provisioning block with the given validate_network_profile_callback. Pass std::nullopt
    // to exercise the no-callback rejection path.
    void make_provisioning(std::optional<ValidateNetworkProfileCallback> validate_cb) {
        provisioning = std::make_unique<Provisioning>(
            *fb_context, *message_queue, ocsp_updater, availability, meter_values, security, diagnostics, transaction,
            std::nullopt, std::nullopt, validate_cb, [](auto, auto) { return true; }, [](auto, auto) {},
            [](auto, auto) { return RequestStartStopStatusEnum::Accepted; }, std::nullopt, *tariff_and_cost,
            registration_status);
    }

    static SetNetworkProfileRequest make_request(int32_t slot, const std::string& url, int security_profile) {
        SetNetworkProfileRequest req;
        req.configurationSlot = slot;
        req.connectionData.ocppCsmsUrl = CiString<2000>(url);
        req.connectionData.securityProfile = security_profile;
        req.connectionData.ocppInterface = OCPPInterfaceEnum::Wired0;
        req.connectionData.ocppTransport = OCPPTransportEnum::JSON;
        req.connectionData.messageTimeout = 30;
        return req;
    }

    static EnhancedMessage<MessageType> make_enhanced(const SetNetworkProfileRequest& req) {
        EnhancedMessage<MessageType> em;
        em.messageType = MessageType::SetNetworkProfile;
        em.message = ocpp::Call<SetNetworkProfileRequest>(req);
        return em;
    }

    // Capture the dispatched response payload from the mock dispatcher into the provided slot.
    void expect_response(SetNetworkProfileResponse& out) {
        EXPECT_CALL(mock_dispatcher, dispatch_call_result(::testing::_))
            .WillOnce(::testing::Invoke([&out](const json& call_result) {
                out = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetNetworkProfileResponse>();
            }));
    }
};

TEST_F(ProvisioningSetNetworkProfileTest, NoCallbackRejectsAsInternalError) {
    make_provisioning(std::nullopt);

    SetNetworkProfileResponse response;
    expect_response(response);

    auto em = make_enhanced(make_request(1, "ws://csms.example.com/ocpp", 1));
    provisioning->handle_message(em);

    EXPECT_EQ(response.status, SetNetworkProfileStatusEnum::Rejected);
    ASSERT_TRUE(response.statusInfo.has_value());
    EXPECT_EQ(response.statusInfo->reasonCode.get(), "InternalError");
}

TEST_F(ProvisioningSetNetworkProfileTest, SlotNotInPriorityValuesListRejectsInvalidConfSlot) {
    make_provisioning([](auto, auto) { return SetNetworkProfileStatusEnum::Accepted; });

    SetNetworkProfileResponse response;
    expect_response(response);

    // The default DM has NetworkConfigurationPriority valuesList limited to slots 1..2;
    // slot 99 falls outside it and must be rejected before any further validation runs.
    auto em = make_enhanced(make_request(99, "ws://csms.example.com/ocpp", 1));
    provisioning->handle_message(em);

    EXPECT_EQ(response.status, SetNetworkProfileStatusEnum::Rejected);
    ASSERT_TRUE(response.statusInfo.has_value());
    EXPECT_EQ(response.statusInfo->reasonCode.get(), "InvalidConfSlot");
}

TEST_F(ProvisioningSetNetworkProfileTest, LowerSecurityProfileRejectsNoSecurityDowngrade) {
    // Raise the active SecurityProfile so an incoming profile=1 is a downgrade.
    ASSERT_EQ(dm->set_read_only_value(ControllerComponentVariables::SecurityProfile.component,
                                      ControllerComponentVariables::SecurityProfile.variable.value(),
                                      AttributeEnum::Actual, "2", "internal"),
              SetVariableStatusEnum::Accepted);
    make_provisioning([](auto, auto) { return SetNetworkProfileStatusEnum::Accepted; });

    SetNetworkProfileResponse response;
    expect_response(response);

    auto em = make_enhanced(make_request(1, "wss://csms.example.com/ocpp", 1));
    provisioning->handle_message(em);

    EXPECT_EQ(response.status, SetNetworkProfileStatusEnum::Rejected);
    ASSERT_TRUE(response.statusInfo.has_value());
    EXPECT_EQ(response.statusInfo->reasonCode.get(), "NoSecurityDowngrade");
}

TEST_F(ProvisioningSetNetworkProfileTest, CallbackRejectsAsInvalidNetworkConf) {
    make_provisioning([](auto, auto) { return SetNetworkProfileStatusEnum::Rejected; });

    SetNetworkProfileResponse response;
    expect_response(response);

    auto em = make_enhanced(make_request(1, "ws://csms.example.com/ocpp", 1));
    provisioning->handle_message(em);

    EXPECT_EQ(response.status, SetNetworkProfileStatusEnum::Rejected);
    ASSERT_TRUE(response.statusInfo.has_value());
    EXPECT_EQ(response.statusInfo->reasonCode.get(), "InvalidNetworkConf");
}

TEST_F(ProvisioningSetNetworkProfileTest, ConnectivityManagerWriteFailureMapsToFailed) {
    make_provisioning([](auto, auto) { return SetNetworkProfileStatusEnum::Accepted; });

    EXPECT_CALL(connectivity_manager, set_network_profile(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(false));

    SetNetworkProfileResponse response;
    expect_response(response);

    auto em = make_enhanced(make_request(1, "ws://csms.example.com/ocpp", 1));
    provisioning->handle_message(em);

    EXPECT_EQ(response.status, SetNetworkProfileStatusEnum::Failed);
    ASSERT_TRUE(response.statusInfo.has_value());
    EXPECT_EQ(response.statusInfo->reasonCode.get(), "InternalError");
}

TEST_F(ProvisioningSetNetworkProfileTest, HappyPathReturnsAccepted) {
    make_provisioning([](auto, auto) { return SetNetworkProfileStatusEnum::Accepted; });

    EXPECT_CALL(connectivity_manager, set_network_profile(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(true));

    SetNetworkProfileResponse response;
    expect_response(response);

    auto em = make_enhanced(make_request(1, "ws://csms.example.com/ocpp", 1));
    provisioning->handle_message(em);

    EXPECT_EQ(response.status, SetNetworkProfileStatusEnum::Accepted);
}

// ---------------------------------------------------------------------------
// Migration survives missing NetworkConfiguration JSON files
//
// `InitDeviceModelDb::initialize_database` used to delete every component row
// in the database that no longer had a matching JSON in the component config
// directory. That pruning broke blob migration on targets where the operator
// updated the install without re-shipping `NetworkConfiguration_<N>.json`: the
// per-slot device-model components vanished, the blob retry never had a
// migration target, and the operator's connection profiles became unrecoverable.
//
// The fix replaced the prune with a warning, so the orphan rows are preserved
// and a later `migrate_from_blob_if_needed` can still write into the existing
// per-slot components. The tests below exercise that behavior end-to-end with
// an on-disk database (so `InitDeviceModelDb`'s ctor sets `database_exists`
// via the filesystem check on the second init, without test-only hacks).
// ---------------------------------------------------------------------------

class MigrationWithoutJsonTest : public ::testing::Test {
protected:
    std::filesystem::path db_path;
    std::string config_path = "./resources/example_config/v2/component_config";
    std::string migration_files_path = "./resources/v2/device_model_migration_files";

    void SetUp() override {
        const std::string unique = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        db_path = std::filesystem::temp_directory_path() / ("libocpp_no_prune_" + unique + ".db");
        std::error_code ec;
        std::filesystem::remove(db_path, ec);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(db_path, ec);
    }

    // Return a copy of the component config map with every NetworkConfiguration
    // entry whose instance is `instance_to_drop` removed. Simulates a target
    // where `NetworkConfiguration_<instance>.json` was not installed.
    static std::map<ComponentKey, std::vector<DeviceModelVariable>>
    drop_network_configuration_instance(std::map<ComponentKey, std::vector<DeviceModelVariable>> configs,
                                        const std::string& instance_to_drop) {
        for (auto it = configs.begin(); it != configs.end();) {
            const auto& key = it->first;
            const bool is_network_config = key.name == "NetworkConfiguration";
            const bool matches_instance = key.instance.has_value() && key.instance.value() == instance_to_drop;
            if (is_network_config && matches_instance) {
                it = configs.erase(it);
            } else {
                ++it;
            }
        }
        return configs;
    }
};

// The first init creates NetworkConfiguration_1 and _2 from the JSON config.
// The second init runs with a config map that lacks NetworkConfiguration_2,
// simulating the JSON file being absent. The orphan row must NOT be removed,
// so that a later blob migration can still write into slot 2.
TEST_F(MigrationWithoutJsonTest, OrphanNetworkConfigurationComponentSurvivesReinit) {
    const auto full = get_all_component_configs(config_path);
    {
        InitDeviceModelDb db(db_path, migration_files_path);
        ASSERT_NO_THROW(db.initialize_database(full, true));
    }

    // Sanity: the first init must have populated NetworkConfiguration_2 from JSON,
    // otherwise the rest of the test is vacuous.
    bool slot_2_in_first_init = false;
    for (const auto& [key, _vars] : full) {
        if (key.name == "NetworkConfiguration" && key.instance.has_value() && key.instance.value() == "2") {
            slot_2_in_first_init = true;
            break;
        }
    }
    ASSERT_TRUE(slot_2_in_first_init) << "Precondition: example component_config must declare NetworkConfiguration_2";

    const auto reduced = drop_network_configuration_instance(full, "2");
    {
        InitDeviceModelDb db(db_path, migration_files_path);
        ASSERT_NO_THROW(db.initialize_database(reduced, false));
    }

    // Open the DB through DeviceModel and confirm slot 2 variables still resolve.
    auto storage = std::make_unique<DeviceModelStorageSqlite>(db_path);
    DeviceModel dm(std::move(storage));

    const auto slot_2_url_cv = NetworkConfigurationComponentVariables::get_component_variable(
        2, NetworkConfigurationComponentVariables::OcppCsmsUrl);
    auto status_after_reinit =
        dm.set_value(slot_2_url_cv.component, slot_2_url_cv.variable.value(), AttributeEnum::Actual,
                     "wss://orphan-survived.example.com/ocpp", "test", true);
    EXPECT_EQ(status_after_reinit, SetVariableStatusEnum::Accepted)
        << "NetworkConfiguration_2 must still be writable after the JSON config was removed";
}

// Same scenario but exercises the full migration path: seed a legacy blob with
// a profile for slot 2 in a fresh DB, then re-init with the reduced config (no
// NetworkConfiguration_2.json), then run migrate_from_blob_if_needed. The
// migration must succeed because the orphan slot 2 row was preserved.
TEST_F(MigrationWithoutJsonTest, MigrationSucceedsWhenNetworkConfigurationJsonRemoved) {
    const auto full = get_all_component_configs(config_path);
    {
        InitDeviceModelDb db(db_path, migration_files_path);
        ASSERT_NO_THROW(db.initialize_database(full, true));
    }

    // Seed the legacy blob and clear slot 2's defaults so the migration must
    // actually write into the slot (rather than skipping it as already populated).
    {
        auto storage = std::make_unique<DeviceModelStorageSqlite>(db_path);
        DeviceModel dm(std::move(storage));
        NetworkConfigurationComponentVariables::clear_slot_in_device_model(dm, 1);
        NetworkConfigurationComponentVariables::clear_slot_in_device_model(dm, 2);

        SetNetworkProfileRequest req;
        req.configurationSlot = 2;
        req.connectionData = make_basic_profile(1, "wss://post-orphan.example.com/ocpp");
        seed_blob(dm, make_blob({req}));
    }

    // Re-init the DB without NetworkConfiguration_2.json. Pre-fix this would have
    // deleted the slot 2 component and the migration below would silently fail.
    const auto reduced = drop_network_configuration_instance(full, "2");
    {
        InitDeviceModelDb db(db_path, migration_files_path);
        ASSERT_NO_THROW(db.initialize_database(reduced, false));
    }

    auto storage = std::make_unique<DeviceModelStorageSqlite>(db_path);
    DeviceModel dm(std::move(storage));

    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(dm);

    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(dm, 2);
    ASSERT_TRUE(result.has_value())
        << "Slot 2 must be populated after migration even though NetworkConfiguration_2.json is missing";
    EXPECT_EQ(result->ocppCsmsUrl.get(), "wss://post-orphan.example.com/ocpp");
    EXPECT_EQ(read_blob(dm).size(), 0u) << "Blob must be cleared once migration writes every profile into its DM slot";
}

// Drops NetworkConfiguration_1 and _2 from the reduced config but keeps the
// previously inserted rows. Both slots must survive the reinit (one warning
// per orphan, no deletions), and migration into both slots must complete.
TEST_F(MigrationWithoutJsonTest, MigrationSucceedsWhenAllNetworkConfigurationJsonRemoved) {
    const auto full = get_all_component_configs(config_path);
    {
        InitDeviceModelDb db(db_path, migration_files_path);
        ASSERT_NO_THROW(db.initialize_database(full, true));
    }

    {
        auto storage = std::make_unique<DeviceModelStorageSqlite>(db_path);
        DeviceModel dm(std::move(storage));
        NetworkConfigurationComponentVariables::clear_slot_in_device_model(dm, 1);
        NetworkConfigurationComponentVariables::clear_slot_in_device_model(dm, 2);

        SetNetworkProfileRequest req1;
        req1.configurationSlot = 1;
        req1.connectionData = make_basic_profile(1, "wss://no-json-slot1.example.com/ocpp");

        SetNetworkProfileRequest req2;
        req2.configurationSlot = 2;
        req2.connectionData = make_basic_profile(2, "wss://no-json-slot2.example.com/ocpp");

        seed_blob(dm, make_blob({req1, req2}));
    }

    auto reduced = drop_network_configuration_instance(full, "1");
    reduced = drop_network_configuration_instance(reduced, "2");
    {
        InitDeviceModelDb db(db_path, migration_files_path);
        ASSERT_NO_THROW(db.initialize_database(reduced, false));
    }

    auto storage = std::make_unique<DeviceModelStorageSqlite>(db_path);
    DeviceModel dm(std::move(storage));

    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(dm);

    auto r1 = NetworkConfigurationComponentVariables::read_profile_from_device_model(dm, 1);
    auto r2 = NetworkConfigurationComponentVariables::read_profile_from_device_model(dm, 2);
    ASSERT_TRUE(r1.has_value()) << "Slot 1 must be populated even with NetworkConfiguration_1.json missing";
    ASSERT_TRUE(r2.has_value()) << "Slot 2 must be populated even with NetworkConfiguration_2.json missing";
    EXPECT_EQ(r1->ocppCsmsUrl.get(), "wss://no-json-slot1.example.com/ocpp");
    EXPECT_EQ(r2->ocppCsmsUrl.get(), "wss://no-json-slot2.example.com/ocpp");
    EXPECT_EQ(read_blob(dm).size(), 0u);
}

// Direct storage-level coverage of the create entry point: with no NetworkConfiguration
// components in the DB at all, the new method must insert a complete COMPONENT + VARIABLE +
// VARIABLE_CHARACTERISTICS + VARIABLE_ATTRIBUTE tree under the new instance, and the next
// `get_device_model()` snapshot must contain a NetworkConfiguration entry at that instance.
TEST_F(MigrationWithoutJsonTest, StorageCreatesSlotFromEmbeddedSchema) {
    auto reduced = get_all_component_configs(config_path);
    reduced = drop_network_configuration_instance(reduced, "1");
    reduced = drop_network_configuration_instance(reduced, "2");

    {
        InitDeviceModelDb db(db_path, migration_files_path);
        ASSERT_NO_THROW(db.initialize_database(reduced, true));
    }

    DeviceModelStorageSqlite storage(db_path);

    ASSERT_TRUE(storage.create_network_configuration_slot_from_default_schema(3));

    // Repeat create on the same slot must be a no-op (returns false). Refuse to overwrite.
    EXPECT_FALSE(storage.create_network_configuration_slot_from_default_schema(3));

    const auto model = storage.get_device_model();
    const VariableMap* slot_3_vars = nullptr;
    for (const auto& [component, variables] : model) {
        if (component.name == "NetworkConfiguration" && component.instance.has_value() &&
            component.instance.value() == "3") {
            slot_3_vars = &variables;
            break;
        }
    }
    ASSERT_NE(slot_3_vars, nullptr) << "Created slot must appear in the next get_device_model() snapshot";
    EXPECT_GT(slot_3_vars->size(), 0u) << "Created slot must expose at least one variable";

    // Pin embedded JSON against accidental field removal: assert the well-known per-slot variables
    // are present by name.
    const auto has_var_named = [&](const std::string& name) {
        for (const auto& [variable, _] : *slot_3_vars) {
            if (variable.name.get() == name) {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(has_var_named("OcppCsmsUrl"));
    EXPECT_TRUE(has_var_named("SecurityProfile"));
    EXPECT_TRUE(has_var_named("OcppInterface"));
    EXPECT_TRUE(has_var_named("OcppTransport"));
    EXPECT_TRUE(has_var_named("MessageTimeout"));
    EXPECT_TRUE(has_var_named("Identity"));
    EXPECT_TRUE(has_var_named("BasicAuthPassword"));
}

// First boot on a target that ships without any NetworkConfiguration_<N>.json. The device model
// holds zero NetworkConfiguration components, so the clone path has no template to copy from.
// Migration must instead create the slot from the embedded default schema.
TEST_F(MigrationWithoutJsonTest, MigrationCreatesSlotWhenNoTemplateExists) {
    auto reduced = get_all_component_configs(config_path);
    reduced = drop_network_configuration_instance(reduced, "1");
    reduced = drop_network_configuration_instance(reduced, "2");

    {
        InitDeviceModelDb db(db_path, migration_files_path);
        ASSERT_NO_THROW(db.initialize_database(reduced, true));
    }

    {
        auto storage = std::make_unique<DeviceModelStorageSqlite>(db_path);
        DeviceModel dm(std::move(storage));

        SetNetworkProfileRequest req;
        req.configurationSlot = 3;
        req.connectionData = make_basic_profile(1, "wss://bootstrap.example.com/ocpp");
        seed_blob(dm, make_blob({req}));
    }

    auto storage = std::make_unique<DeviceModelStorageSqlite>(db_path);
    DeviceModel dm(std::move(storage));

    // Precondition: NO NetworkConfiguration slot should resolve before migration.
    for (int probe_slot = 1; probe_slot <= 32; ++probe_slot) {
        const auto probe_cv = NetworkConfigurationComponentVariables::get_component_variable(
            probe_slot, NetworkConfigurationComponentVariables::OcppCsmsUrl);
        ASSERT_FALSE(dm.get_variable_meta_data(probe_cv.component, probe_cv.variable.value()).has_value())
            << "Precondition: NetworkConfiguration_" << probe_slot << " must be absent before create-path migration";
    }

    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(dm);

    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(dm, 3);
    ASSERT_TRUE(result.has_value()) << "Slot 3 must be readable after create from the embedded schema";
    EXPECT_EQ(result->ocppCsmsUrl.get(), "wss://bootstrap.example.com/ocpp");
    EXPECT_EQ(read_blob(dm).size(), 0u);
}

// Multi-slot create in one migration pass. Blob seeds profiles for slots 3 and 5 on a
// target that ships without NetworkConfiguration_<N>.json. Migration must create both
// slots from the embedded schema and populate each.
TEST_F(MigrationWithoutJsonTest, MigrationCreatesMultipleSlotsInOnePass) {
    auto reduced = get_all_component_configs(config_path);
    reduced = drop_network_configuration_instance(reduced, "1");
    reduced = drop_network_configuration_instance(reduced, "2");

    {
        InitDeviceModelDb db(db_path, migration_files_path);
        ASSERT_NO_THROW(db.initialize_database(reduced, true));
    }

    {
        auto storage = std::make_unique<DeviceModelStorageSqlite>(db_path);
        DeviceModel dm(std::move(storage));

        SetNetworkProfileRequest req3;
        req3.configurationSlot = 3;
        req3.connectionData = make_basic_profile(1, "wss://multi-slot-3.example.com/ocpp");

        SetNetworkProfileRequest req5;
        req5.configurationSlot = 5;
        req5.connectionData = make_basic_profile(1, "wss://multi-slot-5.example.com/ocpp");

        seed_blob(dm, make_blob({req3, req5}));
    }

    auto storage = std::make_unique<DeviceModelStorageSqlite>(db_path);
    DeviceModel dm(std::move(storage));

    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(dm);

    auto r3 = NetworkConfigurationComponentVariables::read_profile_from_device_model(dm, 3);
    auto r5 = NetworkConfigurationComponentVariables::read_profile_from_device_model(dm, 5);
    ASSERT_TRUE(r3.has_value()) << "Slot 3 must be populated after multi-slot create";
    ASSERT_TRUE(r5.has_value()) << "Slot 5 must be populated after multi-slot create";
    EXPECT_EQ(r3->ocppCsmsUrl.get(), "wss://multi-slot-3.example.com/ocpp");
    EXPECT_EQ(r5->ocppCsmsUrl.get(), "wss://multi-slot-5.example.com/ocpp");
    EXPECT_EQ(read_blob(dm).size(), 0u);
}

// SecurityCtrlr.Identity exceeds the per-slot CiString<48> length cap. Migration must skip
// propagation (not crash) and leave the per-slot Identity unset.
TEST_F(NetworkConfigSyncTest, MigrateFromBlobIgnoresOverlongSecurityCtrlrIdentity) {
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    // Build a 49-char Identity (per-slot cap is 48).
    const std::string overlong_identity(49, 'X');
    dm->set_read_only_value(ControllerComponentVariables::SecurityCtrlrIdentity.component,
                            ControllerComponentVariables::SecurityCtrlrIdentity.variable.value(), AttributeEnum::Actual,
                            overlong_identity, "test");

    SetNetworkProfileRequest req;
    req.configurationSlot = 1;
    req.connectionData = make_basic_profile(1, "wss://overlong-identity.example.com/ocpp");
    ASSERT_FALSE(req.connectionData.identity.has_value());

    seed_blob(*dm, make_blob({req}));
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->identity.has_value())
        << "Migration must skip overflowing SecurityCtrlr.Identity (do not truncate, do not propagate)";
    EXPECT_EQ(read_blob(*dm).size(), 0u);
}

// Malformed blob entry: the blob carries one well-formed profile and one that lacks
// `configurationSlot`. The well-formed slot must be populated; the migration must not abort;
// the blob must still be cleared (per the unconditional clear policy).
TEST_F(NetworkConfigSyncTest, MigrateFromBlobSkipsMalformedProfileEntry) {
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*dm, 1);

    // Hand-craft a blob with one valid entry and one malformed (missing configurationSlot).
    json arr = json::array();

    SetNetworkProfileRequest good;
    good.configurationSlot = 1;
    good.connectionData = make_basic_profile(1, "wss://good.example.com/ocpp");
    arr.push_back(json(good));

    // Malformed entry: object with only connectionData, no configurationSlot field.
    json bad;
    bad["connectionData"] = json(make_basic_profile(1, "wss://bad.example.com/ocpp"));
    arr.push_back(bad);

    seed_blob(*dm, arr);
    NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*dm);

    auto result = NetworkConfigurationComponentVariables::read_profile_from_device_model(*dm, 1);
    ASSERT_TRUE(result.has_value()) << "Well-formed profile must be written even when a sibling is malformed";
    EXPECT_EQ(result->ocppCsmsUrl.get(), "wss://good.example.com/ocpp");
    EXPECT_EQ(read_blob(*dm).size(), 0u) << "Blob must be cleared after migration even with malformed siblings";
}

// After `create_network_configuration_slot_from_default_schema` returns, the freshly created
// component must be immediately visible to subsequent `set_value` calls without manual cache
// reload. Pins the in-memory device-model map reload behavior.
TEST_F(MigrationWithoutJsonTest, CreatedSlotVisibleToSetValueWithoutManualReload) {
    auto reduced = get_all_component_configs(config_path);
    reduced = drop_network_configuration_instance(reduced, "1");
    reduced = drop_network_configuration_instance(reduced, "2");

    {
        InitDeviceModelDb db(db_path, migration_files_path);
        ASSERT_NO_THROW(db.initialize_database(reduced, true));
    }

    auto storage = std::make_unique<DeviceModelStorageSqlite>(db_path);
    DeviceModel dm(std::move(storage));

    ASSERT_TRUE(dm.create_network_configuration_slot_from_default_schema(7));

    const auto cv = NetworkConfigurationComponentVariables::get_component_variable(
        7, NetworkConfigurationComponentVariables::OcppCsmsUrl);
    const auto status = dm.set_value(cv.component, cv.variable.value(), AttributeEnum::Actual,
                                     "wss://bootstrap-visible.example.com/ocpp", "internal");
    EXPECT_EQ(status, SetVariableStatusEnum::Accepted)
        << "set_value into a freshly-created slot must succeed without manual cache reload";
}

} // namespace ocpp::v2
