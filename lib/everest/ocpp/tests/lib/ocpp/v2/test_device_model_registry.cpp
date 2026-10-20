// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include <device_model_test_helper.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>

namespace ocpp::v2 {

static_assert(!std::is_base_of_v<DeviceModelRegistryInterface, DeviceModelAbstract>);
static_assert(std::is_base_of_v<DeviceModelRegistryInterface, DeviceModel>);

namespace {

const std::string SOURCE = "EVEREST_CONFIG";
const std::string OTHER_SOURCE = "EVEREST";

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
    SetVariableStatusEnum set_value(const Component& /*component*/, const Variable& /*variable*/,
                                    AttributeEnum /*attribute*/, const std::string& value,
                                    const std::string& /*source*/) override {
        const auto running = ++this->calls_running;
        this->max_calls_running = std::max(this->max_calls_running.load(), running);
        std::this_thread::sleep_for(this->delay);
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->values.push_back(value);
        }
        --this->calls_running;
        return this->result;
    }

    std::vector<std::string> received() {
        std::lock_guard<std::mutex> lock(this->mutex);
        return this->values;
    }

    SetVariableStatusEnum result = SetVariableStatusEnum::Accepted;
    std::chrono::milliseconds delay{0};
    std::atomic<int> calls_running{0};
    std::atomic<int> max_calls_running{0};

private:
    std::mutex mutex;
    std::vector<std::string> values;
};

class DeviceModelRegistryTest : public ::testing::Test {
protected:
    FakeProvider provider;
    DeviceModelTestHelper device_model_test_helper;
    DeviceModel* dm = device_model_test_helper.get_device_model();
    const Component config_component = make_component("Auth", std::nullopt);
    const Variable timeout = make_variable("connection_timeout");
    std::vector<std::pair<std::string, std::string>> changes;

    void SetUp() override {
        dm->register_variable_listener(
            [this](const std::unordered_map<std::int64_t, VariableMonitoringMeta>&, const Component&, const Variable&,
                   const VariableCharacteristics&, const VariableAttribute&, const std::string& value_previous,
                   const std::string& value_current) { changes.emplace_back(value_previous, value_current); });
    }

    void register_config_view() {
        RegisteredComponent component;
        component.component = config_component;
        component.variables.push_back(make_registered_variable(timeout.name, "60"));
        const auto result = dm->register_components(SOURCE, {component}, &provider);
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
    EXPECT_EQ(meta_data->source, SOURCE);
    ASSERT_EQ(meta_data->attributes.size(), 1);
    EXPECT_EQ(meta_data->attributes.at(0).persistent, false);

    const auto report = dm->get_custom_report_data(std::vector<ComponentVariable>{{config_component, timeout}});
    ASSERT_EQ(report.size(), 1);
    EXPECT_EQ(report.at(0).variableAttribute.at(0).value.value().get(), "60");
}

TEST_F(DeviceModelRegistryTest, write_goes_to_the_provider_and_then_to_memory) {
    register_config_view();

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(provider.received(), std::vector<std::string>{"90"});
    EXPECT_EQ(get(config_component, timeout), "90");
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0], std::make_pair(std::string("60"), std::string("90")));
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

TEST_F(DeviceModelRegistryTest, reboot_required_write_updates_memory) {
    register_config_view();
    provider.result = SetVariableStatusEnum::RebootRequired;

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::RebootRequired);
    EXPECT_EQ(get(config_component, timeout), "90");
}

TEST_F(DeviceModelRegistryTest, notified_value_updates_memory_without_calling_the_provider) {
    register_config_view();

    EXPECT_EQ(dm->notify_value_changed(config_component, timeout, AttributeEnum::Actual, "120", SOURCE),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(config_component, timeout), "120");
    EXPECT_TRUE(provider.received().empty());
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0], std::make_pair(std::string("60"), std::string("120")));
}

TEST_F(DeviceModelRegistryTest, notified_value_of_another_source_is_rejected) {
    register_config_view();

    EXPECT_EQ(dm->notify_value_changed(config_component, timeout, AttributeEnum::Actual, "120", OTHER_SOURCE),
              SetVariableStatusEnum::Rejected);
    const auto& heartbeat = ControllerComponentVariables::HeartbeatInterval;
    EXPECT_EQ(
        dm->notify_value_changed(heartbeat.component, heartbeat.variable.value(), AttributeEnum::Actual, "1", SOURCE),
        SetVariableStatusEnum::Rejected);
    EXPECT_EQ(get(config_component, timeout), "60");
}

