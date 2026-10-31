// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <ocpp/v2/ocpp16_key_mappings.hpp>
#include <test_temp_paths.hpp>

namespace ocpp::v2 {

class Ocpp16KeyMappingsTest : public ::testing::Test {
protected:
    std::filesystem::path test_dir;

    void SetUp() override {
        test_dir = libocpp_test::unique_temp_directory("ocpp16_key_mapping_tests");
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir, ec);
    }

    std::filesystem::path write_yaml_file(const std::string& filename, const std::string& yaml_content) {
        const auto file_path = test_dir / filename;
        std::ofstream ofs(file_path);
        ofs << yaml_content;
        ofs.close();
        return file_path;
    }
};

TEST_F(Ocpp16KeyMappingsTest, schema_contains_mappings_root_property) {
    const auto schema = get_ocpp16_key_mapping_schema();
    ASSERT_TRUE(schema.contains("properties"));
    ASSERT_TRUE(schema.at("properties").contains("mappings"));
}

TEST_F(Ocpp16KeyMappingsTest, load_valid_yaml_returns_mappings) {
    const auto yaml_file = write_yaml_file("valid.yaml", R"yaml(
mappings:
  - ocpp16_key: CustomKey1
    component:
      name: CustomController1
      instance: Main
      evse:
        id: 7
        connectorId: 2
    variable:
      name: CustomVariable1
      instance: Default
  - ocpp16_key: CustomKey2
    component:
      name: CustomController2
    variable:
      name: CustomVariable2
)yaml");

    const auto mappings = load_ocpp16_key_mappings_from_yaml(yaml_file);

    ASSERT_EQ(mappings.size(), 2);
    ASSERT_TRUE(mappings.find("CustomKey1") != mappings.end());
    ASSERT_TRUE(mappings.find("CustomKey2") != mappings.end());

    const auto& connector_mapping = mappings.at("CustomKey1");
    EXPECT_EQ(connector_mapping.first.name.get(), "CustomController1");
    ASSERT_TRUE(connector_mapping.first.instance.has_value());
    EXPECT_EQ(connector_mapping.first.instance.value().get(), "Main");
    ASSERT_TRUE(connector_mapping.first.evse.has_value());
    EXPECT_EQ(connector_mapping.first.evse->id, 7);
    ASSERT_TRUE(connector_mapping.first.evse->connectorId.has_value());
    EXPECT_EQ(connector_mapping.first.evse->connectorId.value(), 2);
    EXPECT_EQ(connector_mapping.second.name.get(), "CustomVariable1");
    ASSERT_TRUE(connector_mapping.second.instance.has_value());
    EXPECT_EQ(connector_mapping.second.instance.value().get(), "Default");

    const auto& foo_mapping = mappings.at("CustomKey2");
    EXPECT_EQ(foo_mapping.first.name.get(), "CustomController2");
    EXPECT_FALSE(foo_mapping.first.instance.has_value());
    EXPECT_FALSE(foo_mapping.first.evse.has_value());
    EXPECT_EQ(foo_mapping.second.name.get(), "CustomVariable2");
    EXPECT_FALSE(foo_mapping.second.instance.has_value());
}

TEST_F(Ocpp16KeyMappingsTest, invalid_schema_throws) {
    const auto yaml_file = write_yaml_file("invalid_schema.yaml", R"yaml(
not_mappings:
  - ocpp16_key: CustomKey3
    component:
      name: CustomController3
    variable:
      name: CustomVariable3
)yaml");

    EXPECT_THROW((void)load_ocpp16_key_mappings_from_yaml(yaml_file), Ocpp16KeyMappingsError);
}

TEST_F(Ocpp16KeyMappingsTest, duplicate_keys_throw) {
    const auto yaml_file = write_yaml_file("duplicate.yaml", R"yaml(
mappings:
  - ocpp16_key: DuplicateKey
    component:
      name: CustomControllerDuplicateA
    variable:
      name: CustomVariableDuplicateA
  - ocpp16_key: DuplicateKey
    component:
      name: CustomControllerDuplicateB
    variable:
      name: CustomVariableDuplicateB
)yaml");

    EXPECT_THROW((void)load_ocpp16_key_mappings_from_yaml(yaml_file), Ocpp16KeyMappingsError);
}

TEST_F(Ocpp16KeyMappingsTest, missing_file_throws) {
    const auto missing_file = test_dir / "does_not_exist.yaml";
    EXPECT_THROW((void)load_ocpp16_key_mappings_from_yaml(missing_file), Ocpp16KeyMappingsError);
}

TEST_F(Ocpp16KeyMappingsTest, network_configuration_target_throws) {
    // any NetworkConfiguration slot instance counts as connection config
    const auto yaml_file = write_yaml_file("network_configuration.yaml", R"yaml(
mappings:
  - ocpp16_key: MyBackendUrl
    component:
      name: NetworkConfiguration
      instance: "1"
    variable:
      name: OcppCsmsUrl
)yaml");

    EXPECT_THROW((void)load_ocpp16_key_mappings_from_yaml(yaml_file), Ocpp16KeyMappingsError);
}

