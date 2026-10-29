// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

#include <device_model_test_helper.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>

namespace ocpp::v2 {

namespace {

const std::string OWNER = "EVEREST_CONFIG";
const std::string OTHER_OWNER = "EVEREST";

Component make_component(const std::string& name, const std::optional<std::int32_t>& evse_id = std::nullopt,
                         const std::optional<std::int32_t>& connector_id = std::nullopt) {
    Component component;
    component.name = name;
    if (evse_id.has_value()) {
        EVSE evse;
        evse.id = evse_id.value();
        evse.connectorId = connector_id;
        component.evse = evse;
    }
    return component;
}

Variable make_variable(const std::string& name) {
    Variable variable;
    variable.name = name;
    return variable;
}

RegisteredVariable make_registered_variable(const std::string& name, const std::string& value,
                                            const MutabilityEnum mutability = MutabilityEnum::ReadWrite,
                                            const DataEnum data_type = DataEnum::integer,
                                            const std::optional<bool> persistent = std::nullopt) {
    RegisteredVariable registered;
    registered.variable = make_variable(name);
    registered.characteristics.dataType = data_type;
    registered.characteristics.supportsMonitoring = true;
    registered.characteristics.minLimit = 0;
    registered.characteristics.maxLimit = 1000;
    VariableAttribute attribute;
    attribute.mutability = mutability;
    attribute.value = value;
    attribute.persistent = persistent;
    registered.attributes.push_back(attribute);
    return registered;
}

class FakeProvider : public VariableValueProvider {
public:
    SetVariableStatusEnum set_value(const Component& component, const Variable& variable, AttributeEnum /*attribute*/,
                                    const std::string& value, const std::string& /*source*/,
                                    const WriteId write_id) override {
        ++this->calls_running;
        std::this_thread::sleep_for(this->delay);
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->values.push_back(value);
            this->ids.push_back(write_id);
        }
        if (this->on_set) {
            this->on_set(component, variable, value, write_id);
        }
        --this->calls_running;
        return this->result;
    }

    std::vector<std::string> received() {
        std::lock_guard<std::mutex> lock(this->mutex);
        return this->values;
    }

    std::vector<WriteId> write_ids() {
        std::lock_guard<std::mutex> lock(this->mutex);
        return this->ids;
    }

    SetVariableStatusEnum result = SetVariableStatusEnum::Accepted;
    std::chrono::milliseconds delay{0};
    std::atomic<int> calls_running{0};
    /// \brief What the owner does with a value before the provider returns
    std::function<void(const Component&, const Variable&, const std::string&, WriteId)> on_set;

private:
    std::mutex mutex;
    std::vector<std::string> values;
    std::vector<WriteId> ids;
};

class DeviceModelRegistryTest : public ::testing::Test {
protected:
    FakeProvider provider;
    DeviceModelTestHelper device_model_test_helper;
    DeviceModel* dm = device_model_test_helper.get_device_model();
    const Component config_component = make_component("Auth", std::nullopt);
    const Variable timeout = make_variable("connection_timeout");
    /// \brief Written by the listener, which concurrent writes call concurrently
    std::vector<std::pair<std::string, std::string>> changes;
    std::mutex changes_mutex;

    void SetUp() override {
        dm->register_variable_listener([this](const std::unordered_map<std::int64_t, VariableMonitoringMeta>&,
                                              const Component&, const Variable&, const VariableCharacteristics&,
                                              const VariableAttribute&, const std::string& value_previous,
                                              const std::string& value_current) {
            std::lock_guard<std::mutex> lock(this->changes_mutex);
            changes.emplace_back(value_previous, value_current);
        });
    }

    void register_config_view() {
        RegisteredComponent component;
        component.component = config_component;
        component.variables.push_back(make_registered_variable(timeout.name, "60"));
        const auto result = dm->register_components(OWNER, {component}, &provider);
        ASSERT_TRUE(result.rejected.empty());
    }

    std::string get(const Component& component, const Variable& variable) {
        std::string value;
        EXPECT_EQ(dm->get_variable(component, variable, AttributeEnum::Actual, value, false, UnsetValue::NotSupported),
                  GetVariableStatusEnum::Accepted);
        return value;
    }

    GetVariableStatusEnum status(const Component& component, const Variable& variable) {
        std::string value;
        return dm->get_variable(component, variable, AttributeEnum::Actual, value, false, UnsetValue::NotSupported);
    }

