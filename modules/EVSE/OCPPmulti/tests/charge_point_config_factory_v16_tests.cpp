// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file charge_point_config_factory_v16_tests.cpp
/// \brief Tests that the OCPP 1.6 config factory backs the configuration with the composed device model
/// storage: variables supplied by an EVerest device model storage (source "EVEREST") are readable and
/// writable through the returned configuration, next to the SQLite-backed OCPP source — the same
/// composition the OCPP 2.x path provides to its ChargePoint.

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

// In-memory stand-in for the EVerest device model storage: one ReadWrite string variable tagged with
// source "EVEREST"; records writes so tests can assert routing.
class FakeEverestDeviceModelStorage : public ocpp::v2::DeviceModelStorageInterface {
public:
    FakeEverestDeviceModelStorage(const std::string& component_name, const std::string& variable_name,
                                  const std::string& value) :
        component(make_component(component_name)), variable(make_variable(variable_name)), value(value) {
    }

    ocpp::v2::DeviceModelMap get_device_model() override {
        ocpp::v2::VariableMetaData meta;
        meta.characteristics.dataType = ocpp::v2::DataEnum::string;
        meta.characteristics.supportsMonitoring = false;
        meta.source = "EVEREST";
        ocpp::v2::DeviceModelMap map;
        map[this->component][this->variable] = meta;
        return map;
    }

    std::optional<ocpp::v2::VariableAttribute> get_variable_attribute(const ocpp::v2::Component& component_id,
                                                                      const ocpp::v2::Variable& variable_id,
                                                                      const ocpp::v2::AttributeEnum&) override {
        if (!(component_id == this->component) || !(variable_id == this->variable)) {
            return std::nullopt;
        }
        ocpp::v2::VariableAttribute attribute;
        attribute.value = this->value;
        attribute.mutability = ocpp::v2::MutabilityEnum::ReadWrite;
        return attribute;
    }

    std::vector<ocpp::v2::VariableAttribute>
    get_variable_attributes(const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
                            const std::optional<ocpp::v2::AttributeEnum>& attribute_enum) override {
        const auto attribute =
            get_variable_attribute(component_id, variable_id, attribute_enum.value_or(ocpp::v2::AttributeEnum::Actual));
        if (!attribute.has_value()) {
            return {};
        }
        return {attribute.value()};
    }

    ocpp::v2::SetVariableStatusEnum set_variable_attribute_value(const ocpp::v2::Component& component_id,
                                                                 const ocpp::v2::Variable& variable_id,
                                                                 const ocpp::v2::AttributeEnum&,
                                                                 const std::string& new_value,
                                                                 const std::string&) override {
        if (!(component_id == this->component) || !(variable_id == this->variable)) {
            return ocpp::v2::SetVariableStatusEnum::Rejected;
        }
        this->value = new_value;
        this->last_set_value = new_value;
        return ocpp::v2::SetVariableStatusEnum::Accepted;
    }

    std::optional<ocpp::v2::VariableMonitoringMeta> set_monitoring_data(const ocpp::v2::SetMonitoringData&,
                                                                        const ocpp::v2::VariableMonitorType) override {
        return std::nullopt;
    }

    bool update_monitoring_reference(const std::int32_t, const std::string&) override {
        return false;
    }

    std::vector<ocpp::v2::VariableMonitoringMeta>
    get_monitoring_data(const std::vector<ocpp::v2::MonitoringCriterionEnum>&, const ocpp::v2::Component&,
                        const ocpp::v2::Variable&) override {
        return {};
    }

    ocpp::v2::ClearMonitoringStatusEnum clear_variable_monitor(int, bool) override {
        return ocpp::v2::ClearMonitoringStatusEnum::NotFound;
    }

    std::int32_t clear_custom_variable_monitors() override {
        return 0;
    }

    void check_integrity() override {
    }

