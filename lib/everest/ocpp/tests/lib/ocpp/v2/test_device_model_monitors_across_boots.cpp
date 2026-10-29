// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <algorithm>

#include <device_model_test_helper.hpp>
#include <test_temp_paths.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>

namespace ocpp::v2 {

namespace {

const std::string OWNER = "EXT:extension";
const std::string OTHER_OWNER = "EVEREST";

Component make_component(const std::string& name) {
    Component component;
    component.name = name;
    return component;
}

Variable make_variable(const std::string& name) {
    Variable variable;
    variable.name = name;
    return variable;
}

RegisteredVariable make_registered_variable(const std::string& name, const float max_limit = 1000,
                                            const MutabilityEnum mutability = MutabilityEnum::ReadWrite) {
    RegisteredVariable registered;
    registered.variable = make_variable(name);
    registered.characteristics.dataType = DataEnum::integer;
    registered.characteristics.supportsMonitoring = true;
    registered.characteristics.maxLimit = max_limit;
    VariableAttribute attribute;
    attribute.mutability = mutability;
    attribute.value = "10";
    registered.attributes.push_back(attribute);
    return registered;
}

VariableMonitoringMeta make_preconfigured_monitor(const MonitorEnum type, const float value,
                                                  const std::int32_t severity) {
    VariableMonitoringMeta meta;
    meta.monitor.type = type;
    meta.monitor.value = value;
    meta.monitor.severity = severity;
    meta.monitor.transaction = false;
    meta.type = VariableMonitorType::PreconfiguredMonitor;
    return meta;
}

SetMonitoringData make_request(const Component& component, const Variable& variable, const float value,
                               const std::int32_t severity = 3, const MonitorEnum type = MonitorEnum::UpperThreshold) {
    SetMonitoringData request;
    request.component = component;
    request.variable = variable;
    request.type = type;
    request.value = value;
    request.severity = severity;
    return request;
}

class DeviceModelMonitorsAcrossBootsTest : public ::testing::Test {
protected:
    // Keeps the in-memory database alive while device models come and go, like the database file across boots
    DeviceModelTestHelper device_model_test_helper;
    // A test that seeds the component config at every boot needs a file, since InitDeviceModelDb treats a database
    // it cannot find on disk as new
    std::filesystem::path db_path = DEVICE_MODEL_DB_IN_MEMORY_PATH;
    std::unique_ptr<DeviceModel> dm = boot();
    const Component component = make_component("ExtensionCtrlr");
    const Variable level = make_variable("Level");
    const Variable other = make_variable("Other");

    void TearDown() override {
        dm.reset();
        if (std::filesystem::is_regular_file(db_path)) {
            std::filesystem::remove(db_path);
        }
    }

    std::unique_ptr<DeviceModel> boot() {
        return std::make_unique<DeviceModel>(std::make_unique<DeviceModelStorageSqlite>(db_path));
    }

    void reboot() {
        dm.reset();
        dm = boot();
    }

    void register_variables(const std::vector<RegisteredVariable>& variables, const std::string& owner = OWNER) {
        RegisteredComponent registered;
        registered.component = component;
        registered.variables = variables;
        ASSERT_TRUE(dm->register_components(owner, {registered}, nullptr).rejected.empty());
    }

    std::int32_t set_monitor(const SetMonitoringData& request) {
        const auto results = dm->set_monitors({request});
        EXPECT_EQ(results.size(), 1);
        EXPECT_EQ(results.at(0).status, SetMonitoringStatusEnum::Accepted);
        return results.at(0).id.value_or(-1);
    }

    std::vector<std::int32_t> monitor_ids(const Component& c, const Variable& v) {
        std::vector<std::int32_t> ids;
        for (const auto& data : dm->get_monitors({}, {{c, v}})) {
            for (const auto& monitor : data.variableMonitoring) {
                ids.push_back(monitor.id);
            }
        }
        return ids;
    }