    bool visible(const Component& component, const Variable& variable) {
        return dm->get_variable_meta_data(component, variable).has_value();
    }
};

} // namespace

TEST_F(DeviceModelRegistryTest, registered_variable_is_part_of_the_model) {
    register_config_view();

    EXPECT_EQ(get(config_component, timeout), "60");
    const auto meta_data = dm->get_variable_meta_data(config_component, timeout);
    ASSERT_TRUE(meta_data.has_value());
    EXPECT_EQ(meta_data->owner, OWNER);
    ASSERT_EQ(meta_data->attributes.size(), 1);
    EXPECT_EQ(meta_data->attributes.at(0).persistent, false);

    const auto report = dm->get_custom_report_data(std::vector<ComponentVariable>{{config_component, timeout}});
    ASSERT_EQ(report.size(), 1);
    EXPECT_EQ(report.at(0).variableAttribute.at(0).value.value().get(), "60");
}

TEST_F(DeviceModelRegistryTest, accepted_write_is_stored_in_memory) {
    register_config_view();

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(provider.received(), std::vector<std::string>{"90"});
    EXPECT_EQ(get(config_component, timeout), "90");
    const std::vector<std::pair<std::string, std::string>> expected{{"60", "90"}};
    EXPECT_EQ(changes, expected);
}

TEST_F(DeviceModelRegistryTest, invalid_value_never_reaches_the_provider) {
    register_config_view();

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "5000", "csms"),
              SetVariableStatusEnum::Rejected);
    EXPECT_TRUE(provider.received().empty());
    EXPECT_EQ(get(config_component, timeout), "60");
}

TEST_F(DeviceModelRegistryTest, rejected_write_leaves_memory_unchanged) {
    register_config_view();
    provider.result = SetVariableStatusEnum::Rejected;

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Rejected);
    EXPECT_EQ(get(config_component, timeout), "60");
    EXPECT_TRUE(changes.empty());
}

TEST_F(DeviceModelRegistryTest, reboot_required_write_is_stored_in_memory) {
    register_config_view();
    provider.result = SetVariableStatusEnum::RebootRequired;

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::RebootRequired);
    EXPECT_EQ(provider.received(), std::vector<std::string>{"90"});
    EXPECT_EQ(get(config_component, timeout), "90");
}

TEST_F(DeviceModelRegistryTest, report_without_write_id_during_a_write_is_overwritten_by_the_write) {
    register_config_view();
    provider.on_set = [this](const Component& component, const Variable& variable, const std::string&, WriteId) {
        EXPECT_EQ(dm->notify_value_changed(component, variable, AttributeEnum::Actual, "40", OWNER, std::nullopt),
                  SetVariableStatusEnum::Accepted);
    };

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(config_component, timeout), "90");
}

TEST_F(DeviceModelRegistryTest, report_based_on_the_write_in_progress_is_not_overwritten_by_it) {
    register_config_view();
    // The owner applies the write, then changes the value itself
    provider.on_set = [this](const Component& component, const Variable& variable, const std::string&,
                             const WriteId write_id) {
        EXPECT_EQ(dm->notify_value_changed(component, variable, AttributeEnum::Actual, "40", OWNER, write_id),
                  SetVariableStatusEnum::Accepted);
    };

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(config_component, timeout), "40");
    const std::vector<std::pair<std::string, std::string>> expected{{"60", "40"}};
    EXPECT_EQ(changes, expected);
}

TEST_F(DeviceModelRegistryTest, report_based_on_a_write_older_than_the_stored_one_is_ignored) {
    register_config_view();
    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Accepted);
    const auto write_id = provider.write_ids().at(0);

    // The owner changed the value before it applied the write
    EXPECT_EQ(dm->notify_value_changed(config_component, timeout, AttributeEnum::Actual, "40", OWNER, write_id - 1),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(config_component, timeout), "90");

    EXPECT_EQ(dm->notify_value_changed(config_component, timeout, AttributeEnum::Actual, "50", OWNER, write_id),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(config_component, timeout), "50");
    const std::vector<std::pair<std::string, std::string>> expected{{"60", "90"}, {"90", "50"}};
    EXPECT_EQ(changes, expected);
}

