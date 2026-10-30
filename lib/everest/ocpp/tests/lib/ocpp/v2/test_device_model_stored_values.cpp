// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <device_model_storage_interface_mock.hpp>
#include <device_model_test_helper.hpp>

#include <everest/database/exceptions.hpp>

#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_composition.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>

namespace ocpp::v2 {

namespace {

const std::string OWNER = "EVEREST";

Component make_der_component(const std::int32_t evse_id) {
    Component component;
    component.name = "ACDERCtrlr";
    EVSE evse;
    evse.id = evse_id;
    component.evse = evse;
    return component;
}

Variable make_variable(const std::string& name) {
    Variable variable;
    variable.name = name;
    return variable;
}

RegisteredVariable make_enabled(const DataEnum data_type = DataEnum::boolean) {
    RegisteredVariable registered;
    registered.variable = make_variable("Enabled");
    registered.characteristics.dataType = data_type;
    registered.characteristics.supportsMonitoring = true;
    VariableAttribute attribute;
    attribute.mutability = MutabilityEnum::ReadWrite;
    attribute.persistent = true;
    attribute.value = "true";
    registered.attributes.push_back(attribute);
    return registered;
}

RegisteredVariable make_limit(const float max_limit) {
    RegisteredVariable registered;
    registered.variable = make_variable("Limit");
    registered.characteristics.dataType = DataEnum::integer;
    registered.characteristics.supportsMonitoring = true;
    registered.characteristics.maxLimit = max_limit;
    VariableAttribute attribute;
    attribute.mutability = MutabilityEnum::ReadWrite;
    attribute.persistent = true;
    attribute.value = "10";
    registered.attributes.push_back(attribute);
    return registered;
}

RegisteredVariable make_vendor_available() {
    RegisteredVariable registered;
    registered.variable = make_variable("VendorAvailable");
    registered.characteristics.dataType = DataEnum::boolean;
    registered.characteristics.supportsMonitoring = true;
    VariableAttribute attribute;
    attribute.mutability = MutabilityEnum::ReadOnly;
    attribute.value = "true";
    registered.attributes.push_back(attribute);
    return registered;
}

class NoOpProvider : public VariableValueProvider {
public:
    SetVariableStatusEnum set_value(const Component&, const Variable&, AttributeEnum, const std::string&,
                                    const std::string&, WriteId) override {
        return SetVariableStatusEnum::Accepted;
    }
};

class DeviceModelStoredValuesTest : public ::testing::Test {
protected:
    DeviceModelTestHelper device_model_test_helper;
    std::unique_ptr<DeviceModel> dm = boot();
    const Component der = make_der_component(1);
    const Variable enabled = make_variable("Enabled");

    static std::unique_ptr<DeviceModel> boot() {
        return std::make_unique<DeviceModel>(
            std::make_unique<DeviceModelStorageSqlite>(DEVICE_MODEL_DB_IN_MEMORY_PATH));
    }

    void reboot() {
        dm.reset();
        dm = boot();
    }

    ComponentRegistrationResult register_der(const std::vector<RegisteredVariable>& variables,
                                             VariableValueProvider* provider = nullptr,
                                             const std::string& owner = OWNER) {
        RegisteredComponent component;
        component.component = der;
        component.variables = variables;
        return dm->register_components(owner, {component}, provider);
    }

    std::string get(const Variable& variable) {
        std::string value;
        EXPECT_EQ(dm->get_variable(der, variable, AttributeEnum::Actual, value, false, UnsetValue::NotSupported),
                  GetVariableStatusEnum::Accepted);
        return value;
    }

