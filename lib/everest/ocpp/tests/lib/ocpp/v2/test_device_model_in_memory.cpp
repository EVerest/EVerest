// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>

#include <device_model_storage_interface_mock.hpp>

#include <ocpp/v2/device_model.hpp>

namespace ocpp::v2 {

using ::testing::_;
using ::testing::Return;

namespace {

Component make_component() {
    Component component;
    component.name = "TestCtrlr";
    return component;
}

Variable make_variable(const std::string& name) {
    Variable variable;
    variable.name = name;
    return variable;
}

VariableAttribute make_attribute(const std::string& value, const bool persistent) {
    VariableAttribute attribute;
    attribute.type = AttributeEnum::Actual;
    attribute.mutability = MutabilityEnum::ReadWrite;
    attribute.persistent = persistent;
    attribute.constant = false;
    attribute.value = value;
    return attribute;
}

struct VariableChange {
    std::string variable;
    std::string value_previous;
    std::string value_current;
};

class DeviceModelInMemoryTest : public ::testing::Test {
protected:
    const Component component = make_component();
    const Variable persisted = make_variable("Persisted");
    const Variable volatile_variable = make_variable("Volatile");
    const Variable config_owned = make_variable("ConfigOwned");
    DeviceModelStorageMock* storage = nullptr;
    std::unique_ptr<DeviceModel> device_model;
    std::vector<VariableChange> changes;

    void SetUp() override {
        auto storage_mock = std::make_unique<DeviceModelStorageMock>();
        this->storage = storage_mock.get();

        VariableMetaData meta_data;
        meta_data.characteristics.dataType = DataEnum::integer;
        meta_data.characteristics.supportsMonitoring = true;
        DeviceModelMap map;
        map[component][persisted] = meta_data;
        map[component][persisted].attributes = {make_attribute("10", true)};
        map[component][volatile_variable] = meta_data;
        map[component][volatile_variable].attributes = {make_attribute("20", false)};
        map[component][config_owned] = meta_data;
        map[component][config_owned].owner = VARIABLE_OWNER_COMPONENT_CONFIG;
        map[component][config_owned].attributes = {make_attribute("30", false)};

        EXPECT_CALL(*storage, get_device_model()).WillOnce(Return(map));
        EXPECT_CALL(*storage, get_variable_attributes(_, _, _)).Times(0);
        EXPECT_CALL(*storage, get_variable_attribute(_, _, _)).Times(0);

        this->device_model = std::make_unique<DeviceModel>(std::move(storage_mock));
        this->device_model->register_variable_listener(
            [this](const std::unordered_map<std::int64_t, VariableMonitoringMeta>&, const Component&,
                   const Variable& variable, const VariableCharacteristics&, const VariableAttribute&,
                   const std::string& value_previous, const std::string& value_current) {
                this->changes.push_back({variable.name.get(), value_previous, value_current});
            });
    }

    std::string get(const Variable& variable) {
        return get(component, variable);
    }

    std::string get(const Component& in_component, const Variable& variable) {
        std::string value;
        EXPECT_EQ(device_model->get_variable(in_component, variable, AttributeEnum::Actual, value, false,
                                             UnsetValue::NotSupported),
                  GetVariableStatusEnum::Accepted);
        return value;
    }
};

} // namespace

TEST_F(DeviceModelInMemoryTest, reads_are_served_from_memory) {
    EXPECT_EQ(get(persisted), "10");
    EXPECT_EQ(get(volatile_variable), "20");
    EXPECT_EQ(device_model->get_value<int>(component, persisted), 10);
    EXPECT_EQ(device_model->get_mutability(component, persisted, AttributeEnum::Actual), MutabilityEnum::ReadWrite);
}

TEST_F(DeviceModelInMemoryTest, persistent_value_is_written_to_storage_before_memory) {
    EXPECT_CALL(*storage, set_variable_attribute_value(component, persisted, AttributeEnum::Actual, "11", "csms"))
        .WillOnce(Return(SetVariableStatusEnum::Accepted));

    EXPECT_EQ(device_model->set_value(component, persisted, AttributeEnum::Actual, "11", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(persisted), "11");
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0].value_previous, "10");
    EXPECT_EQ(changes[0].value_current, "11");
}

