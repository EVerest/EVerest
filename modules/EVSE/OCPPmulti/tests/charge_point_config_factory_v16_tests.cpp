// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file charge_point_config_factory_v16_tests.cpp
/// \brief Tests that the OCPP 1.6 config factory backs the configuration with the device model database and takes
/// over the EVerest device model database of earlier versions.

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model_storage_interface.hpp>
#include <ocpp/v2/init_device_model_db.hpp>

#include "charge_point_config_factory_v16.hpp"

namespace {

namespace fs = std::filesystem;

ocpp::v2::Component make_component(const std::string& name) {
    ocpp::v2::Component component;
    component.name = name;
    return component;
}

ocpp::v2::Variable make_variable(const std::string& name) {
    ocpp::v2::Variable variable;
    variable.name = name;
    return variable;
}

class ChargePointConfigFactoryV16Test : public ::testing::Test {
protected:
    void SetUp() override {
        work_dir = fs::temp_directory_path() / "ocppmulti_factory_v16_tests" /
                   ::testing::UnitTest::GetInstance()->current_test_info()->name();
        fs::remove_all(work_dir);
        fs::create_directories(work_dir);
    }

    void TearDown() override {
        fs::remove_all(work_dir);
    }

    module::config_factory_v16::Ocpp16DeviceModelParams make_params() const {
        // Made absolute, as the factory resolves relative paths against the share path.
        return {
            (work_dir / "device_model_storage.db").string(),            // DeviceModelDatabasePath
            fs::absolute(LIBOCPP_DEVICE_MODEL_MIGRATIONS_DIR).string(), // DeviceModelDatabaseMigrationPath
            fs::absolute(LIBOCPP_COMPONENT_CONFIG_DIR).string(),        // DeviceModelConfigPath
            "",                                                         // DeviceModelConfigMappings
            1,                                                          // Ocpp16NetworkConfigSlot
            false,                                                      // EnableLegacyConfigMigration
            "",                                                         // ChargePointConfigPath
            "",                                                         // UserConfigPath
        };
    }

    // The shipped custom component configs define two EVSEs.
    static constexpr std::int32_t n_evse = 2;

    fs::path work_dir;
};

TEST_F(ChargePointConfigFactoryV16Test, OcppVariableResolves) {
    auto result = module::config_factory_v16::create_charge_point_configuration(work_dir, make_params(), n_evse, {});
    ASSERT_NE(result.configuration, nullptr);
    auto& device_model = result.configuration->get_device_model();

    const auto& heartbeat_interval = ocpp::v2::ControllerComponentVariables::HeartbeatInterval;
    ASSERT_TRUE(heartbeat_interval.variable.has_value());
    std::string value;
    EXPECT_EQ(device_model.get_variable(heartbeat_interval.component, heartbeat_interval.variable.value(),
                                        ocpp::v2::AttributeEnum::Actual, value, false,
                                        ocpp::v2::UnsetValue::NotSupported),
              ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_FALSE(value.empty());
}

// The EVerest device model database of earlier versions is taken over at the first start and renamed
TEST_F(ChargePointConfigFactoryV16Test, EverestDeviceModelDatabaseIsTakenOver) {
    const auto everest_db_path = work_dir / "everest_device_model_storage.db";
    {
        ocpp::v2::InitDeviceModelDb init(everest_db_path, fs::absolute(LIBOCPP_DEVICE_MODEL_MIGRATIONS_DIR));
        init.initialize_database({}, false, false);
        init.close_connection();
    }

    auto result =
        module::config_factory_v16::create_charge_point_configuration(work_dir, make_params(), n_evse, everest_db_path);
    ASSERT_NE(result.configuration, nullptr);
    EXPECT_FALSE(fs::exists(everest_db_path));
    EXPECT_TRUE(fs::exists(work_dir / "everest_device_model_storage.db.migrated"));
}

} // namespace
