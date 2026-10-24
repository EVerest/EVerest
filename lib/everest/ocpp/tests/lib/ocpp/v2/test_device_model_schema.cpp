// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <device_model_test_helper.hpp>
#include <everest/database/sqlite/connection.hpp>
#include <everest/database/sqlite/schema_updater.hpp>
#include <test_temp_paths.hpp>

#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>

namespace ocpp {
namespace v2 {

namespace {

constexpr std::uint32_t SCHEMA_WITH_FOREIGN_KEY_MONITORS = 3;
constexpr std::uint32_t SCHEMA_WITH_NAME_KEYED_MONITORS = 4;

/// \brief A device model database of OCPP201 or OCPPmulti (2026.02, 2026.10): the OCPP side seeded from the component
/// config, including variables that overlap with what EVerest provisions, with custom and preconfigured monitors.
/// Monitor 3 was deleted, so the id sequence is ahead of the highest id.
constexpr auto OCPP_DATABASE_SCHEMA_3 = R"(
INSERT INTO COMPONENT (ID, NAME, INSTANCE, EVSE_ID, CONNECTOR_ID) VALUES
  (1, 'OCPPCommCtrlr', NULL, NULL, NULL),
  (2, 'EVSE', NULL, 1, NULL),
  (3, 'Connector', NULL, 1, 1),
  (4, 'NetworkConfiguration', '1', NULL, NULL);
INSERT INTO VARIABLE (ID, NAME, INSTANCE, COMPONENT_ID, SOURCE) VALUES
  (1, 'HeartbeatInterval', NULL, 1, NULL),
  (2, 'Power', NULL, 2, NULL),
  (3, 'AvailabilityState', NULL, 3, 'OCPP'),
  (4, 'MessageTimeout', 'Default', 4, NULL);
INSERT INTO VARIABLE_CHARACTERISTICS (DATATYPE_ID, VARIABLE_ID, SUPPORTS_MONITORING) VALUES
  (2, 1, 1), (1, 2, 1), (5, 3, 1), (2, 4, 1);
INSERT INTO VARIABLE_ATTRIBUTE (VARIABLE_ID, MUTABILITY_ID, PERSISTENT, CONSTANT, TYPE_ID, VALUE_SOURCE, VALUE) VALUES
  (1, 2, 1, 0, 0, 'csms', '300'),
  (2, 0, 1, 0, 0, 'default', '0'),
  (3, 0, 1, 0, 0, 'default', 'Available'),
  (4, 2, 1, 0, 0, 'default', '30');
INSERT INTO VARIABLE_MONITORING (ID, VARIABLE_ID, SEVERITY, "TRANSACTION", TYPE_ID, CONFIG_TYPE_ID, VALUE,
                                 REFERENCE_VALUE) VALUES
  (1, 2, 5, 0, 0, 2, 7000, NULL),
  (2, 1, 7, 0, 2, 1, 10, '300'),
  (3, 3, 4, 0, 2, 2, 0, 'Available'),
  (4, 4, 8, 1, 1, 2, 5, NULL);
DELETE FROM VARIABLE_MONITORING WHERE ID = 3;
)";

/// \brief The EVerest device model database of OCPP201 or OCPPmulti: provisioned by EVerest, no monitors.
constexpr auto EVEREST_DATABASE_SCHEMA_3 = R"(
INSERT INTO COMPONENT (ID, NAME, INSTANCE, EVSE_ID, CONNECTOR_ID) VALUES (1, 'EVSE', NULL, 1, NULL),
  (2, 'DCDERCtrlr', NULL, 1, NULL);
INSERT INTO VARIABLE (ID, NAME, INSTANCE, COMPONENT_ID, SOURCE) VALUES (1, 'Power', NULL, 1, 'EVEREST'),
  (2, 'Enabled', NULL, 2, 'EVEREST');
INSERT INTO VARIABLE_CHARACTERISTICS (DATATYPE_ID, VARIABLE_ID, SUPPORTS_MONITORING) VALUES (1, 1, 0), (4, 2, 1);
INSERT INTO VARIABLE_ATTRIBUTE (VARIABLE_ID, MUTABILITY_ID, PERSISTENT, CONSTANT, TYPE_ID, VALUE_SOURCE, VALUE) VALUES
  (1, 0, 1, 0, 0, 'EVEREST', '7360'),
  (2, 2, 1, 0, 0, 'csms', 'false');
)";

/// \brief A device model database of the OCPP 1.6 module (2026.10): only what the component config defines.
constexpr auto OCPP16_DATABASE_SCHEMA_3 = R"(
INSERT INTO COMPONENT (ID, NAME, INSTANCE, EVSE_ID, CONNECTOR_ID) VALUES (1, 'OCPP16LegacyCtrlr', NULL, NULL, NULL);
INSERT INTO VARIABLE (ID, NAME, INSTANCE, COMPONENT_ID, SOURCE) VALUES (1, 'HeartbeatInterval', NULL, 1, NULL);
INSERT INTO VARIABLE_CHARACTERISTICS (DATATYPE_ID, VARIABLE_ID, SUPPORTS_MONITORING) VALUES (2, 1, 1);
INSERT INTO VARIABLE_ATTRIBUTE (VARIABLE_ID, MUTABILITY_ID, PERSISTENT, CONSTANT, TYPE_ID, VALUE_SOURCE, VALUE) VALUES
  (1, 2, 1, 0, 0, 'default', '86400');
)";