TEST_F(DeviceModelRegistryTest, write_ids_increase) {
    register_config_view();
    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "120", "csms"),
              SetVariableStatusEnum::Accepted);

    const auto write_ids = provider.write_ids();
    ASSERT_EQ(write_ids.size(), 2);
    EXPECT_GT(write_ids.at(0), 0);
    EXPECT_GT(write_ids.at(1), write_ids.at(0));
}

TEST_F(DeviceModelRegistryTest, clearing_a_registered_variable_is_rejected) {
    register_config_view();

    EXPECT_EQ(dm->clear_value(config_component, timeout, AttributeEnum::Actual, "internal"),
              SetVariableStatusEnum::Rejected);
    EXPECT_TRUE(provider.received().empty());
    EXPECT_EQ(get(config_component, timeout), "60");
}

TEST_F(DeviceModelRegistryTest, notified_value_updates_memory_without_calling_the_provider) {
    register_config_view();

    EXPECT_EQ(dm->notify_value_changed(config_component, timeout, AttributeEnum::Actual, "120", OWNER, std::nullopt),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(config_component, timeout), "120");
    EXPECT_TRUE(provider.received().empty());
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0], std::make_pair(std::string("60"), std::string("120")));
}

TEST_F(DeviceModelRegistryTest, notified_value_of_another_owner_is_rejected) {
    register_config_view();

    EXPECT_EQ(
        dm->notify_value_changed(config_component, timeout, AttributeEnum::Actual, "120", OTHER_OWNER, std::nullopt),
        SetVariableStatusEnum::Rejected);
    const auto& heartbeat = ControllerComponentVariables::HeartbeatInterval;
    EXPECT_EQ(dm->notify_value_changed(heartbeat.component, heartbeat.variable.value(), AttributeEnum::Actual, "1",
                                       OWNER, std::nullopt),
              SetVariableStatusEnum::Rejected);
    EXPECT_EQ(get(config_component, timeout), "60");
}

TEST_F(DeviceModelRegistryTest, notified_value_of_the_wrong_type_is_rejected) {
    register_config_view();

    EXPECT_EQ(dm->notify_value_changed(config_component, timeout, AttributeEnum::Actual, "abc", OWNER, std::nullopt),
              SetVariableStatusEnum::Rejected);
    EXPECT_EQ(get(config_component, timeout), "60");
    EXPECT_TRUE(changes.empty());
}

TEST_F(DeviceModelRegistryTest, notified_value_outside_the_limits_is_accepted) {
    register_config_view();

    EXPECT_EQ(dm->notify_value_changed(config_component, timeout, AttributeEnum::Actual, "5000", OWNER, std::nullopt),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(config_component, timeout), "5000");
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0], std::make_pair(std::string("60"), std::string("5000")));

    // The limits still bound what the CSMS may set
    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "5001", "csms"),
              SetVariableStatusEnum::Rejected);
}

TEST_F(DeviceModelRegistryTest, second_runtime_owner_is_rejected) {
    register_config_view();

    RegisteredComponent component;
    component.component = config_component;
    component.variables.push_back(make_registered_variable(timeout.name, "1"));
    const auto result = dm->register_components(OTHER_OWNER, {component}, nullptr);
    ASSERT_EQ(result.rejected.size(), 1);
    EXPECT_EQ(result.rejected.at(0).reason, RegistrationRejectionReason::AlreadyDefined);
    EXPECT_EQ(result.rejected.at(0).defined_by, OWNER);
    EXPECT_EQ(get(config_component, timeout), "60");
    EXPECT_EQ(dm->get_variable_meta_data(config_component, timeout)->owner, OWNER);
}

TEST_F(DeviceModelRegistryTest, persistent_attribute_with_provider_is_reported_but_not_stored) {
    RegisteredComponent component;
    component.component = config_component;
    component.variables.push_back(
        make_registered_variable(timeout.name, "1", MutabilityEnum::ReadWrite, DataEnum::integer, true));
    ASSERT_TRUE(dm->register_components(OWNER, {component}, &provider).rejected.empty());

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "2", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(dm->notify_value_changed(config_component, timeout, AttributeEnum::Actual, "3", OWNER, std::nullopt),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(config_component, timeout), "3");
    EXPECT_EQ(provider.received(), std::vector<std::string>{"2"});

    const auto meta_data = dm->get_variable_meta_data(config_component, timeout);
    ASSERT_TRUE(meta_data.has_value());
    EXPECT_TRUE(meta_data->attributes.at(0).persistent.value_or(false));
    DeviceModelStorageSqlite storage(DEVICE_MODEL_DB_IN_MEMORY_PATH);
    EXPECT_TRUE(storage.get_stored_values().empty());
}