TEST_F(Ocpp16KeyMappingsTest, network_profile_selector_target_throws) {
    const auto yaml_file = write_yaml_file("profile_selector.yaml", R"yaml(
mappings:
  - ocpp16_key: MyNetworkPriority
    component:
      name: OCPPCommCtrlr
    variable:
      name: NetworkConfigurationPriority
)yaml");

    EXPECT_THROW((void)load_ocpp16_key_mappings_from_yaml(yaml_file), Ocpp16KeyMappingsError);
}

TEST_F(Ocpp16KeyMappingsTest, security_ctrlr_fallback_target_throws) {
    const auto yaml_file = write_yaml_file("security_fallback.yaml", R"yaml(
mappings:
  - ocpp16_key: MyAuthKey
    component:
      name: SecurityCtrlr
    variable:
      name: BasicAuthPassword
)yaml");

    EXPECT_THROW((void)load_ocpp16_key_mappings_from_yaml(yaml_file), Ocpp16KeyMappingsError);
}

TEST_F(Ocpp16KeyMappingsTest, non_connection_security_ctrlr_target_is_allowed) {
    // the ban covers the connection fallback CVs, not the whole SecurityCtrlr component
    const auto yaml_file = write_yaml_file("security_other.yaml", R"yaml(
mappings:
  - ocpp16_key: MyCertificateEntries
    component:
      name: SecurityCtrlr
    variable:
      name: CertificateEntries
)yaml");

    const auto mappings = load_ocpp16_key_mappings_from_yaml(yaml_file);
    ASSERT_EQ(mappings.size(), 1);
    ASSERT_TRUE(mappings.find("MyCertificateEntries") != mappings.end());
}

TEST_F(Ocpp16KeyMappingsTest, override_of_ocpp16_legacy_ctrlr_key_is_accepted) {
    const auto yaml_file = write_yaml_file("legacy_override.yaml", R"yaml(
mappings:
  - ocpp16_key: LightIntensity
    component:
      name: VendorStation
    variable:
      name: LightIntensity
)yaml");

    const auto mappings = load_ocpp16_key_mappings_from_yaml(yaml_file);
    ASSERT_EQ(mappings.size(), 1);
    const auto it = mappings.find("LightIntensity");
    ASSERT_NE(it, mappings.end());
    EXPECT_EQ(it->second.first.name.get(), "VendorStation");
    EXPECT_EQ(it->second.second.name.get(), "LightIntensity");
}

TEST_F(Ocpp16KeyMappingsTest, override_of_standard_key_throws) {
    const auto yaml_file = write_yaml_file("standard_override.yaml", R"yaml(
mappings:
  - ocpp16_key: HeartbeatInterval
    component:
      name: VendorStation
    variable:
      name: Heartbeat
)yaml");

    EXPECT_THROW((void)load_ocpp16_key_mappings_from_yaml(yaml_file), Ocpp16KeyMappingsError);
}

TEST_F(Ocpp16KeyMappingsTest, override_of_derived_legacy_key_throws) {
    for (const auto* key : {"NumberOfConnectors", "SupportedFeatureProfiles", "MeterPublicKeys", "DefaultPriceText"}) {
        const auto yaml_file = write_yaml_file("derived_override.yaml", std::string{R"yaml(
mappings:
  - ocpp16_key: )yaml"} + key + R"yaml(
    component:
      name: VendorStation
    variable:
      name: Derived
)yaml");

        EXPECT_THROW((void)load_ocpp16_key_mappings_from_yaml(yaml_file), Ocpp16KeyMappingsError) << key;
    }
}

TEST_F(Ocpp16KeyMappingsTest, override_targeting_builtin_mapping_of_other_key_throws) {
    const auto yaml_file = write_yaml_file("builtin_target.yaml", R"yaml(
mappings:
  - ocpp16_key: LightIntensity
    component:
      name: OCPPCommCtrlr
    variable:
      name: HeartbeatInterval
)yaml");

    EXPECT_THROW((void)load_ocpp16_key_mappings_from_yaml(yaml_file), Ocpp16KeyMappingsError);
}

TEST_F(Ocpp16KeyMappingsTest, override_sharing_target_with_custom_key_throws) {
    const auto yaml_file = write_yaml_file("shared_target.yaml", R"yaml(
mappings:
  - ocpp16_key: LightIntensity
    component:
      name: VendorStation
    variable:
      name: LightIntensity
  - ocpp16_key: VendorLightIntensity
    component:
      name: VendorStation
    variable:
      name: LightIntensity
)yaml");

    EXPECT_THROW((void)load_ocpp16_key_mappings_from_yaml(yaml_file), Ocpp16KeyMappingsError);
}

} // namespace ocpp::v2