struct MonitorRow {
    int id;
    std::string component_name;
    std::string component_instance;
    int evse_id;
    int connector_id;
    std::string variable_name;
    std::string variable_instance;
    std::optional<std::string> owner;
    int severity;
    int type_id;
    int config_type_id;
    std::optional<std::string> reference_value;

    bool operator==(const MonitorRow& other) const {
        return std::tie(id, component_name, component_instance, evse_id, connector_id, variable_name, variable_instance,
                        owner, severity, type_id, config_type_id, reference_value) ==
               std::tie(other.id, other.component_name, other.component_instance, other.evse_id, other.connector_id,
                        other.variable_name, other.variable_instance, other.owner, other.severity, other.type_id,
                        other.config_type_id, other.reference_value);
    }
};

Component make_component(const std::string& name, std::optional<std::int32_t> evse_id = std::nullopt,
                         std::optional<std::int32_t> connector_id = std::nullopt,
                         std::optional<std::string> instance = std::nullopt) {
    Component component;
    component.name = name;
    if (instance.has_value()) {
        component.instance = instance.value();
    }
    if (evse_id.has_value()) {
        EVSE evse;
        evse.id = evse_id.value();
        evse.connectorId = connector_id;
        component.evse = evse;
    }
    return component;
}

Variable make_variable(const std::string& name, std::optional<std::string> instance = std::nullopt) {
    Variable variable;
    variable.name = name;
    if (instance.has_value()) {
        variable.instance = instance.value();
    }
    return variable;
}

SetMonitoringData make_monitor_request(const Component& component, const Variable& variable, MonitorEnum type,
                                       float value, int severity) {
    SetMonitoringData request;
    request.component = component;
    request.variable = variable;
    request.type = type;
    request.value = value;
    request.severity = severity;
    return request;
}

} // namespace

class DeviceModelSchemaMigrationTest : public ::testing::Test {
protected:
    const std::filesystem::path db_path =
        libocpp_test::unique_temp_path("libocpp_test_device_model_schema_migration", ".db");
    std::unique_ptr<everest::db::sqlite::Connection> database;

    void SetUp() override {
        std::filesystem::remove(db_path);
        database = std::make_unique<everest::db::sqlite::Connection>(db_path);
        ASSERT_TRUE(database->open_connection());
    }

    void TearDown() override {
        database.reset();
        std::filesystem::remove(db_path);
    }