TEST_F(DeviceModelInMemoryTest, failed_storage_write_leaves_memory_unchanged) {
    EXPECT_CALL(*storage, set_variable_attribute_value(component, persisted, AttributeEnum::Actual, "11", "csms"))
        .WillOnce(Return(SetVariableStatusEnum::Rejected));

    EXPECT_EQ(device_model->set_value(component, persisted, AttributeEnum::Actual, "11", "csms"),
              SetVariableStatusEnum::Rejected);
    EXPECT_EQ(get(persisted), "10");
    EXPECT_TRUE(changes.empty());
}

TEST_F(DeviceModelInMemoryTest, reboot_required_storage_write_leaves_memory_unchanged) {
    EXPECT_CALL(*storage, set_variable_attribute_value(component, persisted, AttributeEnum::Actual, "11", "csms"))
        .WillOnce(Return(SetVariableStatusEnum::RebootRequired));

    EXPECT_EQ(device_model->set_value(component, persisted, AttributeEnum::Actual, "11", "csms"),
              SetVariableStatusEnum::RebootRequired);
    EXPECT_EQ(get(persisted), "10");
    EXPECT_TRUE(changes.empty());
}

TEST_F(DeviceModelInMemoryTest, non_persistent_value_is_not_written_to_storage) {
    EXPECT_CALL(*storage, set_variable_attribute_value(_, _, _, _, _)).Times(0);

    EXPECT_EQ(device_model->set_value(component, volatile_variable, AttributeEnum::Actual, "21", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(volatile_variable), "21");
    EXPECT_EQ(device_model->clear_value(component, volatile_variable, AttributeEnum::Actual, "internal"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(volatile_variable), "");
    ASSERT_EQ(changes.size(), 2);
}

TEST_F(DeviceModelInMemoryTest, rejected_clear_of_a_persistent_value_leaves_memory_unchanged) {
    EXPECT_CALL(*storage, set_variable_attribute_value(component, persisted, AttributeEnum::Actual, "", "csms"))
        .WillOnce(Return(SetVariableStatusEnum::Rejected));

    EXPECT_EQ(device_model->clear_value(component, persisted, AttributeEnum::Actual, "csms"),
              SetVariableStatusEnum::Rejected);
    EXPECT_EQ(get(persisted), "10");
    EXPECT_TRUE(changes.empty());
}

TEST_F(DeviceModelInMemoryTest, persistent_value_is_cleared_in_storage_before_memory) {
    EXPECT_CALL(*storage, set_variable_attribute_value(component, persisted, AttributeEnum::Actual, "", "csms"))
        .WillOnce(Return(SetVariableStatusEnum::Accepted));

    EXPECT_EQ(device_model->clear_value(component, persisted, AttributeEnum::Actual, "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(persisted), "");
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0].value_previous, "10");
    EXPECT_EQ(changes[0].value_current, "");
}

TEST_F(DeviceModelInMemoryTest, delta_monitor_takes_its_reference_from_memory) {
    EXPECT_CALL(*storage, set_variable_attribute_value(_, _, _, _, _)).Times(0);
    ASSERT_EQ(device_model->set_value(component, volatile_variable, AttributeEnum::Actual, "25", "csms"),
              SetVariableStatusEnum::Accepted);

    SetMonitoringData request;
    request.value = 5.0;
    request.type = MonitorEnum::Delta;
    request.severity = 3;
    request.component = component;
    request.variable = volatile_variable;

    VariableMonitoringMeta stored;
    stored.monitor.id = 7;
    stored.monitor.type = MonitorEnum::Delta;
    stored.monitor.severity = 3;
    stored.monitor.value = 5.0;
    stored.monitor.transaction = false;
    stored.type = VariableMonitorType::CustomMonitor;
    stored.reference_value = "20";
    EXPECT_CALL(*storage, set_monitoring_data(_, VariableMonitorType::CustomMonitor)).WillOnce(Return(stored));
    EXPECT_CALL(*storage, update_monitoring_reference(7, "25")).WillOnce(Return(true));

    const auto results = device_model->set_monitors({request});
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].status, SetMonitoringStatusEnum::Accepted);
    const auto meta_data = device_model->get_variable_meta_data(component, volatile_variable);
    ASSERT_TRUE(meta_data.has_value());
    ASSERT_EQ(meta_data->monitors.count(7), 1);
    EXPECT_EQ(meta_data->monitors.at(7).reference_value, "25");
}