    std::vector<StoredMonitor> stored_monitors() {
        DeviceModelStorageSqlite storage(db_path);
        return storage.get_all_monitors();
    }

    std::optional<StoredMonitor> stored_monitor(const std::int32_t id) {
        for (auto& stored : stored_monitors()) {
            if (stored.monitor.monitor.id == id) {
                return stored;
            }
        }
        return std::nullopt;
    }

    /// Boots with a component config of one variable, Level or Other, that has a hardwired monitor. InitDeviceModelDb
    /// seeds the component config at every boot.
    void boot_with_component_config(const bool with_level) {
        dm.reset();
        if (db_path == DEVICE_MODEL_DB_IN_MEMORY_PATH) {
            db_path = libocpp_test::unique_temp_path("libocpp_test_monitors_across_boots", ".db");
        }
        InitDeviceModelDb db(db_path, MIGRATION_FILES_PATH);
        ComponentKey key;
        key.name = component.name;
        DeviceModelVariable config_variable;
        config_variable.name = with_level ? level.name : other.name;
        config_variable.characteristics.dataType = DataEnum::integer;
        config_variable.characteristics.supportsMonitoring = true;
        config_variable.characteristics.maxLimit = 1000;
        DbVariableAttribute attribute;
        attribute.variable_attribute.mutability = MutabilityEnum::ReadWrite;
        attribute.variable_attribute.type = AttributeEnum::Actual;
        attribute.variable_attribute.value = "10";
        config_variable.attributes = {attribute};
        auto hardwired = make_preconfigured_monitor(MonitorEnum::UpperThreshold, 100, 3);
        hardwired.type = VariableMonitorType::HardWiredMonitor;
        config_variable.monitors = {hardwired};
        db.initialize_database({{key, {config_variable}}}, false);
        dm = boot();
    }
};

} // namespace

TEST_F(DeviceModelMonitorsAcrossBootsTest, monitor_is_invisible_until_its_owner_registers_the_variable) {
    register_variables({make_registered_variable(level.name)});
    const auto id = set_monitor(make_request(component, level, 100));

    reboot();
    // The owner has not registered yet: the monitor is invisible for the CSMS
    EXPECT_TRUE(monitor_ids(component, level).empty());
    auto with_id = make_request(component, level, 200);
    with_id.id = id;
    EXPECT_EQ(dm->set_monitors({with_id}).at(0).status, SetMonitoringStatusEnum::Rejected); // N04.FR.13
    EXPECT_EQ(dm->clear_monitors({id}).at(0).status, ClearMonitoringStatusEnum::NotFound);  // N06.FR.02
    EXPECT_TRUE(stored_monitor(id).has_value());

    // The variable comes back: the monitor is active again with its id
    register_variables({make_registered_variable(level.name)});
    EXPECT_EQ(monitor_ids(component, level), std::vector<std::int32_t>{id});
    EXPECT_EQ(dm->get_variable_meta_data(component, level)->monitors.at(id).monitor.value, 100);
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, monitor_is_removed_at_start_when_its_owner_registers_without_the_variable) {
    register_variables({make_registered_variable(level.name), make_registered_variable(other.name)});
    const auto id = set_monitor(make_request(component, level, 100));

    reboot();
    register_variables({make_registered_variable(other.name)});
    EXPECT_TRUE(stored_monitor(id).has_value());
    dm->remove_monitors_of_absent_variables();
    EXPECT_FALSE(stored_monitor(id).has_value());

    // Removed monitors do not come back
    reboot();
    register_variables({make_registered_variable(level.name)});
    EXPECT_TRUE(monitor_ids(component, level).empty());
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, monitor_of_an_absent_variable_is_removed_at_start) {
    register_variables({make_registered_variable(level.name), make_registered_variable(other.name)});
    const auto absent = set_monitor(make_request(component, level, 100));
    const auto present = set_monitor(make_request(component, other, 100));

    reboot();
    register_variables({make_registered_variable(other.name)}, OTHER_OWNER);
    dm->remove_monitors_of_absent_variables();

    EXPECT_FALSE(stored_monitor(absent).has_value());
    EXPECT_TRUE(stored_monitor(present).has_value());
    EXPECT_EQ(monitor_ids(component, other), std::vector<std::int32_t>{present});
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, monitor_of_a_variable_of_another_owner_is_kept) {
    register_variables({make_registered_variable(level.name)}, OTHER_OWNER);
    const auto id = set_monitor(make_request(component, level, 100));

    reboot();
    register_variables({make_registered_variable(other.name)});
    EXPECT_TRUE(stored_monitor(id).has_value());
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, monitor_that_no_longer_fits_is_removed) {
    register_variables({make_registered_variable(level.name)});
    const auto threshold = set_monitor(make_request(component, level, 500, 3));
    const auto periodic = set_monitor(make_request(component, level, 60, 4, MonitorEnum::Periodic));

    // A threshold needs a numeric variable; a periodic monitor does not
    reboot();
    auto text = make_registered_variable(level.name);
    text.characteristics.dataType = DataEnum::string;
    register_variables({text});
    EXPECT_FALSE(stored_monitor(threshold).has_value());
    EXPECT_EQ(monitor_ids(component, level), std::vector<std::int32_t>{periodic});

    reboot();
    auto unmonitored = make_registered_variable(level.name);
    unmonitored.characteristics.supportsMonitoring = false;
    register_variables({unmonitored});
    EXPECT_FALSE(stored_monitor(periodic).has_value());
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, threshold_monitor_outside_the_new_limits_is_kept) {
    register_variables({make_registered_variable(level.name)});
    const auto threshold = set_monitor(make_request(component, level, 500));

    reboot();
    register_variables({make_registered_variable(level.name, 100)});
    EXPECT_TRUE(stored_monitor(threshold).has_value());
    EXPECT_EQ(monitor_ids(component, level), std::vector<std::int32_t>{threshold});

    // The owner reports a value above the threshold: the listeners see the monitor with it
    std::vector<float> monitor_values;
    dm->register_variable_listener([&monitor_values](
                                       const std::unordered_map<std::int64_t, VariableMonitoringMeta>& monitors,
                                       const Component&, const Variable&, const VariableCharacteristics&,
                                       const VariableAttribute&, const std::string&, const std::string& value_current) {
        for (const auto& [id, meta] : monitors) {
            if (meta.monitor.type == MonitorEnum::UpperThreshold and std::stof(value_current) > meta.monitor.value) {
                monitor_values.push_back(meta.monitor.value);
            }
        }
    });
    EXPECT_EQ(dm->notify_value_changed(component, level, AttributeEnum::Actual, "600", OWNER, std::nullopt),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(monitor_values, std::vector<float>{500});
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, monitor_follows_the_variable_to_a_new_owner) {
    register_variables({make_registered_variable(level.name)}, OTHER_OWNER);
    const auto id = set_monitor(make_request(component, level, 950));

    dm->unregister_components(OTHER_OWNER);
    EXPECT_TRUE(monitor_ids(component, level).empty());

    register_variables({make_registered_variable(level.name, 2000)});
    EXPECT_EQ(monitor_ids(component, level), std::vector<std::int32_t>{id});
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, factory_default_removes_custom_monitors_of_unregistered_variables) {
    register_variables({make_registered_variable(level.name)});
    const auto id = set_monitor(make_request(component, level, 100));

    reboot();
    dm->clear_custom_monitors(); // N03 FactoryDefault and HardWiredOnly
    EXPECT_FALSE(stored_monitor(id).has_value());
    register_variables({make_registered_variable(level.name)});
    EXPECT_TRUE(monitor_ids(component, level).empty());
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, preconfigured_monitor_is_matched_by_type_and_severity) {
    auto variable = make_registered_variable(level.name);
    variable.monitors.push_back(make_preconfigured_monitor(MonitorEnum::UpperThreshold, 100, 3));
    register_variables({variable});
    const auto ids = monitor_ids(component, level);
    ASSERT_EQ(ids.size(), 1);
    const auto id = ids.at(0);
    EXPECT_EQ(dm->get_variable_meta_data(component, level)->monitors.at(id).type,
              VariableMonitorType::PreconfiguredMonitor);

    // The CSMS changes the preconfigured monitor
    auto change = make_request(component, level, 200);
    change.id = id;
    EXPECT_EQ(dm->set_monitors({change}, VariableMonitorType::PreconfiguredMonitor).at(0).status,
              SetMonitoringStatusEnum::Accepted);

    reboot();
    register_variables({variable});
    EXPECT_EQ(monitor_ids(component, level), std::vector<std::int32_t>{id});
    EXPECT_EQ(dm->get_variable_meta_data(component, level)->monitors.at(id).monitor.value, 200);

    // A registration without it removes it
    reboot();
    register_variables({make_registered_variable(level.name)});
    EXPECT_TRUE(monitor_ids(component, level).empty());
    EXPECT_FALSE(stored_monitor(id).has_value());
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, monitor_on_write_only_variable_is_rejected) {
    register_variables({make_registered_variable(level.name, 1000, MutabilityEnum::WriteOnly)});
    EXPECT_EQ(dm->set_monitors({make_request(component, level, 100)}).at(0).status, SetMonitoringStatusEnum::Rejected);
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, ids_are_not_reused) {
    register_variables({make_registered_variable(level.name), make_registered_variable(other.name)});
    const auto earlier = set_monitor(make_request(component, level, 100));

    reboot();
    register_variables({make_registered_variable(other.name)}, OTHER_OWNER);
    const auto later = set_monitor(make_request(component, other, 100));
    EXPECT_GT(later, earlier);
}

TEST_F(DeviceModelMonitorsAcrossBootsTest,
       monitors_of_a_variable_removed_from_the_component_config_are_removed_at_start) {
    boot_with_component_config(true);
    const auto custom_id = set_monitor(make_request(component, level, 500, 5));
    ASSERT_EQ(stored_monitors().size(), 2);

    // The new firmware leaves the variable out of the component config and no owner registers it
    boot_with_component_config(false);
    ASSERT_EQ(stored_monitors().size(), 3);
    dm->remove_monitors_of_absent_variables();

    EXPECT_FALSE(stored_monitor(custom_id).has_value());
    for (const auto& stored : stored_monitors()) {
        EXPECT_EQ(stored.variable, other);
    }
    EXPECT_EQ(monitor_ids(component, other).size(), 1);
}

TEST_F(DeviceModelMonitorsAcrossBootsTest,
       monitors_move_with_the_variable_from_the_component_config_to_a_registration) {
    boot_with_component_config(true);
    const auto custom_id = set_monitor(make_request(component, level, 500, 5));
    ASSERT_EQ(monitor_ids(component, level).size(), 2);

    // The new firmware leaves the variable out of the component config and registers it
    boot_with_component_config(false);
    EXPECT_TRUE(monitor_ids(component, level).empty());
    auto registered = make_registered_variable(level.name);
    registered.monitors = {make_preconfigured_monitor(MonitorEnum::LowerThreshold, 5, 4)};
    register_variables({registered});

    // The custom monitor keeps its id; the hardwired monitor of the component config is not listed by the
    // registration and is replaced by the one the registration lists
    auto ids = monitor_ids(component, level);
    std::sort(ids.begin(), ids.end());
    ASSERT_EQ(ids.size(), 2);
    EXPECT_NE(std::find(ids.begin(), ids.end(), custom_id), ids.end());

    // The registration's monitors keep their ids at the next boot
    boot_with_component_config(false);
    register_variables({registered});
    auto ids_after_boot = monitor_ids(component, level);
    std::sort(ids_after_boot.begin(), ids_after_boot.end());
    EXPECT_EQ(ids_after_boot, ids);
}

} // namespace ocpp::v2
