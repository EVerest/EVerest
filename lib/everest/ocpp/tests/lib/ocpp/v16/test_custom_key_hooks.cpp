// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_custom_key_hooks.cpp
/// \brief Tests for the ChargePointConfigurationInterface extensibility hooks:
///   * set_custom_key_forced() bypassing readOnly on an existing Custom key, and returning
///     NotSupported for a key that isn't defined in the Custom profile.
///   * the ChargePointImpl-level custom_key_validation_callback vetoing a ChangeConfiguration,
///     exercised through the public set_configuration_key() entry point so both the readonly
///     gate and the veto gate in set_configuration_key_internal() are covered end to end.

#include <filesystem>
#include <fstream>
#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ocpp/v16/charge_point_configuration.hpp>
#include <ocpp/v16/charge_point_impl.hpp>

#include "connectivity_manager_mock.hpp"
#include "evse_security_mock.hpp"

namespace fs = std::filesystem;

using ::testing::NiceMock;

namespace ocpp {
namespace v16 {

namespace {

/// \brief config-full.json only defines Internal/Core/../CostAndPrice - there is no Custom
/// profile in the stock v16 config fixtures (Custom is an application-defined extension point).
/// To exercise set_custom_key_forced() against a real, schema-backed Custom key we copy the
/// stock v16 config directory into a temp dir, add a small Custom.json schema there, wire it
/// into Config.json's additionalProperties:false property list, and inject a matching "Custom"
/// object into the config JSON before constructing ChargePointConfiguration.
fs::path make_custom_profile_config_dir(const fs::path& base_dir) {
    const fs::path dst = base_dir / "config_with_custom_profile";
    fs::create_directories(dst);
    fs::copy(fs::path(CONFIG_DIR_V16), dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing);

    const fs::path custom_schema_path = dst / "profile_schemas" / "Custom.json";
    std::ofstream custom_schema_ofs(custom_schema_path);
    custom_schema_ofs << R"({
        "$schema": "http://json-schema.org/draft-07/schema#",
        "type": "object",
        "properties": {
            "TestReadOnlyKey": { "type": "string", "readOnly": true },
            "TestWritableKey": { "type": "string", "readOnly": false }
        },
        "additionalProperties": false
    })";
    custom_schema_ofs.close();

    const fs::path config_schema_path = dst / "profile_schemas" / "Config.json";
    std::ifstream config_schema_ifs(config_schema_path);
    json config_schema = json::parse(config_schema_ifs);
    config_schema_ifs.close();
    config_schema["properties"]["Custom"] = {{"type", "object"}, {"$ref", "Custom.json"}};
    std::ofstream config_schema_ofs(config_schema_path);
    config_schema_ofs << config_schema.dump();
    config_schema_ofs.close();

    return dst;
}

std::string read_file(const fs::path& path) {
    std::ifstream ifs(path);
    return std::string((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
}

} // namespace

class CustomKeyHooksTest : public ::testing::Test {
protected:
    void SetUp() override {
        this->tmp_dir = fs::temp_directory_path() /
                        ("ocpp_v16_custom_key_hooks_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(this->tmp_dir);

        const fs::path config_dir = make_custom_profile_config_dir(this->tmp_dir);

        json config_json = json::parse(read_file(fs::path(CONFIG_FILE_LOCATION_V16)));
        config_json["Custom"] = {{"TestReadOnlyKey", "initial-ro"}, {"TestWritableKey", "initial-rw"}};

        this->configuration = std::make_unique<ChargePointConfiguration>(config_json.dump(), config_dir,
                                                                         fs::path(USER_CONFIG_FILE_LOCATION_V16));
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(this->tmp_dir, ec);
    }

    std::unique_ptr<ChargePointConfiguration> configuration;
    fs::path tmp_dir;
};

TEST_F(CustomKeyHooksTest, SetCustomKeyForcedBypassesReadOnly) {
    // Confirm the key really is readOnly through the normal path first.
    auto before = configuration->get("TestReadOnlyKey");
    ASSERT_TRUE(before.has_value());
    EXPECT_TRUE(before.value().readonly);

    auto normal_set = configuration->set("TestReadOnlyKey", "should-be-rejected");
    ASSERT_TRUE(normal_set.has_value());
    EXPECT_EQ(normal_set.value(), ConfigurationStatus::Rejected);

    const auto forced_result = configuration->set_custom_key_forced("TestReadOnlyKey", "forced-value");
    EXPECT_EQ(forced_result, ConfigurationStatus::Accepted);

    auto after = configuration->get("TestReadOnlyKey");
    ASSERT_TRUE(after.has_value());
    ASSERT_TRUE(after.value().value.has_value());
    EXPECT_EQ(after.value().value.value(), "forced-value");
}

TEST_F(CustomKeyHooksTest, SetCustomKeyForcedRejectsMissingKey) {
    const auto result = configuration->set_custom_key_forced("DoesNotExist", "x");
    EXPECT_EQ(result, ConfigurationStatus::NotSupported);
}

class CustomKeyValidationCallbackTest : public ::testing::Test {
protected:
    void SetUp() override {
        this->evse_security = std::make_shared<NiceMock<EvseSecurityMock>>();
        this->connectivity_manager = std::make_shared<NiceMock<ConnectivityManagerMock>>();

        this->configuration = std::make_unique<ChargePointConfiguration>(read_file(fs::path(CONFIG_FILE_LOCATION_V16)),
                                                                         fs::path(CONFIG_DIR_V16),
                                                                         fs::path(USER_CONFIG_FILE_LOCATION_V16));

        this->tmp_dir = fs::temp_directory_path() / ("ocpp_v16_custom_key_validation_test_" +
                                                     std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(this->tmp_dir);

        this->charge_point = std::make_unique<ChargePointImpl>(
            *this->configuration, /*share_path=*/fs::path(CONFIG_DIR_V16), /*database_path=*/this->tmp_dir,
            /*sql_init_path=*/fs::path(MIGRATION_FILES_LOCATION_V16), /*message_log_path=*/this->tmp_dir,
            this->evse_security, this->connectivity_manager, /*security_configuration=*/std::nullopt,
            /*message_callback=*/nullptr);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(this->tmp_dir, ec);
    }

    std::shared_ptr<NiceMock<EvseSecurityMock>> evse_security;
    std::shared_ptr<NiceMock<ConnectivityManagerMock>> connectivity_manager;
    std::unique_ptr<ChargePointConfiguration> configuration;
    std::unique_ptr<ChargePointImpl> charge_point;
    fs::path tmp_dir;
};

TEST_F(CustomKeyValidationCallbackTest, VetoRejectsChangeConfiguration) {
    charge_point->register_custom_key_validation_callback(
        [](const std::string& key, const std::string& value) { return false; });

    const auto result = charge_point->set_configuration_key("HeartbeatInterval", "100");
    EXPECT_EQ(result, ConfigurationStatus::Rejected);
}

TEST_F(CustomKeyValidationCallbackTest, ApprovingCallbackAllowsChangeConfiguration) {
    charge_point->register_custom_key_validation_callback(
        [](const std::string& key, const std::string& value) { return true; });

    const auto result = charge_point->set_configuration_key("HeartbeatInterval", "100");
    EXPECT_EQ(result, ConfigurationStatus::Accepted);
}

TEST_F(CustomKeyValidationCallbackTest, NoCallbackRegisteredBehavesAsBefore) {
    const auto result = charge_point->set_configuration_key("HeartbeatInterval", "100");
    EXPECT_EQ(result, ConfigurationStatus::Accepted);
}

} // namespace v16
} // namespace ocpp