    void migrate_to(std::uint32_t version) {
        everest::db::sqlite::SchemaUpdater updater{database.get()};
        ASSERT_TRUE(updater.apply_migration_files(MIGRATION_FILES_PATH, version));
        ASSERT_EQ(database->get_user_version(), version);
    }

    void create_schema_3_database(const char* content) {
        migrate_to(SCHEMA_WITH_FOREIGN_KEY_MONITORS);
        ASSERT_TRUE(database->execute_statement(content));
    }

    std::vector<MonitorRow> monitor_rows() {
        auto statement = database->new_statement(
            "SELECT ID, COMPONENT_NAME, COMPONENT_INSTANCE, EVSE_ID, CONNECTOR_ID, VARIABLE_NAME, VARIABLE_INSTANCE, "
            "OWNER, SEVERITY, TYPE_ID, CONFIG_TYPE_ID, REFERENCE_VALUE FROM VARIABLE_MONITORING ORDER BY ID");
        std::vector<MonitorRow> rows;
        while (statement->step() == SQLITE_ROW) {
            rows.push_back({statement->column_int(0), statement->column_text(1), statement->column_text(2),
                            statement->column_int(3), statement->column_int(4), statement->column_text(5),
                            statement->column_text(6), statement->column_text_nullable(7), statement->column_int(8),
                            statement->column_int(9), statement->column_int(10), statement->column_text_nullable(11)});
        }
        return rows;
    }

    std::optional<int> monitor_sequence() {
        auto statement = database->new_statement("SELECT seq FROM sqlite_sequence WHERE name = 'VARIABLE_MONITORING'");
        if (statement->step() != SQLITE_ROW) {
            return std::nullopt;
        }
        return statement->column_int(0);
    }

    bool table_exists(const std::string& table) {
        auto statement = database->new_statement("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?");
        statement->bind_text(1, table, everest::db::sqlite::SQLiteString::Transient);
        return statement->step() == SQLITE_ROW;
    }

    bool column_exists(const std::string& table, const std::string& column) {
        auto statement = database->new_statement("SELECT 1 FROM pragma_table_info(?) WHERE name = ?");
        statement->bind_text(1, table, everest::db::sqlite::SQLiteString::Transient);
        statement->bind_text(2, column, everest::db::sqlite::SQLiteString::Transient);
        return statement->step() == SQLITE_ROW;
    }
};

TEST_F(DeviceModelSchemaMigrationTest, ocpp_database_monitors_keep_their_ids_and_get_a_name_key) {
    create_schema_3_database(OCPP_DATABASE_SCHEMA_3);
    ASSERT_EQ(monitor_sequence(), 4);

    migrate_to(SCHEMA_WITH_NAME_KEYED_MONITORS);

    const std::vector<MonitorRow> expected{
        {1, "EVSE", "", 1, 0, "Power", "", std::nullopt, 5, 0, 2, std::nullopt},
        {2, "OCPPCommCtrlr", "", 0, 0, "HeartbeatInterval", "", std::nullopt, 7, 2, 1, "300"},
        {4, "NetworkConfiguration", "1", 0, 0, "MessageTimeout", "Default", std::nullopt, 8, 1, 2, std::nullopt},
    };
    EXPECT_EQ(monitor_rows(), expected);
    EXPECT_EQ(monitor_sequence(), 4);
    EXPECT_FALSE(column_exists("VARIABLE_MONITORING", "VARIABLE_ID"));
    EXPECT_TRUE(table_exists("VARIABLE_ATTRIBUTE_VALUE"));
    EXPECT_FALSE(column_exists("VARIABLE", "SOURCE"));

    // Ids of deleted monitors are not handed out again.
    DeviceModelStorageSqlite storage(db_path);
    const auto monitor = storage.set_monitoring_data(
        make_monitor_request(make_component("EVSE", 1), make_variable("Power"), MonitorEnum::LowerThreshold, 10, 3),
        VariableMonitorType::CustomMonitor);
    ASSERT_TRUE(monitor.has_value());
    EXPECT_EQ(monitor->monitor.id, 5);
}