TEST_F(DeviceModelRegistryTest, notified_value_of_the_wrong_type_is_rejected) {
    register_config_view();

    EXPECT_EQ(dm->notify_value_changed(config_component, timeout, AttributeEnum::Actual, "abc", SOURCE),
              SetVariableStatusEnum::Rejected);
    EXPECT_EQ(get(config_component, timeout), "60");
    EXPECT_TRUE(changes.empty());
}

TEST_F(DeviceModelRegistryTest, notified_value_outside_the_limits_is_accepted) {
    register_config_view();

    EXPECT_EQ(dm->notify_value_changed(config_component, timeout, AttributeEnum::Actual, "5000", SOURCE),
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
    const auto result = dm->register_components(OTHER_SOURCE, {component}, nullptr);
    ASSERT_EQ(result.rejected.size(), 1);
    EXPECT_EQ(result.rejected.at(0).reason, RegistrationRejectionReason::OwnedByOtherSource);
    EXPECT_EQ(get(config_component, timeout), "60");
    EXPECT_EQ(dm->get_variable_meta_data(config_component, timeout)->source, SOURCE);
}

TEST_F(DeviceModelRegistryTest, persistent_attribute_with_provider_is_reported_but_not_stored) {
    RegisteredComponent component;
    component.component = config_component;
    component.variables.push_back(
        make_registered_variable(timeout.name, "1", MutabilityEnum::ReadWrite, DataEnum::integer, true));
    ASSERT_TRUE(dm->register_components(SOURCE, {component}, &provider).rejected.empty());

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "2", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(dm->notify_value_changed(config_component, timeout, AttributeEnum::Actual, "3", SOURCE),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(config_component, timeout), "3");
    EXPECT_EQ(provider.received(), std::vector<std::string>{"2"});

    const auto meta_data = dm->get_variable_meta_data(config_component, timeout);
    ASSERT_TRUE(meta_data.has_value());
    EXPECT_TRUE(meta_data->attributes.at(0).persistent.value_or(false));
    DeviceModelStorageSqlite storage(DEVICE_MODEL_DB_IN_MEMORY_PATH);
    EXPECT_TRUE(storage.get_stored_values().empty());
}

TEST_F(DeviceModelRegistryTest, registration_overrides_component_config_until_unregistered) {
    const auto evse = make_component("EVSE", 1);
    const auto power = make_variable("Power");
    const auto json_meta = dm->get_variable_meta_data(evse, power);
    ASSERT_TRUE(json_meta.has_value());
    EXPECT_EQ(json_meta->characteristics.maxLimit, 22000);

    RegisteredComponent component;
    component.component = evse;
    auto registered = make_registered_variable("Power", "7360", MutabilityEnum::ReadOnly, DataEnum::decimal);
    registered.characteristics.maxLimit = 11000;
    component.variables.push_back(registered);
    ASSERT_TRUE(dm->register_components(OTHER_SOURCE, {component}, nullptr).rejected.empty());

    EXPECT_EQ(get(evse, power), "7360");
    EXPECT_EQ(dm->get_variable_meta_data(evse, power)->characteristics.maxLimit, 11000);
    // The other variables of the component config stay visible
    EXPECT_TRUE(visible(evse, make_variable("AllowReset")));

    dm->unregister_components(OTHER_SOURCE);
    EXPECT_EQ(dm->get_variable_meta_data(evse, power)->characteristics.maxLimit, 22000);
}

TEST_F(DeviceModelRegistryTest, component_config_of_unregistered_evse_is_hidden) {
    const auto evse_2_allow_reset = std::pair{make_component("EVSE", 2), make_variable("AllowReset")};
    const auto connector_2 = std::pair{make_component("Connector", 2, 1), make_variable("ConnectorType")};
    EXPECT_TRUE(visible(evse_2_allow_reset.first, evse_2_allow_reset.second));

    RegisteredComponent evse_1;
    evse_1.component = make_component("EVSE", 1);
    evse_1.variables.push_back(make_registered_variable("Power", "0", MutabilityEnum::ReadOnly, DataEnum::decimal));
    RegisteredComponent connector_1;
    connector_1.component = make_component("Connector", 1, 1);
    connector_1.variables.push_back(
        make_registered_variable("SupplyPhases", "3", MutabilityEnum::ReadOnly, DataEnum::integer));
    ASSERT_TRUE(dm->register_components(OTHER_SOURCE, {evse_1, connector_1}, nullptr).rejected.empty());

    EXPECT_FALSE(visible(evse_2_allow_reset.first, evse_2_allow_reset.second));
    EXPECT_FALSE(visible(connector_2.first, connector_2.second));
    // Component config variables of the registered EVSE and connector stay visible
    EXPECT_TRUE(visible(make_component("EVSE", 1), make_variable("AllowReset")));
    EXPECT_TRUE(visible(make_component("Connector", 1, 1), make_variable("ConnectorType")));

    dm->unregister_components(OTHER_SOURCE);
    EXPECT_TRUE(visible(evse_2_allow_reset.first, evse_2_allow_reset.second));
    EXPECT_TRUE(visible(connector_2.first, connector_2.second));
}

TEST_F(DeviceModelRegistryTest, registering_again_replaces_the_previous_registration) {
    register_config_view();

    RegisteredComponent component;
    component.component = config_component;
    component.variables.push_back(make_registered_variable("master_pass_group_id", "1"));
    ASSERT_TRUE(dm->register_components(SOURCE, {component}, nullptr).rejected.empty());

    EXPECT_EQ(status(config_component, timeout), GetVariableStatusEnum::UnknownVariable);
    EXPECT_EQ(get(config_component, make_variable("master_pass_group_id")), "1");
    // Without a provider, writes are applied to the model
    EXPECT_EQ(
        dm->set_value(config_component, make_variable("master_pass_group_id"), AttributeEnum::Actual, "2", "csms"),
        SetVariableStatusEnum::Accepted);
    EXPECT_TRUE(provider.received().empty());
}

TEST_F(DeviceModelRegistryTest, libocpp_cannot_set_read_only_values_of_other_sources) {
    const auto evse = make_component("EVSE", 1);
    RegisteredComponent component;
    component.component = evse;
    component.variables.push_back(make_registered_variable("Power", "0", MutabilityEnum::ReadOnly, DataEnum::decimal));
    component.variables.push_back(
        make_registered_variable("AvailabilityState", "Available", MutabilityEnum::ReadOnly, DataEnum::OptionList));
    ASSERT_TRUE(dm->register_components(OTHER_SOURCE, {component}, nullptr).rejected.empty());

    EXPECT_EQ(dm->set_read_only_value(evse, EvseComponentVariables::Power, AttributeEnum::Actual, "100", "internal"),
              SetVariableStatusEnum::Rejected);
    EXPECT_EQ(get(evse, EvseComponentVariables::Power), "0");

    dm->unregister_components(OTHER_SOURCE);
    RegisteredComponent own;
    own.component = evse;
    own.variables.push_back(
        make_registered_variable("AvailabilityState", "Available", MutabilityEnum::ReadOnly, DataEnum::OptionList));
    ASSERT_TRUE(dm->register_components(VARIABLE_SOURCE_OCPP_RUNTIME, {own}, nullptr).rejected.empty());
    EXPECT_EQ(dm->set_read_only_value(evse, EvseComponentVariables::AvailabilityState, AttributeEnum::Actual,
                                      "Occupied", "internal"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(evse, EvseComponentVariables::AvailabilityState), "Occupied");
}

TEST_F(DeviceModelRegistryTest, local_channel_writes_the_component_config_only) {
    const auto evse = make_component("EVSE", 1);
    RegisteredComponent component;
    component.component = evse;
    component.variables.push_back(make_registered_variable("Power", "0", MutabilityEnum::ReadOnly, DataEnum::decimal));
    component.variables.push_back(
        make_registered_variable("AllowReset", "false", MutabilityEnum::ReadWrite, DataEnum::boolean));
    ASSERT_TRUE(dm->register_components(OTHER_SOURCE, {component}, nullptr).rejected.empty());
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
    ASSERT_TRUE(dm->register_components(OTHER_SOURCE, {component}, nullptr).rejected.empty());

    EXPECT_EQ(dm->set_value(der, make_variable("Enabled"), AttributeEnum::Actual, "false", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(get(der, make_variable("Enabled")), "false");

    DeviceModelStorageSqlite storage(DEVICE_MODEL_DB_IN_MEMORY_PATH);
    const auto stored = storage.get_stored_values();
    ASSERT_EQ(stored.size(), 1);
    EXPECT_EQ(stored.at(0).component, der);
    EXPECT_EQ(stored.at(0).value, "false");
    EXPECT_EQ(stored.at(0).source, OTHER_SOURCE);
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

TEST_F(DeviceModelRegistryTest, writes_to_one_variable_are_serialised_while_the_provider_runs) {
    register_config_view();
    provider.delay = std::chrono::milliseconds(20);

    std::vector<std::thread> writers;
    for (int i = 1; i <= 5; i++) {
        writers.emplace_back([this, i]() {
            EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, std::to_string(i * 10), "csms"),
                      SetVariableStatusEnum::Accepted);
        });
    }
    // Reads do not wait for the provider: the read returns while writers are still queued
    std::string value;
    dm->get_variable(config_component, timeout, AttributeEnum::Actual, value, false, UnsetValue::NotSupported);
    EXPECT_LT(provider.received().size(), 5);

    for (auto& writer : writers) {
        writer.join();
    }
    EXPECT_EQ(provider.max_calls_running.load(), 1);
    const auto received = provider.received();
    ASSERT_EQ(received.size(), 5);
    // The last writer wins
    EXPECT_EQ(get(config_component, timeout), received.back());
}

TEST_F(DeviceModelRegistryTest, unregistering_waits_for_a_write_in_progress) {
    register_config_view();
    provider.delay = std::chrono::milliseconds(50);

    std::thread writer([this]() { dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"); });
    while (provider.calls_running.load() == 0) {
        std::this_thread::yield();
    }
    dm->unregister_components(SOURCE);

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

TEST_F(DeviceModelRegistryTest, listener_may_unregister_the_source_of_the_variable) {
    register_config_view();
    dm->register_variable_listener([this](const std::unordered_map<std::int64_t, VariableMonitoringMeta>&,
                                          const Component&, const Variable&, const VariableCharacteristics&,
                                          const VariableAttribute&, const std::string&,
                                          const std::string&) { dm->unregister_components(SOURCE); });

    EXPECT_EQ(dm->set_value(config_component, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Accepted);
    EXPECT_FALSE(visible(config_component, timeout));
}

TEST_F(DeviceModelRegistryTest, write_in_progress_is_not_applied_to_the_component_config_after_unregistering) {
    const auto& heartbeat = ControllerComponentVariables::HeartbeatInterval;
    const auto component_config_value = get(heartbeat.component, heartbeat.variable.value());
    RegisteredComponent component;
    component.component = heartbeat.component;
    component.variables.push_back(make_registered_variable(heartbeat.variable->name, "60"));
    ASSERT_TRUE(dm->register_components(SOURCE, {component}, &provider).rejected.empty());
    provider.delay = std::chrono::milliseconds(50);

    std::thread writer([&]() {
        EXPECT_EQ(dm->set_value(heartbeat.component, heartbeat.variable.value(), AttributeEnum::Actual, "90", "csms"),
                  SetVariableStatusEnum::Accepted);
    });
    while (provider.calls_running.load() == 0) {
        std::this_thread::yield();
    }
    dm->unregister_components(SOURCE);
    writer.join();

    EXPECT_EQ(provider.received(), std::vector<std::string>{"90"});
    EXPECT_EQ(get(heartbeat.component, heartbeat.variable.value()), component_config_value);
    EXPECT_TRUE(changes.empty());
}

TEST_F(DeviceModelRegistryTest, write_in_progress_is_not_applied_after_the_provider_is_replaced) {
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
    ASSERT_TRUE(dm->register_components(SOURCE, {component}, &replacement).rejected.empty());
    writer.join();

    EXPECT_EQ(provider.received(), std::vector<std::string>{"90"});
    EXPECT_EQ(get(config_component, timeout), "75");
}

} // namespace ocpp::v2
