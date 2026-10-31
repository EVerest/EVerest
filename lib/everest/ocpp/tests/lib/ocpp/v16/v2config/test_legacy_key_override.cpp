// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "ocpp16_test_config.hpp"

#include <ocpp/v16/charge_point_configuration.hpp>
#include <ocpp/v16/charge_point_configuration_devicemodel.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>
#include <ocpp/v2/ocpp16_component_config_patcher.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

namespace {

constexpr char VENDOR_STATION_COMPONENT_CONFIG[] = R"({
    "description": "Station settings shared by OCPP 1.6 and OCPP 2.x",
    "name": "VendorStation",
    "type": "object",
    "properties": {
        "LightIntensity": {
            "variable_name": "LightIntensity",
            "characteristics": {
                "supportsMonitoring": true,
                "dataType": "integer",
                "unit": "Percent"
            },
            "attributes": [
                {
                    "type": "Actual",
                    "mutability": "ReadWrite"
                }
            ],
            "type": "integer"
        }
    }
})";

// matches LightIntensity and NumberOfConnectors in ocpp16_test_config.hpp
constexpr auto MIGRATED_LIGHT_INTENSITY = "12";
constexpr std::int32_t NUMBER_OF_CONNECTORS = 3;

ocpp::v2::Ocpp16KeyMappings light_intensity_override(const std::string& variable = "LightIntensity") {
    return {{"LightIntensity", {ocpp::v2::Component{"VendorStation"}, ocpp::v2::Variable{variable}}}};
}

class LegacyKeyOverride : public testing::Test {
protected:
    const std::filesystem::path work_dir =
        std::filesystem::temp_directory_path() /
        ("ocpp16_legacy_key_override_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::unique_ptr<ocpp::v16::ChargePointConfiguration> json_config;
    std::unique_ptr<ocpp::v16::ChargePointConfigurationDeviceModel> config;

    void SetUp() override {
        const auto component_config_dir = work_dir / "component_config";
        std::filesystem::create_directories(work_dir);
        std::filesystem::copy("./resources/example_config/v2/component_config", component_config_dir,
                              std::filesystem::copy_options::recursive);
        std::ofstream(component_config_dir / "custom" / "VendorStation.json") << VENDOR_STATION_COMPONENT_CONFIG;
    }

    void TearDown() override {
        config.reset();
        std::error_code ec;
        std::filesystem::remove_all(work_dir, ec);
    }

    void migrate(const ocpp::v2::Ocpp16KeyMappings& mappings) {
        const auto db_path = (work_dir / "device_model.db").string();
        json_config = std::make_unique<ocpp::v16::ChargePointConfiguration>(
            ocpp::tests::make_ocpp16_test_config_full(), CONFIG_DIR_V16, USER_CONFIG_FILE_LOCATION_V16);

        ocpp::v2::InitDeviceModelDb db(db_path, "./resources/v2/device_model_migration_files");
        auto component_configs = ocpp::v2::get_all_component_configs(work_dir / "component_config");
        ocpp::v2::patch_component_config_with_ocpp16(component_configs, *json_config, mappings);
        db.initialize_database(component_configs, true);

        std::unique_ptr<ocpp::v2::DeviceModelInterface> storage =
            std::make_unique<ocpp::v2::DeviceModel>(std::make_unique<ocpp::v2::DeviceModelStorageSqlite>(db_path));
        config = std::make_unique<ocpp::v16::ChargePointConfigurationDeviceModel>(CONFIG_DIR_V16, std::move(storage),
                                                                                  mappings);
    }

    std::optional<std::string> device_model_value(const std::string& component, const std::string& variable) {
        std::string value;
        const auto status = config->get_device_model().get_variable(
            ocpp::v2::Component{component}, ocpp::v2::Variable{variable}, ocpp::v2::AttributeEnum::Actual, value);
        if (status != ocpp::v2::GetVariableStatusEnum::Accepted) {
            return std::nullopt;
        }
        return value;
    }
};

TEST_F(LegacyKeyOverride, MigratedValueIsStoredInOverrideTarget) {
    migrate(light_intensity_override());
    EXPECT_EQ(device_model_value("VendorStation", "LightIntensity"), MIGRATED_LIGHT_INTENSITY);
    EXPECT_NE(device_model_value("OCPP16LegacyCtrlr", "LightIntensity"), MIGRATED_LIGHT_INTENSITY);
}

TEST_F(LegacyKeyOverride, GetAndSetUseOverrideTarget) {
    migrate(light_intensity_override());

    auto kv = config->get("LightIntensity");
    ASSERT_TRUE(kv.has_value());
    EXPECT_EQ(kv->value, MIGRATED_LIGHT_INTENSITY);
    EXPECT_FALSE(kv->readonly);

    EXPECT_EQ(config->set("LightIntensity", "55"), ocpp::v16::ConfigurationStatus::Accepted);
    EXPECT_EQ(device_model_value("VendorStation", "LightIntensity"), "55");
    EXPECT_EQ(config->getLightIntensity(), 55);

    // a write on the OCPP 2.x side is seen by OCPP 1.6
    EXPECT_EQ(config->get_device_model().set_value(ocpp::v2::Component{"VendorStation"},
                                                   ocpp::v2::Variable{"LightIntensity"},
                                                   ocpp::v2::AttributeEnum::Actual, "70", "test"),
              ocpp::v2::SetVariableStatusEnum::Accepted);
    kv = config->get("LightIntensity");
    ASSERT_TRUE(kv.has_value());
    EXPECT_EQ(kv->value, "70");
}

TEST_F(LegacyKeyOverride, GetAllKeyValueListsKeyOnceWithTargetValue) {
    migrate(light_intensity_override());
    ASSERT_EQ(config->set("LightIntensity", "55"), ocpp::v16::ConfigurationStatus::Accepted);

    const auto all = config->get_all_key_value();
    const auto count = std::count_if(all.begin(), all.end(), [](const auto& kv) { return kv.key == "LightIntensity"; });
    EXPECT_EQ(count, 1);
    const auto it = std::find_if(all.begin(), all.end(), [](const auto& kv) { return kv.key == "LightIntensity"; });
    ASSERT_NE(it, all.end());
    EXPECT_EQ(it->value, "55");
}

TEST_F(LegacyKeyOverride, IntegrityCheckPassesWithExistingTarget) {
    migrate(light_intensity_override());
    EXPECT_NO_THROW(config->check_integrity(NUMBER_OF_CONNECTORS));
}

TEST_F(LegacyKeyOverride, MissingTargetFallsBackToBuiltinMapping) {
    migrate(light_intensity_override("DoesNotExist"));
    EXPECT_NO_THROW(config->check_integrity(NUMBER_OF_CONNECTORS));
    EXPECT_FALSE(config->get_variable_resolver().is_overridden(ocpp::v16::keys::valid_keys::LightIntensity));

    EXPECT_EQ(device_model_value("OCPP16LegacyCtrlr", "LightIntensity"), MIGRATED_LIGHT_INTENSITY);
    const auto kv = config->get("LightIntensity");
    ASSERT_TRUE(kv.has_value());
    EXPECT_EQ(kv->value, MIGRATED_LIGHT_INTENSITY);
}

} // namespace