TEST_F(DeviceModelSchemaMigrationTest, migrated_monitors_have_no_owner) {
    create_schema_3_database(OCPP_DATABASE_SCHEMA_3);
    ASSERT_TRUE(database->execute_statement(
        "INSERT INTO VARIABLE_MONITORING (ID, VARIABLE_ID, SEVERITY, \"TRANSACTION\", TYPE_ID, CONFIG_TYPE_ID, VALUE) "
        "VALUES (5, 3, 4, 0, 0, 2, 1)"));

    migrate_to(SCHEMA_WITH_NAME_KEYED_MONITORS);

    const auto rows = monitor_rows();
    ASSERT_EQ(rows.size(), 4);
    EXPECT_EQ(rows.back().id, 5);
    EXPECT_EQ(rows.back().component_name, "Connector");
    EXPECT_EQ(rows.back().evse_id, 1);
    EXPECT_EQ(rows.back().connector_id, 1);
    EXPECT_EQ(rows.back().owner, std::nullopt);
}

TEST_F(DeviceModelSchemaMigrationTest, everest_database_migrates_without_monitors) {
    create_schema_3_database(EVEREST_DATABASE_SCHEMA_3);

    migrate_to(SCHEMA_WITH_NAME_KEYED_MONITORS);

    EXPECT_TRUE(monitor_rows().empty());
    EXPECT_EQ(monitor_sequence(), std::nullopt);
    DeviceModelStorageSqlite storage(db_path);
    EXPECT_EQ(
        storage
            .get_variable_attribute(make_component("DCDERCtrlr", 1), make_variable("Enabled"), AttributeEnum::Actual)
            ->value,
        "false");
    EXPECT_TRUE(storage.get_stored_values().empty());
}

TEST_F(DeviceModelSchemaMigrationTest, ocpp16_database_migrates) {
    create_schema_3_database(OCPP16_DATABASE_SCHEMA_3);

    migrate_to(SCHEMA_WITH_NAME_KEYED_MONITORS);

    EXPECT_TRUE(monitor_rows().empty());
    DeviceModelStorageSqlite storage(db_path);
    const auto model = storage.get_device_model();
    ASSERT_EQ(model.size(), 1);
    EXPECT_EQ(storage
                  .get_variable_attribute(make_component("OCPP16LegacyCtrlr"), make_variable("HeartbeatInterval"),
                                          AttributeEnum::Actual)
                  ->value,
              "86400");
}

TEST_F(DeviceModelSchemaMigrationTest, migrating_a_second_time_changes_nothing) {
    create_schema_3_database(OCPP_DATABASE_SCHEMA_3);
    migrate_to(SCHEMA_WITH_NAME_KEYED_MONITORS);
    const auto rows = monitor_rows();
    const auto sequence = monitor_sequence();

    migrate_to(SCHEMA_WITH_NAME_KEYED_MONITORS);
    // Opening the database the way the modules do runs the migrations as well.
    DeviceModelStorageSqlite storage(db_path, MIGRATION_FILES_PATH);

    EXPECT_EQ(monitor_rows(), rows);
    EXPECT_EQ(monitor_sequence(), sequence);
}