TEST_F(DeviceModelRegistryTest, variable_of_the_component_config_is_rejected) {
    const auto evse = make_component("EVSE", 1);
    const auto power = make_variable("Power");
    const auto vendor = make_variable("VendorPower");

    RegisteredComponent component;
    component.component = evse;
    auto registered = make_registered_variable("Power", "7360", MutabilityEnum::ReadOnly, DataEnum::decimal);
    registered.characteristics.maxLimit = 11000;
    component.variables.push_back(registered);
    component.variables.push_back(make_registered_variable(vendor.name, "1"));
    const auto result = dm->register_components(OTHER_OWNER, {component}, nullptr);

    ASSERT_EQ(result.rejected.size(), 1);
    EXPECT_EQ(result.rejected.at(0).component, evse);
    EXPECT_EQ(result.rejected.at(0).variable, power);
    EXPECT_EQ(result.rejected.at(0).reason, RegistrationRejectionReason::AlreadyDefined);
    EXPECT_EQ(result.rejected.at(0).defined_by, VARIABLE_OWNER_COMPONENT_CONFIG);
    EXPECT_EQ(dm->get_variable_meta_data(evse, power)->characteristics.maxLimit, 22000);
    EXPECT_EQ(dm->get_variable_meta_data(evse, power)->owner, VARIABLE_OWNER_COMPONENT_CONFIG);
    // A component combines the variables of the component config and of a registration
    EXPECT_EQ(get(evse, vendor), "1");
    EXPECT_TRUE(visible(evse, make_variable("AllowReset")));
}

TEST_F(DeviceModelRegistryTest, registering_again_replaces_the_previous_registration) {
    register_config_view();

    RegisteredComponent component;
    component.component = config_component;
    component.variables.push_back(make_registered_variable("master_pass_group_id", "1"));
    ASSERT_TRUE(dm->register_components(OWNER, {component}, nullptr).rejected.empty());

    EXPECT_EQ(status(config_component, timeout), GetVariableStatusEnum::UnknownVariable);
    EXPECT_EQ(get(config_component, make_variable("master_pass_group_id")), "1");
    // Without a provider, writes are applied to the model
    EXPECT_EQ(
        dm->set_value(config_component, make_variable("master_pass_group_id"), AttributeEnum::Actual, "2", "csms"),
        SetVariableStatusEnum::Accepted);
    EXPECT_TRUE(provider.received().empty());
}

TEST_F(DeviceModelRegistryTest, registering_again_notifies_the_listeners_of_changed_values) {
    RegisteredComponent component;
    component.component = config_component;
    component.variables.push_back(make_registered_variable(timeout.name, "60"));
    component.variables.push_back(make_registered_variable("master_pass_group_id", "1"));
    ASSERT_TRUE(dm->register_components(OWNER, {component}, nullptr).rejected.empty());
    EXPECT_TRUE(changes.empty());

    component.variables.at(0) = make_registered_variable(timeout.name, "70");
    ASSERT_TRUE(dm->register_components(OWNER, {component}, nullptr).rejected.empty());

    const std::vector<std::pair<std::string, std::string>> expected{{"60", "70"}};
    EXPECT_EQ(changes, expected);
    EXPECT_EQ(get(config_component, timeout), "70");
}

TEST_F(DeviceModelRegistryTest, reserved_owner_is_rejected) {
    RegisteredComponent component;
    component.component = config_component;
    component.variables.push_back(make_registered_variable(timeout.name, "60"));

    const auto result = dm->register_components(VARIABLE_OWNER_COMPONENT_CONFIG, {component}, nullptr);
    ASSERT_EQ(result.rejected.size(), 1);
    EXPECT_EQ(result.rejected.at(0).reason, RegistrationRejectionReason::ReservedOwner);
    EXPECT_FALSE(visible(config_component, timeout));
}