    ocpp::v2::Component component;
    ocpp::v2::Variable variable;
    std::string value;
    std::optional<std::string> last_set_value;
};

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
        return {
            (work_dir / "device_model_storage.db").string(), // DeviceModelDatabasePath
            LIBOCPP_DEVICE_MODEL_MIGRATIONS_DIR,             // DeviceModelDatabaseMigrationPath
            LIBOCPP_COMPONENT_CONFIG_DIR,                    // DeviceModelConfigPath
            "",                                              // DeviceModelConfigMappings
            1,                                               // Ocpp16NetworkConfigSlot
            false,                                           // EnableLegacyConfigMigration
            "",                                              // ChargePointConfigPath
            "",                                              // UserConfigPath
        };
    }

    // The shipped custom component configs define two EVSEs.
    static constexpr std::int32_t n_evse = 2;

    fs::path work_dir;
};

TEST_F(ChargePointConfigFactoryV16Test, EverestSourcedVariableIsReadableAndWritable) {
    auto everest_storage = std::make_shared<FakeEverestDeviceModelStorage>("ConnectedEV", "VehicleId", "everest-value");

    auto result =
        module::config_factory_v16::create_charge_point_configuration(work_dir, make_params(), n_evse, everest_storage);
    ASSERT_NE(result.configuration, nullptr);
    auto& device_model = result.configuration->get_device_model();

    std::string value;
    const auto get_status = device_model.get_variable(make_component("ConnectedEV"), make_variable("VehicleId"),
                                                      ocpp::v2::AttributeEnum::Actual, value);
    EXPECT_EQ(get_status, ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_EQ(value, "everest-value");

    const auto set_status = device_model.set_value(make_component("ConnectedEV"), make_variable("VehicleId"),
                                                   ocpp::v2::AttributeEnum::Actual, "WMI999", "test");
    EXPECT_EQ(set_status, ocpp::v2::SetVariableStatusEnum::Accepted);
    EXPECT_EQ(everest_storage->last_set_value, "WMI999");
}

TEST_F(ChargePointConfigFactoryV16Test, OcppSourcedVariableStillResolves) {
    auto everest_storage = std::make_shared<FakeEverestDeviceModelStorage>("ConnectedEV", "VehicleId", "everest-value");

    auto result =
        module::config_factory_v16::create_charge_point_configuration(work_dir, make_params(), n_evse, everest_storage);
    ASSERT_NE(result.configuration, nullptr);
    auto& device_model = result.configuration->get_device_model();

    const auto& heartbeat_interval = ocpp::v2::ControllerComponentVariables::HeartbeatInterval;
    ASSERT_TRUE(heartbeat_interval.variable.has_value());
    std::string value;
    const auto get_status = device_model.get_variable(heartbeat_interval.component, heartbeat_interval.variable.value(),
                                                      ocpp::v2::AttributeEnum::Actual, value);
    EXPECT_EQ(get_status, ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_FALSE(value.empty());

    // The write must not leak into the EVerest storage.
    EXPECT_FALSE(everest_storage->last_set_value.has_value());
}

// The factory is also used in setups without an EVerest device model storage (e.g. tests); it must
// degrade to the OCPP-only device model instead of crashing.
TEST_F(ChargePointConfigFactoryV16Test, NullEverestStorageDegradesToOcppOnly) {
    auto result =
        module::config_factory_v16::create_charge_point_configuration(work_dir, make_params(), n_evse, nullptr);
    ASSERT_NE(result.configuration, nullptr);
    auto& device_model = result.configuration->get_device_model();

    std::string value;
    EXPECT_EQ(device_model.get_variable(make_component("ConnectedEV"), make_variable("VehicleId"),
                                        ocpp::v2::AttributeEnum::Actual, value),
              ocpp::v2::GetVariableStatusEnum::UnknownComponent);

    const auto& heartbeat_interval = ocpp::v2::ControllerComponentVariables::HeartbeatInterval;
    ASSERT_TRUE(heartbeat_interval.variable.has_value());
    EXPECT_EQ(device_model.get_variable(heartbeat_interval.component, heartbeat_interval.variable.value(),
                                        ocpp::v2::AttributeEnum::Actual, value),
              ocpp::v2::GetVariableStatusEnum::Accepted);
}

} // namespace