TEST_F(DeviceModelSchemaMigrationTest, down_migration_restores_foreign_key_monitors) {
    create_schema_3_database(OCPP_DATABASE_SCHEMA_3);
    migrate_to(SCHEMA_WITH_NAME_KEYED_MONITORS);
    {
        // A monitor of a variable that has no row cannot be represented in schema 3.
        DeviceModelStorageSqlite storage(db_path);
        ASSERT_TRUE(storage
                        .set_monitoring_data(make_monitor_request(make_component("EVSE", 2), make_variable("Power"),
                                                                  MonitorEnum::UpperThreshold, 1, 1),
                                             VariableMonitorType::CustomMonitor)
                        .has_value());
    }

    migrate_to(SCHEMA_WITH_FOREIGN_KEY_MONITORS);

    EXPECT_FALSE(table_exists("VARIABLE_ATTRIBUTE_VALUE"));
    EXPECT_TRUE(column_exists("VARIABLE", "SOURCE"));
    auto statement = database->new_statement("SELECT ID, VARIABLE_ID FROM VARIABLE_MONITORING ORDER BY ID");
    std::vector<std::pair<int, int>> monitors;
    while (statement->step() == SQLITE_ROW) {
        monitors.emplace_back(statement->column_int(0), statement->column_int(1));
    }
    const std::vector<std::pair<int, int>> expected{{1, 2}, {2, 1}, {4, 4}};
    EXPECT_EQ(monitors, expected);
    EXPECT_EQ(monitor_sequence(), 5);
}

class DeviceModelStorageSqliteNameKeyTest : public ::testing::Test {
protected:
    DeviceModelTestHelper device_model_test_helper;
    DeviceModelStorageSqlite storage{DEVICE_MODEL_DB_IN_MEMORY_PATH};

    const Component heartbeat_component = make_component("OCPPCommCtrlr");
    const Variable heartbeat_variable = make_variable("HeartbeatInterval");
    const Component runtime_component = make_component("EVSE", 9);
    const Variable runtime_variable = make_variable("Power");

    StoredVariableValue make_stored_value(const std::string& value, const std::string& value_source,
                                          const std::optional<std::string>& owner) const {
        return {runtime_component, runtime_variable, AttributeEnum::Actual, value, value_source, owner};
    }
};

TEST_F(DeviceModelStorageSqliteNameKeyTest, monitor_on_a_variable_without_row) {
    const auto monitor = storage.set_monitoring_data(
        make_monitor_request(runtime_component, runtime_variable, MonitorEnum::UpperThreshold, 7000, 5),
        VariableMonitorType::CustomMonitor);
    ASSERT_TRUE(monitor.has_value());

    const auto all_monitors = storage.get_all_monitors();
    const auto it = std::find_if(all_monitors.begin(), all_monitors.end(),
                                 [&](const StoredMonitor& m) { return m.monitor.monitor.id == monitor->monitor.id; });
    ASSERT_NE(it, all_monitors.end());
    EXPECT_EQ(it->component, runtime_component);
    EXPECT_EQ(it->variable, runtime_variable);
    EXPECT_EQ(it->monitor.monitor.value, 7000);
    EXPECT_FALSE(it->owner.has_value());

    EXPECT_EQ(storage.clear_variable_monitor(monitor->monitor.id, false), ClearMonitoringStatusEnum::Accepted);
    EXPECT_TRUE(storage.get_all_monitors().empty());
}

TEST_F(DeviceModelStorageSqliteNameKeyTest, monitor_of_an_unknown_variable_is_stored_but_not_attached) {
    const Variable misspelled = make_variable("HeartbeatIntervall");
    const auto monitor = storage.set_monitoring_data(
        make_monitor_request(heartbeat_component, misspelled, MonitorEnum::UpperThreshold, 100, 5),
        VariableMonitorType::CustomMonitor);
    ASSERT_TRUE(monitor.has_value());

    const auto all_monitors = storage.get_all_monitors();
    ASSERT_EQ(all_monitors.size(), 1);
    EXPECT_EQ(all_monitors.at(0).variable, misspelled);
    EXPECT_FALSE(all_monitors.at(0).owner.has_value());

    const auto model = storage.get_device_model();
    EXPECT_EQ(model.at(heartbeat_component).count(misspelled), 0);
    EXPECT_TRUE(model.at(heartbeat_component).at(heartbeat_variable).monitors.empty());
}