TEST_F(DeviceModelRegistryTest, variables_that_libocpp_writes_are_rejected) {
    Component slot;
    slot.name = "NetworkConfiguration";
    slot.instance = "7";
    const std::vector<RegisteredComponent> components{
        {ControllerComponents::LocalAuthListCtrlr, {make_registered_variable("Entries", "0")}},
        {ControllerComponents::SecurityCtrlr,
         {make_registered_variable("Identity", "cp001", MutabilityEnum::ReadWrite, DataEnum::string)}},
        {make_component("EVSE", 3),
         {make_registered_variable("AvailabilityState", "Available", MutabilityEnum::ReadOnly, DataEnum::OptionList),
          make_registered_variable("Power", "0", MutabilityEnum::ReadOnly, DataEnum::decimal)}},
        {make_component("Connector", 3, 1),
         {make_registered_variable("AvailabilityState", "Available", MutabilityEnum::ReadOnly, DataEnum::OptionList)}},
        {slot, {make_registered_variable("VendorTimeout", "1")}},
    };

    const auto result = dm->register_components(OWNER, components, nullptr);

    ASSERT_EQ(result.rejected.size(), 5);
    for (const auto& rejected : result.rejected) {
        EXPECT_EQ(rejected.reason, RegistrationRejectionReason::ReservedVariable) << rejected.variable;
    }
    EXPECT_FALSE(visible(make_component("EVSE", 3), make_variable("AvailabilityState")));
    EXPECT_FALSE(visible(make_component("Connector", 3, 1), make_variable("AvailabilityState")));
    EXPECT_FALSE(visible(slot, make_variable("VendorTimeout")));
    EXPECT_TRUE(dm->create_network_configuration_slot_from_default_schema(7));
    EXPECT_TRUE(visible(make_component("EVSE", 3), make_variable("Power")));
}

TEST_F(DeviceModelRegistryTest, second_definition_of_a_variable_in_one_registration_is_rejected) {
    RegisteredComponent component;
    component.component = config_component;
    component.variables.push_back(make_registered_variable(timeout.name, "60"));
    component.variables.push_back(make_registered_variable(timeout.name, "70"));

    const auto result = dm->register_components(OWNER, {component}, nullptr);

    ASSERT_EQ(result.rejected.size(), 1);
    EXPECT_EQ(result.rejected.at(0).reason, RegistrationRejectionReason::AlreadyDefined);
    EXPECT_EQ(result.rejected.at(0).defined_by, OWNER);
    EXPECT_EQ(get(config_component, timeout), "60");
}

/// Monitors and stored values key an absent instance as '' and an absent id as 0
TEST_F(DeviceModelRegistryTest, name_keys_that_monitors_and_stored_values_cannot_represent_are_rejected) {
    auto empty_instance = config_component;
    empty_instance.instance = "";
    auto empty_variable_instance = make_registered_variable(timeout.name, "60");
    empty_variable_instance.variable.instance = "";

    std::vector<RegisteredComponent> components{
        {empty_instance, {make_registered_variable(timeout.name, "60")}},
        {config_component, {empty_variable_instance}},
        {make_component("EVSE", 0), {make_registered_variable("Power", "0")}},
        {make_component("Connector", 1, 0), {make_registered_variable("Power", "0")}},
        {make_component("Connector", 1, 1), {make_registered_variable("Power", "0")}},
    };
    const auto result = dm->register_components(OWNER, components, nullptr);

    ASSERT_EQ(result.rejected.size(), 4);
    for (const auto& rejected : result.rejected) {
        EXPECT_EQ(rejected.reason, RegistrationRejectionReason::InvalidNameKey);
        EXPECT_FALSE(visible(rejected.component, rejected.variable));
    }
    EXPECT_TRUE(visible(make_component("Connector", 1, 1), make_variable("Power")));
}

TEST_F(DeviceModelRegistryTest, libocpp_cannot_set_read_only_values_of_other_owners) {
    const auto evse = make_component("EVSE", 3);
    RegisteredComponent component;
    component.component = evse;
    component.variables.push_back(make_registered_variable("Power", "0", MutabilityEnum::ReadOnly, DataEnum::decimal));
    ASSERT_TRUE(dm->register_components(OTHER_OWNER, {component}, nullptr).rejected.empty());

    EXPECT_EQ(dm->set_read_only_value(evse, EvseComponentVariables::Power, AttributeEnum::Actual, "100", "internal"),
              SetVariableStatusEnum::Rejected);
    EXPECT_EQ(get(evse, EvseComponentVariables::Power), "0");
}