    std::vector<StoredVariableValue> stored_values() {
        DeviceModelStorageSqlite storage(DEVICE_MODEL_DB_IN_MEMORY_PATH);
        return storage.get_stored_values();
    }
};

} // namespace

TEST_F(DeviceModelStoredValuesTest, persistent_value_survives_a_restart) {
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    ASSERT_EQ(dm->set_value(der, enabled, AttributeEnum::Actual, "false", "csms"), SetVariableStatusEnum::Accepted);

    const auto stored = stored_values();
    ASSERT_EQ(stored.size(), 1);
    EXPECT_EQ(stored.at(0).value, "false");
    EXPECT_EQ(stored.at(0).value_source, "csms");
    EXPECT_EQ(stored.at(0).owner, OWNER);

    reboot();
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    EXPECT_EQ(get(enabled), "false");
}

TEST_F(DeviceModelStoredValuesTest, value_reported_by_the_owner_is_stored) {
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    ASSERT_EQ(dm->notify_value_changed(der, enabled, AttributeEnum::Actual, "false", OWNER, std::nullopt),
              SetVariableStatusEnum::Accepted);

    reboot();
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    EXPECT_EQ(get(enabled), "false");
}

TEST_F(DeviceModelStoredValuesTest, stored_value_that_no_longer_fits_is_discarded) {
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    ASSERT_EQ(dm->set_value(der, enabled, AttributeEnum::Actual, "false", "csms"), SetVariableStatusEnum::Accepted);

    reboot();
    ASSERT_TRUE(register_der({make_enabled(DataEnum::integer)}).rejected.empty());
    EXPECT_EQ(get(enabled), "true");
    EXPECT_TRUE(stored_values().empty());
}

TEST_F(DeviceModelStoredValuesTest, stored_value_outside_the_new_limits_is_restored) {
    const auto limit = make_variable("Limit");
    ASSERT_TRUE(register_der({make_limit(1000)}).rejected.empty());
    ASSERT_EQ(dm->set_value(der, limit, AttributeEnum::Actual, "800", "csms"), SetVariableStatusEnum::Accepted);

    reboot();
    ASSERT_TRUE(register_der({make_limit(100)}).rejected.empty());
    EXPECT_EQ(get(limit), "800");
    EXPECT_EQ(stored_values().size(), 1);
}

TEST_F(DeviceModelStoredValuesTest, stored_value_follows_the_variable_to_a_new_owner) {
    const std::string new_owner = "EXT:extension";
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    ASSERT_EQ(dm->set_value(der, enabled, AttributeEnum::Actual, "false", "csms"), SetVariableStatusEnum::Accepted);

    reboot();
    ASSERT_TRUE(register_der({make_enabled()}, nullptr, new_owner).rejected.empty());
    EXPECT_EQ(get(enabled), "false");
    const auto stored = stored_values();
    ASSERT_EQ(stored.size(), 1);
    EXPECT_EQ(stored.at(0).value, "false");
    EXPECT_EQ(stored.at(0).owner, new_owner);
}

TEST_F(DeviceModelStoredValuesTest, stored_value_is_kept_while_its_owner_is_absent) {
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    ASSERT_EQ(dm->set_value(der, enabled, AttributeEnum::Actual, "false", "csms"), SetVariableStatusEnum::Accepted);

    reboot();
    EXPECT_EQ(stored_values().size(), 1);
    reboot();
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    EXPECT_EQ(get(enabled), "false");
}

TEST_F(DeviceModelStoredValuesTest, stored_value_of_an_unregistered_variable_is_removed_at_start) {
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    ASSERT_EQ(dm->set_value(der, enabled, AttributeEnum::Actual, "false", "csms"), SetVariableStatusEnum::Accepted);

    reboot();
    dm->remove_monitors_and_values_of_absent_variables();
    EXPECT_TRUE(stored_values().empty());
}

TEST_F(DeviceModelStoredValuesTest, stored_value_of_a_registered_variable_is_kept_at_start) {
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    ASSERT_EQ(dm->set_value(der, enabled, AttributeEnum::Actual, "false", "csms"), SetVariableStatusEnum::Accepted);

    reboot();
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    dm->remove_monitors_and_values_of_absent_variables();
    EXPECT_EQ(stored_values().size(), 1);
    EXPECT_EQ(get(enabled), "false");
}

TEST_F(DeviceModelStoredValuesTest, stored_value_is_removed_at_start_when_its_owner_registers_without_the_variable) {
    ASSERT_TRUE(register_der({make_enabled(), make_vendor_available()}).rejected.empty());
    ASSERT_EQ(dm->set_value(der, enabled, AttributeEnum::Actual, "false", "csms"), SetVariableStatusEnum::Accepted);

    reboot();
    ASSERT_TRUE(register_der({make_vendor_available()}).rejected.empty());
    EXPECT_EQ(stored_values().size(), 1);
    dm->remove_monitors_and_values_of_absent_variables();
    EXPECT_TRUE(stored_values().empty());
}

TEST_F(DeviceModelStoredValuesTest,
       stored_value_is_removed_at_start_when_the_variable_is_registered_without_attributes) {
    ASSERT_TRUE(register_der({make_enabled(), make_vendor_available()}).rejected.empty());
    ASSERT_EQ(dm->set_value(der, enabled, AttributeEnum::Actual, "false", "csms"), SetVariableStatusEnum::Accepted);

    reboot();
    auto without_attributes = make_enabled();
    without_attributes.attributes.clear();
    const auto result = register_der({without_attributes, make_vendor_available()});
    ASSERT_EQ(result.rejected.size(), 1);
    EXPECT_EQ(result.rejected.at(0).reason, RegistrationRejectionReason::NoAttributes);
    dm->remove_monitors_and_values_of_absent_variables();
    EXPECT_TRUE(stored_values().empty());
}

TEST_F(DeviceModelStoredValuesTest, stored_value_is_kept_when_the_variable_belongs_to_another_owner) {
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    ASSERT_EQ(dm->set_value(der, enabled, AttributeEnum::Actual, "false", "csms"), SetVariableStatusEnum::Accepted);

    // Another owner with a provider takes the variable: the stored value is kept, still recorded under OWNER
    reboot();
    NoOpProvider provider;
    ASSERT_TRUE(register_der({make_enabled()}, &provider, "EXT:other").rejected.empty());
    ASSERT_EQ(register_der({make_enabled()}).rejected.size(), 1);
    dm->remove_monitors_and_values_of_absent_variables();
    EXPECT_EQ(stored_values().size(), 1);

    reboot();
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    EXPECT_EQ(get(enabled), "false");
}

TEST_F(DeviceModelStoredValuesTest, stored_value_is_not_applied_to_a_variable_with_a_provider) {
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    ASSERT_EQ(dm->set_value(der, enabled, AttributeEnum::Actual, "false", "csms"), SetVariableStatusEnum::Accepted);

    // The provider keeps the value: the stored value is not applied, but kept
    reboot();
    NoOpProvider provider;
    ASSERT_TRUE(register_der({make_enabled()}, &provider).rejected.empty());
    EXPECT_EQ(get(enabled), "true");
    EXPECT_EQ(stored_values().size(), 1);

    reboot();
    ASSERT_TRUE(register_der({make_enabled()}).rejected.empty());
    EXPECT_EQ(get(enabled), "false");
}

TEST_F(DeviceModelStoredValuesTest, delta_monitor_is_kept_when_its_value_is_restored) {
    auto without_initial_value = make_enabled();
    without_initial_value.attributes.at(0).value.reset();
    ASSERT_TRUE(register_der({without_initial_value}).rejected.empty());
    ASSERT_EQ(dm->set_value(der, enabled, AttributeEnum::Actual, "false", "csms"), SetVariableStatusEnum::Accepted);

    SetMonitoringData request;
    request.component = der;
    request.variable = enabled;
    request.type = MonitorEnum::Delta;
    request.value = 1;
    request.severity = 5;
    const auto results = dm->set_monitors({request});
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results.at(0).status, SetMonitoringStatusEnum::Accepted);
    const auto id = results.at(0).id.value();