TEST_F(DeviceModelStorageSqliteNameKeyTest, device_model_attaches_monitors_by_name_key) {
    const auto attached = storage.set_monitoring_data(
        make_monitor_request(heartbeat_component, heartbeat_variable, MonitorEnum::UpperThreshold, 100, 5),
        VariableMonitorType::CustomMonitor);
    const auto detached = storage.set_monitoring_data(
        make_monitor_request(runtime_component, runtime_variable, MonitorEnum::UpperThreshold, 7000, 5),
        VariableMonitorType::CustomMonitor);
    ASSERT_TRUE(attached.has_value());
    ASSERT_TRUE(detached.has_value());

    const auto model = storage.get_device_model();

    const auto& monitors = model.at(heartbeat_component).at(heartbeat_variable).monitors;
    ASSERT_EQ(monitors.count(attached->monitor.id), 1);
    EXPECT_EQ(monitors.at(attached->monitor.id).monitor.value, 100);
    EXPECT_EQ(model.count(runtime_component), 0);
}

TEST_F(DeviceModelStorageSqliteNameKeyTest, value_of_a_variable_without_row_is_stored_by_name_key) {
    EXPECT_EQ(storage.set_stored_value(make_stored_value("1", "csms", "OWNER_A")), SetVariableStatusEnum::Accepted);
    EXPECT_EQ(storage.set_stored_value(make_stored_value("2", "internal", "OWNER_B")), SetVariableStatusEnum::Accepted);

    const auto values = storage.get_stored_values();
    ASSERT_EQ(values.size(), 1);
    EXPECT_EQ(values.at(0).component, runtime_component);
    EXPECT_EQ(values.at(0).variable, runtime_variable);
    EXPECT_EQ(values.at(0).attribute, AttributeEnum::Actual);
    EXPECT_EQ(values.at(0).value, "2");
    EXPECT_EQ(values.at(0).value_source, "internal");
    EXPECT_EQ(values.at(0).owner, "OWNER_B");

    // The component tables are not touched.
    EXPECT_FALSE(storage.get_variable_attribute(runtime_component, runtime_variable, AttributeEnum::Actual));

    EXPECT_TRUE(storage.remove_stored_value(runtime_component, runtime_variable, AttributeEnum::Actual));
    EXPECT_FALSE(storage.remove_stored_value(runtime_component, runtime_variable, AttributeEnum::Actual));
    EXPECT_TRUE(storage.get_stored_values().empty());
}

TEST_F(DeviceModelStorageSqliteNameKeyTest, attribute_value_of_a_variable_without_row_is_rejected) {
    EXPECT_EQ(
        storage.set_variable_attribute_value(runtime_component, runtime_variable, AttributeEnum::Actual, "1", "csms"),
        SetVariableStatusEnum::Rejected);
    EXPECT_TRUE(storage.get_stored_values().empty());
}

TEST_F(DeviceModelStorageSqliteNameKeyTest, value_of_a_variable_with_row_is_not_stored_by_name_key) {
    EXPECT_EQ(storage.set_variable_attribute_value(heartbeat_component, heartbeat_variable, AttributeEnum::Actual,
                                                   "123", "csms"),
              SetVariableStatusEnum::Accepted);

    EXPECT_TRUE(storage.get_stored_values().empty());
    EXPECT_EQ(storage.get_variable_attribute(heartbeat_component, heartbeat_variable, AttributeEnum::Actual)->value,
              "123");
}

TEST_F(DeviceModelStorageSqliteNameKeyTest, delta_monitor_without_value_has_no_reference) {
    const auto monitor = storage.set_monitoring_data(
        make_monitor_request(runtime_component, runtime_variable, MonitorEnum::Delta, 10, 5),
        VariableMonitorType::CustomMonitor);
    ASSERT_TRUE(monitor.has_value());
    EXPECT_FALSE(monitor->reference_value.has_value());

    ASSERT_TRUE(storage.update_monitoring_reference(monitor->monitor.id, "50"));
    const auto all_monitors = storage.get_all_monitors();
    ASSERT_EQ(all_monitors.size(), 1);
    EXPECT_EQ(all_monitors.at(0).monitor.reference_value, "50");
}