TEST_F(DeviceModelInMemoryTest, variable_of_the_component_config_is_served_from_memory) {
    EXPECT_EQ(get(config_owned), "30");
    EXPECT_EQ(device_model->get_value<int>(component, config_owned), 30);
}

TEST_F(DeviceModelInMemoryTest, non_persistent_write_to_variable_of_the_component_config_stays_in_memory) {
    EXPECT_CALL(*storage, set_variable_attribute_value(_, _, _, _, _)).Times(0);

    EXPECT_EQ(device_model->set_value(component, config_owned, AttributeEnum::Actual, "31", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(config_owned), "31");
}

TEST_F(DeviceModelInMemoryTest, creating_a_network_configuration_slot_keeps_the_values_in_memory) {
    const Variable slot_variable = make_variable("OcppCsmsUrl");
    Component slot_component;
    slot_component.name = "NetworkConfiguration";
    slot_component.instance = "2";

    VariableMetaData meta_data;
    meta_data.characteristics.dataType = DataEnum::string;
    DeviceModelMap reloaded;
    reloaded[component][persisted] = meta_data;
    reloaded[component][persisted].attributes = {make_attribute("10", true)};
    reloaded[component][volatile_variable] = meta_data;
    reloaded[component][volatile_variable].attributes = {make_attribute("20", false)};
    reloaded[slot_component][slot_variable] = meta_data;
    reloaded[slot_component][slot_variable].attributes = {make_attribute("ws://example.com", true)};
    const Variable added = make_variable("Added");
    reloaded[component][added] = meta_data;
    reloaded[component][added].attributes = {make_attribute("added", true)};

    EXPECT_CALL(*storage, set_variable_attribute_value(_, _, _, _, _)).Times(0);
    ASSERT_EQ(device_model->set_value(component, volatile_variable, AttributeEnum::Actual, "21", "csms"),
              SetVariableStatusEnum::Accepted);

    EXPECT_CALL(*storage, create_network_configuration_slot_from_default_schema(2)).WillOnce(Return(true));
    EXPECT_CALL(*storage, get_device_model()).WillOnce(Return(reloaded));
    ASSERT_TRUE(device_model->create_network_configuration_slot_from_default_schema(2));

    EXPECT_EQ(get(volatile_variable), "21");
    EXPECT_EQ(get(slot_component, slot_variable), "ws://example.com");
    EXPECT_EQ(get(added), "added");
}

TEST_F(DeviceModelInMemoryTest, reports_contain_the_values_in_memory) {
    EXPECT_CALL(*storage, set_variable_attribute_value(component, volatile_variable, _, _, _)).Times(0);
    ASSERT_EQ(device_model->set_value(component, volatile_variable, AttributeEnum::Actual, "42", "csms"),
              SetVariableStatusEnum::Accepted);

    const std::map<Variable, std::string> expected{{persisted, "10"}, {volatile_variable, "42"}, {config_owned, "30"}};
    const auto report = device_model->get_base_report_data(ReportBaseEnum::FullInventory);
    ASSERT_EQ(report.size(), expected.size());
    for (const auto& data : report) {
        ASSERT_EQ(data.variableAttribute.size(), 1);
        EXPECT_EQ(data.variableAttribute.at(0).value.value().get(), expected.at(data.variable));
    }
}

namespace {

class DeviceModelStorageHeldVariableTest : public ::testing::Test {
protected:
    const Component component = make_component();
    const Variable in_memory = make_variable("InMemory");
    const Variable storage_held = make_variable("StorageHeld");
    DeviceModelStorageMock* storage = nullptr;
    std::unique_ptr<DeviceModel> device_model;

    void SetUp() override {
        auto storage_mock = std::make_unique<DeviceModelStorageMock>();
        this->storage = storage_mock.get();

        VariableMetaData meta_data;
        meta_data.characteristics.dataType = DataEnum::integer;
        meta_data.characteristics.supportsMonitoring = true;
        DeviceModelMap map;
        map[component][in_memory] = meta_data;
        map[component][in_memory].attributes = {make_attribute("10", true)};
        map[component][storage_held] = meta_data;
        map[component][storage_held].owner = "OTHER";
        map[component][storage_held].attributes = {make_attribute("20", true)};

        EXPECT_CALL(*storage, get_device_model()).WillOnce(Return(map));
        EXPECT_CALL(*storage, get_variable_attribute(component, in_memory, _)).Times(0);
        EXPECT_CALL(*storage, get_variable_attributes(component, in_memory, _)).Times(0);

        this->device_model = std::make_unique<DeviceModel>(std::move(storage_mock));
    }
};

} // namespace

TEST_F(DeviceModelStorageHeldVariableTest, variable_with_an_owner_is_read_from_the_storage) {
    EXPECT_CALL(*storage, get_variable_attribute(component, storage_held, AttributeEnum::Actual))
        .WillOnce(Return(make_attribute("21", true)));

    EXPECT_EQ(device_model->get_value<int>(component, storage_held), 21);
    EXPECT_EQ(device_model->get_value<int>(component, in_memory), 10);
}

TEST_F(DeviceModelStorageHeldVariableTest, write_to_a_variable_with_an_owner_goes_to_the_storage) {
    EXPECT_CALL(*storage, get_variable_attribute(component, storage_held, AttributeEnum::Actual))
        .WillRepeatedly(Return(make_attribute("20", false)));
    EXPECT_CALL(*storage, set_variable_attribute_value(component, storage_held, AttributeEnum::Actual, "21", "csms"))
        .WillOnce(Return(SetVariableStatusEnum::Accepted));

    EXPECT_EQ(device_model->set_value(component, storage_held, AttributeEnum::Actual, "21", "csms"),
              SetVariableStatusEnum::Accepted);
}

TEST_F(DeviceModelStorageHeldVariableTest, clear_of_a_variable_with_an_owner_goes_to_the_storage) {
    EXPECT_CALL(*storage, get_variable_attribute(component, storage_held, AttributeEnum::Actual))
        .WillRepeatedly(Return(make_attribute("20", false)));
    EXPECT_CALL(*storage, set_variable_attribute_value(component, storage_held, AttributeEnum::Actual, "", "csms"))
        .WillOnce(Return(SetVariableStatusEnum::Accepted));

    EXPECT_EQ(device_model->clear_value(component, storage_held, AttributeEnum::Actual, "csms"),
              SetVariableStatusEnum::Accepted);
}

TEST_F(DeviceModelStorageHeldVariableTest, reports_take_a_variable_with_an_owner_from_the_storage) {
    EXPECT_CALL(*storage, get_variable_attributes(component, storage_held, _))
        .WillOnce(Return(std::vector<VariableAttribute>{make_attribute("21", true)}));

    const auto report = device_model->get_base_report_data(ReportBaseEnum::FullInventory);
    ASSERT_EQ(report.size(), 2);
    for (const auto& data : report) {
        ASSERT_EQ(data.variableAttribute.size(), 1);
        const auto expected = data.variable == in_memory ? "10" : "21";
        EXPECT_EQ(data.variableAttribute.at(0).value.value().get(), expected);
    }
}

TEST(DeviceModelStoredMonitorTest, monitor_of_a_variable_with_an_owner_is_active_after_a_restart) {
    auto storage_mock = std::make_unique<DeviceModelStorageMock>();
    auto* storage = storage_mock.get();
    const auto component = make_component();
    const auto variable = make_variable("StorageHeld");

    DeviceModelMap map;
    auto& meta_data = map[component][variable];
    meta_data.characteristics.dataType = DataEnum::integer;
    meta_data.characteristics.supportsMonitoring = true;
    meta_data.owner = "EVEREST";
    meta_data.attributes = {make_attribute("20", true)};

    StoredMonitor stored;
    stored.component = component;
    stored.variable = variable;
    stored.monitor.monitor.id = 5;
    stored.monitor.monitor.type = MonitorEnum::UpperThreshold;
    stored.monitor.monitor.value = 30;
    stored.monitor.monitor.severity = 1;
    stored.monitor.type = VariableMonitorType::CustomMonitor;

    EXPECT_CALL(*storage, get_device_model()).WillOnce(Return(map));
    EXPECT_CALL(*storage, get_all_monitors()).WillOnce(Return(std::vector<StoredMonitor>{stored}));
    DeviceModel device_model{std::move(storage_mock)};

    const auto monitors = device_model.get_monitors({}, {});
    ASSERT_EQ(monitors.size(), 1);
    EXPECT_EQ(monitors.at(0).variable, variable);
    ASSERT_EQ(monitors.at(0).variableMonitoring.size(), 1);
    EXPECT_EQ(monitors.at(0).variableMonitoring.at(0).id, 5);
}

} // namespace ocpp::v2