TEST_F(DeviceModelRegistryTest, local_channel_writes_the_component_config_only) {
    const auto evse = make_component("EVSE", 3);
    RegisteredComponent component;
    component.component = evse;
    component.variables.push_back(make_registered_variable("Power", "0", MutabilityEnum::ReadOnly, DataEnum::decimal));
    component.variables.push_back(
        make_registered_variable("AllowReset", "false", MutabilityEnum::ReadWrite, DataEnum::boolean));
    ASSERT_TRUE(dm->register_components(OTHER_OWNER, {component}, nullptr).rejected.empty());
    register_config_view();

    // the registered variables are rejected whatever their mutability
    EXPECT_EQ(dm->set_value(evse, EvseComponentVariables::Power, AttributeEnum::Actual, "100", "internal", true),
              SetVariableStatusEnum::Rejected);
    EXPECT_EQ(get(evse, EvseComponentVariables::Power), "0");
    EXPECT_EQ(dm->set_value(evse, EvseComponentVariables::AllowReset, AttributeEnum::Actual, "true", "internal", true),
              SetVariableStatusEnum::Rejected);
    EXPECT_EQ(get(evse, EvseComponentVariables::AllowReset), "false");
    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "internal", true),
              SetVariableStatusEnum::Rejected);
    EXPECT_TRUE(provider.received().empty());
    EXPECT_EQ(get(config_component, timeout), "60");

    // the CSMS still writes a ReadWrite registered variable, and the local channel still writes the component config
    EXPECT_EQ(dm->set_value(evse, EvseComponentVariables::AllowReset, AttributeEnum::Actual, "true", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(evse, EvseComponentVariables::AllowReset), "true");
    EXPECT_EQ(dm->set_value(ControllerComponents::InternalCtrlr, make_variable("ChargePointModel"),
                            AttributeEnum::Actual, "Model", "internal", true),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(ControllerComponents::InternalCtrlr, make_variable("ChargePointModel")), "Model");
}

TEST_F(DeviceModelRegistryTest, persistent_attribute_without_provider_is_stored) {
    const auto der = make_component("ACDERCtrlr", 1);
    RegisteredComponent component;
    component.component = der;
    component.variables.push_back(
        make_registered_variable("Enabled", "true", MutabilityEnum::ReadWrite, DataEnum::boolean, true));
    ASSERT_TRUE(dm->register_components(OTHER_OWNER, {component}, nullptr).rejected.empty());

    EXPECT_EQ(dm->set_value(der, make_variable("Enabled"), AttributeEnum::Actual, "false", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(der, make_variable("Enabled")), "false");

    DeviceModelStorageSqlite storage(DEVICE_MODEL_DB_IN_MEMORY_PATH);
    const auto stored = storage.get_stored_values();
    ASSERT_EQ(stored.size(), 1);
    EXPECT_EQ(stored.at(0).component, der);
    EXPECT_EQ(stored.at(0).value, "false");
    EXPECT_EQ(stored.at(0).owner, OTHER_OWNER);
}

TEST_F(DeviceModelRegistryTest, delta_monitor_on_registered_variable_takes_reference_from_memory) {
    register_config_view();
    SetMonitoringData request;
    request.component = config_component;
    request.variable = timeout;
    request.type = MonitorEnum::Delta;
    request.value = 5;
    request.severity = 3;

    const auto results = dm->set_monitors({request});
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results.at(0).status, SetMonitoringStatusEnum::Accepted);
    const auto meta_data = dm->get_variable_meta_data(config_component, timeout);
    EXPECT_EQ(meta_data->monitors.at(results.at(0).id.value()).reference_value, "60");
}

TEST_F(DeviceModelRegistryTest, overlapping_writes_leave_the_value_of_the_newest_write) {
    register_config_view();
    provider.delay = std::chrono::milliseconds(20);

    std::vector<std::thread> writers;
    for (int i = 1; i <= 5; i++) {
        writers.emplace_back([this, i]() {
            EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, std::to_string(i * 10), "csms"),
                      SetVariableStatusEnum::Accepted);
        });
    }
    // Reads do not wait for the provider: the read returns while writers are still running
    std::string value;
    dm->get_variable(config_component, timeout, AttributeEnum::Actual, value, false, UnsetValue::NotSupported);
    EXPECT_LT(provider.received().size(), 5);

    for (auto& writer : writers) {
        writer.join();
    }
    const auto values = provider.received();
    const auto write_ids = provider.write_ids();
    ASSERT_EQ(values.size(), 5);
    const auto newest = std::max_element(write_ids.begin(), write_ids.end()) - write_ids.begin();
    EXPECT_EQ(get(config_component, timeout), values.at(newest));
}