    // Without the restored value the Delta monitor would not fit the variable and would be removed
    reboot();
    ASSERT_TRUE(register_der({without_initial_value}).rejected.empty());
    const auto monitors = dm->get_monitors({}, {{der, enabled}});
    ASSERT_EQ(monitors.size(), 1);
    ASSERT_EQ(monitors.at(0).variableMonitoring.size(), 1);
    EXPECT_EQ(monitors.at(0).variableMonitoring.at(0).id, id);
}

class StoredValuesStorageMock : public DeviceModelStorageMock {
public:
    MOCK_METHOD(std::vector<StoredVariableValue>, get_stored_values, (), (override));
    MOCK_METHOD(bool, remove_stored_value, (const Component&, const Variable&, const AttributeEnum&), (override));
};

TEST(StoredValuesTest, value_stays_when_the_storage_cannot_remove_it) {
    StoredVariableValue stored;
    stored.component = make_der_component(1);
    stored.variable = make_variable("Enabled");
    stored.attribute = AttributeEnum::Actual;
    stored.value = "false";
    stored.owner = OWNER;
    StoredValuesStorageMock storage;
    EXPECT_CALL(storage, get_stored_values()).WillOnce(::testing::Return(std::vector<StoredVariableValue>{stored}));
    // Every call but the last finds the value still in memory
    EXPECT_CALL(storage, remove_stored_value(stored.component, stored.variable, AttributeEnum::Actual))
        .WillOnce(::testing::Return(false))
        .WillOnce(::testing::Throw(everest::db::Exception("database is locked")))
        .WillOnce(::testing::Return(true));
    StoredValues values(storage);
    const auto unregistered = [](const Component&, const Variable&) { return false; };

    for (int i = 0; i < 4; ++i) {
        values.remove_of_unregistered_variables(unregistered);
    }
}

} // namespace ocpp::v2
