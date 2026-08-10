// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <everest/ocpp_module_common/device_model/composed_device_model_storage.hpp>

namespace {

using ocpp_module_common::device_model::make_composed_device_model_storage;

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

// In-memory storage exposing a fixed device model map; records writes so tests can assert routing.
class FakeDeviceModelStorage : public ocpp::v2::DeviceModelStorageInterface {
public:
    FakeDeviceModelStorage(ocpp::v2::DeviceModelMap device_model_map, std::string attribute_value) :
        device_model_map(std::move(device_model_map)), attribute_value(std::move(attribute_value)) {
    }

    ocpp::v2::DeviceModelMap get_device_model() override {
        return this->device_model_map;
    }

    std::optional<ocpp::v2::VariableAttribute> get_variable_attribute(const ocpp::v2::Component& component_id,
                                                                      const ocpp::v2::Variable& variable_id,
                                                                      const ocpp::v2::AttributeEnum&) override {
        const auto component_it = this->device_model_map.find(component_id);
        if (component_it == this->device_model_map.end() ||
            component_it->second.find(variable_id) == component_it->second.end()) {
            return std::nullopt;
        }
        ocpp::v2::VariableAttribute attribute;
        attribute.value = this->attribute_value;
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
                                                                 const std::string& value,
                                                                 const std::string&) override {
        this->last_set_component = component_id.name.get();
        this->last_set_variable = variable_id.name.get();
        this->last_set_value = value;
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

    ocpp::v2::DeviceModelMap device_model_map;
    std::string attribute_value;
    std::optional<std::string> last_set_component;
    std::optional<std::string> last_set_variable;
    std::optional<std::string> last_set_value;
};

ocpp::v2::DeviceModelMap make_device_model_map(const std::string& component_name, const std::string& variable_name,
                                               const std::optional<std::string>& source) {
    ocpp::v2::VariableMetaData meta;
    meta.characteristics.dataType = ocpp::v2::DataEnum::string;
    meta.characteristics.supportsMonitoring = false;
    meta.source = source;
    ocpp::v2::DeviceModelMap map;
    map[make_component(component_name)][make_variable(variable_name)] = meta;
    return map;
}

class ComposedDeviceModelStorageFactoryTest : public ::testing::Test {
protected:
    // OCPP-side variable carries no source (defaults to "OCPP"); EVerest-side variable is tagged "EVEREST".
    std::shared_ptr<FakeDeviceModelStorage> ocpp_storage = std::make_shared<FakeDeviceModelStorage>(
        make_device_model_map("OCPPCommCtrlr", "HeartbeatInterval", std::nullopt), "ocpp-value");
    std::shared_ptr<FakeDeviceModelStorage> everest_storage = std::make_shared<FakeDeviceModelStorage>(
        make_device_model_map("ConnectedEV", "VehicleId", "EVEREST"), "everest-value");
};

TEST_F(ComposedDeviceModelStorageFactoryTest, EverestSourcedVariableRoutesToEverestStorage) {
    const auto composed = make_composed_device_model_storage(ocpp_storage, everest_storage);

    const auto attribute = composed->get_variable_attribute(make_component("ConnectedEV"), make_variable("VehicleId"),
                                                            ocpp::v2::AttributeEnum::Actual);
    ASSERT_TRUE(attribute.has_value());
    ASSERT_TRUE(attribute->value.has_value());
    EXPECT_EQ(attribute->value.value().get(), "everest-value");

    const auto status = composed->set_variable_attribute_value(
        make_component("ConnectedEV"), make_variable("VehicleId"), ocpp::v2::AttributeEnum::Actual, "WMI123", "test");
    EXPECT_EQ(status, ocpp::v2::SetVariableStatusEnum::Accepted);
    EXPECT_EQ(everest_storage->last_set_value, "WMI123");
    EXPECT_FALSE(ocpp_storage->last_set_value.has_value());
}

TEST_F(ComposedDeviceModelStorageFactoryTest, UnsourcedVariableDefaultsToOcppStorage) {
    const auto composed = make_composed_device_model_storage(ocpp_storage, everest_storage);

    const auto attribute = composed->get_variable_attribute(
        make_component("OCPPCommCtrlr"), make_variable("HeartbeatInterval"), ocpp::v2::AttributeEnum::Actual);
    ASSERT_TRUE(attribute.has_value());
    ASSERT_TRUE(attribute->value.has_value());
    EXPECT_EQ(attribute->value.value().get(), "ocpp-value");

    const auto status =
        composed->set_variable_attribute_value(make_component("OCPPCommCtrlr"), make_variable("HeartbeatInterval"),
                                               ocpp::v2::AttributeEnum::Actual, "42", "test");
    EXPECT_EQ(status, ocpp::v2::SetVariableStatusEnum::Accepted);
    EXPECT_EQ(ocpp_storage->last_set_value, "42");
    EXPECT_FALSE(everest_storage->last_set_value.has_value());
}

TEST_F(ComposedDeviceModelStorageFactoryTest, GetDeviceModelMergesBothSources) {
    const auto composed = make_composed_device_model_storage(ocpp_storage, everest_storage);

    const auto device_model_map = composed->get_device_model();
    EXPECT_EQ(device_model_map.size(), 2u);
    EXPECT_NE(device_model_map.find(make_component("OCPPCommCtrlr")), device_model_map.end());
    EXPECT_NE(device_model_map.find(make_component("ConnectedEV")), device_model_map.end());
}

TEST_F(ComposedDeviceModelStorageFactoryTest, NullEverestStorageIsSkipped) {
    const auto composed = make_composed_device_model_storage(ocpp_storage, nullptr);

    const auto device_model_map = composed->get_device_model();
    EXPECT_EQ(device_model_map.size(), 1u);

    const auto attribute = composed->get_variable_attribute(
        make_component("OCPPCommCtrlr"), make_variable("HeartbeatInterval"), ocpp::v2::AttributeEnum::Actual);
    ASSERT_TRUE(attribute.has_value());
    ASSERT_TRUE(attribute->value.has_value());
    EXPECT_EQ(attribute->value.value().get(), "ocpp-value");

    // EVEREST-sourced lookups have no registered storage and must not crash.
    EXPECT_EQ(composed->get_variable_attribute(make_component("ConnectedEV"), make_variable("VehicleId"),
                                               ocpp::v2::AttributeEnum::Actual),
              std::nullopt);
}

} // namespace
