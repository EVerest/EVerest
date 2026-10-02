// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <device_model_test_helper.hpp>
#include <test_temp_paths.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>

namespace ocpp::v2 {

namespace {

const std::string SOURCE = "EXT:extension";
const std::string OTHER_SOURCE = "EVEREST";

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

    void register_variables(const std::vector<RegisteredVariable>& variables, const std::string& source = SOURCE) {
        RegisteredComponent registered;
        registered.component = component;
        registered.variables = variables;
        ASSERT_TRUE(dm->register_components(source, {registered}, nullptr).rejected.empty());
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
};

} // namespace

TEST_F(DeviceModelMonitorsAcrossBootsTest, monitor_of_an_absent_source_is_dormant) {
    register_variables({make_registered_variable(level.name)});
    const auto id = set_monitor(make_request(component, level, 100));
    EXPECT_EQ(stored_monitor(id)->source, SOURCE);

    reboot();
    // The source has not registered yet: the monitor is invisible for the CSMS
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

TEST_F(DeviceModelMonitorsAcrossBootsTest, monitor_is_removed_when_its_source_registers_without_the_variable) {
    register_variables({make_registered_variable(level.name), make_registered_variable(other.name)});
    const auto id = set_monitor(make_request(component, level, 100));

    reboot();
    register_variables({make_registered_variable(other.name)});
    EXPECT_FALSE(stored_monitor(id).has_value());

    // Removed monitors do not come back
    reboot();
    register_variables({make_registered_variable(level.name)});
    EXPECT_TRUE(monitor_ids(component, level).empty());
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, monitor_of_another_source_stays_dormant) {
    register_variables({make_registered_variable(level.name)}, OTHER_SOURCE);
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
    EXPECT_EQ(dm->notify_value_changed(component, level, AttributeEnum::Actual, "600", SOURCE),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(monitor_values, std::vector<float>{500});
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, monitor_follows_the_variable_to_a_new_owner) {
    const auto& interval = ControllerComponentVariables::AlignedDataInterval;
    const auto id = set_monitor(make_request(interval.component, interval.variable.value(), 950));
    EXPECT_FALSE(stored_monitor(id)->source.has_value());

    RegisteredComponent registered;
    registered.component = interval.component;
    auto variable = make_registered_variable(interval.variable->name, 2000);
    variable.attributes.at(0).value = "900";
    registered.variables.push_back(variable);
    ASSERT_TRUE(dm->register_components(SOURCE, {registered}, nullptr).rejected.empty());

    EXPECT_EQ(monitor_ids(interval.component, interval.variable.value()), std::vector<std::int32_t>{id});
    EXPECT_EQ(stored_monitor(id)->source, SOURCE);

    // Unregistering hands the monitor back to the component config variable
    dm->unregister_components(SOURCE);
    EXPECT_EQ(monitor_ids(interval.component, interval.variable.value()), std::vector<std::int32_t>{id});
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, factory_default_removes_dormant_custom_monitors) {
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
    const auto dormant = set_monitor(make_request(component, level, 100));

    reboot();
    register_variables({make_registered_variable(other.name)}, OTHER_SOURCE);
    const auto fresh = set_monitor(make_request(component, other, 100));
    EXPECT_GT(fresh, dormant);
}

TEST_F(DeviceModelMonitorsAcrossBootsTest, component_config_monitor_moves_to_the_overriding_registration) {
    dm.reset();
    db_path = libocpp_test::unique_temp_path("libocpp_test_monitors_across_boots", ".db");

    // The component config defines the variable with a hardwired monitor; InitDeviceModelDb seeds it at every boot
    const auto boot_with_component_config = [this] {
        dm.reset();
        InitDeviceModelDb db(db_path, MIGRATION_FILES_PATH);
        ComponentKey key;
        key.name = component.name;
        DeviceModelVariable config_variable;
        config_variable.name = level.name;
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
    };

    boot_with_component_config();
    const auto ids = monitor_ids(component, level);
    ASSERT_EQ(ids.size(), 1);
    const auto id = ids.front();

    // The registration lists no monitors of its own: the component config monitor moves along with the variable
    register_variables({make_registered_variable(level.name)});
    EXPECT_EQ(monitor_ids(component, level), std::vector<std::int32_t>{id});
    EXPECT_TRUE(stored_monitor(id).has_value());

    // The next boot seeds the component config again and finds the monitor in place: it keeps its id
    boot_with_component_config();
    register_variables({make_registered_variable(level.name)});
    EXPECT_EQ(monitor_ids(component, level), std::vector<std::int32_t>{id});

    // Back with the component config variable when the owner unregisters
    dm->unregister_components(SOURCE);
    EXPECT_EQ(monitor_ids(component, level), std::vector<std::int32_t>{id});
}

} // namespace ocpp::v2