TEST_F(DeviceModelStorageSqliteNameKeyTest, delta_monitor_without_row_takes_the_stored_value_as_reference) {
    const auto request = make_monitor_request(runtime_component, runtime_variable, MonitorEnum::Delta, 10, 5);
    ASSERT_EQ(storage.set_stored_value(make_stored_value("50", "internal", std::nullopt)),
              SetVariableStatusEnum::Accepted);
    const auto monitor = storage.set_monitoring_data(request, VariableMonitorType::CustomMonitor);
    ASSERT_TRUE(monitor.has_value());
    EXPECT_EQ(monitor->reference_value, "50");
}

class InitDeviceModelDbMonitorTest : public ::testing::Test {
protected:
    const std::filesystem::path db_path =
        libocpp_test::unique_temp_path("libocpp_test_init_device_model_db_monitors", ".db");
    const ComponentKey heartbeat_component_key{std::nullopt, "OCPPCommCtrlr", std::nullopt, std::nullopt, std::nullopt};

    void SetUp() override {
        std::filesystem::remove(db_path);
    }

    void TearDown() override {
        std::filesystem::remove(db_path);
    }

    static VariableMonitoringMeta preconfigured_monitor() {
        VariableMonitoringMeta monitor;
        monitor.monitor.type = MonitorEnum::UpperThreshold;
        monitor.monitor.severity = 3;
        monitor.monitor.value = 1000;
        monitor.monitor.transaction = false;
        monitor.type = VariableMonitorType::PreconfiguredMonitor;
        return monitor;
    }

    void initialize(const std::map<ComponentKey, std::vector<DeviceModelVariable>>& configs) {
        InitDeviceModelDb init(db_path, MIGRATION_FILES_PATH);
        init.initialize_database(configs, false);
    }

    std::map<ComponentKey, std::vector<DeviceModelVariable>> configs_with_heartbeat_monitor() {
        auto configs = get_all_component_configs(CONFIG_PATH);
        auto& variables = configs.at(heartbeat_component_key);
        const auto it = std::find_if(variables.begin(), variables.end(),
                                     [](const DeviceModelVariable& v) { return v.name == "HeartbeatInterval"; });
        it->monitors.push_back(preconfigured_monitor());
        return configs;
    }
};

TEST_F(InitDeviceModelDbMonitorTest, preconfigured_monitor_is_seeded_by_name_key) {
    const auto configs = configs_with_heartbeat_monitor();
    initialize(configs);

    std::vector<StoredMonitor> monitors;
    {
        DeviceModelStorageSqlite storage(db_path);
        monitors = storage.get_all_monitors();
    }
    ASSERT_EQ(monitors.size(), 1);
    EXPECT_EQ(monitors.at(0).component, make_component("OCPPCommCtrlr"));
    EXPECT_EQ(monitors.at(0).variable, make_variable("HeartbeatInterval"));
    EXPECT_EQ(monitors.at(0).monitor.type, VariableMonitorType::PreconfiguredMonitor);

    // Seeding again with the same config keeps the monitor and its id.
    initialize(configs);
    DeviceModelStorageSqlite storage(db_path);
    const auto monitors_after = storage.get_all_monitors();
    ASSERT_EQ(monitors_after.size(), 1);
    EXPECT_EQ(monitors_after.at(0).monitor.monitor.id, monitors.at(0).monitor.monitor.id);
}