TEST_F(DeviceModelRegistryTest, unregistering_waits_for_a_write_in_progress) {
    register_config_view();
    provider.delay = std::chrono::milliseconds(50);

    std::thread writer([this]() { dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"); });
    while (provider.calls_running.load() == 0) {
        std::this_thread::yield();
    }
    dm->unregister_components(OWNER);

    EXPECT_EQ(provider.calls_running.load(), 0);
    writer.join();
    EXPECT_EQ(provider.received(), std::vector<std::string>{"90"});
}

TEST_F(DeviceModelRegistryTest, listener_may_write_the_variable_it_is_notified_of) {
    register_config_view();
    dm->register_variable_listener([this](const std::unordered_map<std::int64_t, VariableMonitoringMeta>&,
                                          const Component&, const Variable&, const VariableCharacteristics&,
                                          const VariableAttribute&, const std::string&,
                                          const std::string& value_current) {
        if (value_current == "90") {
            EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "120", "csms"),
                      SetVariableStatusEnum::Accepted);
        }
    });

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(provider.received(), (std::vector<std::string>{"90", "120"}));
    EXPECT_EQ(get(config_component, timeout), "120");
}

TEST_F(DeviceModelRegistryTest, listener_may_unregister_the_owner_of_the_variable) {
    register_config_view();
    dm->register_variable_listener([this](const std::unordered_map<std::int64_t, VariableMonitoringMeta>&,
                                          const Component&, const Variable&, const VariableCharacteristics&,
                                          const VariableAttribute&, const std::string&,
                                          const std::string&) { dm->unregister_components(OWNER); });

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_FALSE(visible(config_component, timeout));
}

TEST_F(DeviceModelRegistryTest, provider_may_unregister_its_owner_and_register_another_provider) {
    register_config_view();
    FakeProvider replacement;
    provider.on_set = [this, &replacement](const Component&, const Variable&, const std::string& value, WriteId) {
        RegisteredComponent component;
        component.component = config_component;
        component.variables.push_back(make_registered_variable(timeout.name, value));
        EXPECT_TRUE(dm->register_components(OWNER, {component}, &replacement).rejected.empty());
        dm->unregister_components(OWNER);
    };

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_FALSE(visible(config_component, timeout));
    EXPECT_TRUE(replacement.received().empty());
}

TEST_F(DeviceModelRegistryTest, write_in_progress_returns_the_result_of_the_provider_after_unregistering) {
    register_config_view();
    provider.delay = std::chrono::milliseconds(50);

    std::thread writer([this]() {
        EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
                  SetVariableStatusEnum::Accepted);
    });
    while (provider.calls_running.load() == 0) {
        std::this_thread::yield();
    }
    dm->unregister_components(OWNER);
    writer.join();

    EXPECT_EQ(provider.received(), std::vector<std::string>{"90"});
    EXPECT_EQ(status(config_component, timeout), GetVariableStatusEnum::UnknownComponent);
    EXPECT_TRUE(changes.empty());
}

TEST_F(DeviceModelRegistryTest, write_after_unregistering_does_not_reach_the_provider) {
    register_config_view();
    dm->unregister_components(OWNER);

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::UnknownComponent);
    EXPECT_TRUE(provider.received().empty());
}

TEST_F(DeviceModelRegistryTest, registering_again_during_a_write_keeps_the_registered_value) {
    register_config_view();
    provider.delay = std::chrono::milliseconds(50);
    FakeProvider replacement;

    std::thread writer([this]() { dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"); });
    while (provider.calls_running.load() == 0) {
        std::this_thread::yield();
    }
    RegisteredComponent component;
    component.component = config_component;
    component.variables.push_back(make_registered_variable(timeout.name, "75"));
    ASSERT_TRUE(dm->register_components(OWNER, {component}, &replacement).rejected.empty());
    writer.join();

    EXPECT_EQ(provider.received(), std::vector<std::string>{"90"});
    EXPECT_EQ(get(config_component, timeout), "75");
}

} // namespace ocpp::v2