TEST_F(InitDeviceModelDbMonitorTest, removing_a_variable_removes_its_monitors_without_owner) {
    auto configs = configs_with_heartbeat_monitor();
    initialize(configs);
    {
        DeviceModelStorageSqlite storage(db_path);
        ASSERT_TRUE(storage
                        .set_monitoring_data(make_monitor_request(make_component("OCPPCommCtrlr"),
                                                                  make_variable("HeartbeatInterval"),
                                                                  MonitorEnum::LowerThreshold, 10, 4),
                                             VariableMonitorType::CustomMonitor)
                        .has_value());
    }
    {
        everest::db::sqlite::Connection database(db_path);
        ASSERT_TRUE(database.open_connection());
        ASSERT_TRUE(database.execute_statement(
            "INSERT INTO VARIABLE_MONITORING (COMPONENT_NAME, COMPONENT_INSTANCE, EVSE_ID, CONNECTOR_ID, "
            "VARIABLE_NAME, VARIABLE_INSTANCE, OWNER, SEVERITY, \"TRANSACTION\", TYPE_ID, CONFIG_TYPE_ID, VALUE) "
            "VALUES ('OCPPCommCtrlr', '', 0, 0, 'HeartbeatInterval', '', 'RUNTIME_OWNER', 4, 0, 0, 2, 20)"));
    }

    auto& variables = configs.at(heartbeat_component_key);
    variables.erase(std::remove_if(variables.begin(), variables.end(),
                                   [](const DeviceModelVariable& v) { return v.name == "HeartbeatInterval"; }),
                    variables.end());
    initialize(configs);

    DeviceModelStorageSqlite storage(db_path);
    const auto monitors = storage.get_all_monitors();
    ASSERT_EQ(monitors.size(), 1);
    EXPECT_EQ(monitors.at(0).owner, "RUNTIME_OWNER");
}

namespace {
nlohmann::json name_key_component_config() {
    return {{"name", "NameKeyCtrlr"},
            {"type", "object"},
            {"properties",
             {{"Value",
               {{"variable_name", "Value"},
                {"characteristics", {{"supportsMonitoring", true}, {"dataType", "integer"}}},
                {"attributes", {{{"type", "Actual"}, {"mutability", "ReadWrite"}, {"value", 1}}}},
                {"type", "integer"}}}}}};
}
} // namespace

/// Monitors and stored values key an absent instance as '' and an absent id as 0, so the component config must not
/// use these
TEST(ComponentConfigNameKeyTest, empty_instances_and_id_zero_are_rejected) {
    EXPECT_NO_THROW(parse_component_config_from_string(name_key_component_config().dump()));

    auto empty_component_instance = name_key_component_config();
    empty_component_instance["instance"] = "";
    auto evse_id_zero = name_key_component_config();
    evse_id_zero["evse_id"] = 0;
    auto connector_id_zero = name_key_component_config();
    connector_id_zero["evse_id"] = 1;
    connector_id_zero["connector_id"] = 0;
    auto empty_variable_instance = name_key_component_config();
    empty_variable_instance["properties"]["Value"]["instance"] = "";

    for (const auto& config : {empty_component_instance, evse_id_zero, connector_id_zero, empty_variable_instance}) {
        EXPECT_THROW(parse_component_config_from_string(config.dump()), ComponentConfigNameKeyError) << config.dump();
    }
}

/// A skipped custom config would be replaced by the standardized config of the same component, whose initialization
/// removes the variables only the custom config defines, together with their monitors
TEST(ComponentConfigNameKeyTest, custom_config_with_invalid_name_key_is_not_skipped) {
    const auto directory = libocpp_test::unique_temp_directory("libocpp_test_component_config_name_key");
    std::filesystem::create_directories(directory / "standardized");
    std::filesystem::create_directories(directory / "custom");
    auto custom = name_key_component_config();
    custom["properties"]["Custom"] = custom["properties"]["Value"];
    custom["properties"]["Custom"]["variable_name"] = "Custom";
    custom["properties"]["Custom"]["instance"] = "";
    std::ofstream(directory / "standardized" / "NameKeyCtrlr.json") << name_key_component_config().dump();
    std::ofstream(directory / "custom" / "NameKeyCtrlr.json") << custom.dump();

    EXPECT_THROW(get_all_component_configs(directory), ComponentConfigNameKeyError);

    std::filesystem::remove_all(directory);
}

} // namespace v2
} // namespace ocpp
