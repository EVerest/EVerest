// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <fstream>
#include <gtest/gtest.h>

#include <filesystem>

#include <everest/database/sqlite/connection.hpp>

#include <everest/ocpp_module_common/device_model/everest_device_model.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>

namespace {

namespace dmn = ocpp_module_common::device_model;
namespace em = types::evse_manager;

em::Evse make_evse(const std::int32_t evse_id, const em::ChargeMode mode) {
    em::Evse evse;
    evse.id = evse_id;
    em::Connector connector;
    connector.id = 1;
    connector.charge_mode = mode;
    connector.hlc_capable = true;
    connector.type = em::ConnectorTypeEnum::cType2;
    evse.connectors.push_back(connector);
    return evse;
}

types::evse_board_support::HardwareCapabilities make_hw_capabilities(const std::int32_t phases) {
    types::evse_board_support::HardwareCapabilities capabilities{};
    capabilities.max_current_A_import = 32;
    capabilities.max_phase_count_import = phases;
    return capabilities;
}

std::vector<ocpp::v2::RegisteredComponent> make_station() {
    const std::vector<em::Evse> evses{make_evse(1, em::ChargeMode::DC), make_evse(2, em::ChargeMode::AC)};
    const std::map<std::int32_t, types::evse_board_support::HardwareCapabilities> hw_capabilities{
        {1, make_hw_capabilities(3)}, {2, make_hw_capabilities(1)}};
    return dmn::to_registered_components(
        dmn::build_runtime_component_configs(evses, hw_capabilities, {}, {}, true, {1}, {}));
}

const ocpp::v2::RegisteredVariable* find(const std::vector<ocpp::v2::RegisteredComponent>& components,
                                         const std::string& component_name, const std::int32_t evse_id,
                                         const std::string& variable_name,
                                         const std::optional<std::int32_t>& connector_id = std::nullopt) {
    for (const auto& component : components) {
        if (component.component.name.get() != component_name or not component.component.evse.has_value() or
            component.component.evse->id != evse_id or component.component.evse->connectorId != connector_id) {
            continue;
        }
        for (const auto& variable : component.variables) {
            if (variable.variable.name.get() == variable_name) {
                return &variable;
            }
        }
    }
    return nullptr;
}

std::string value_of(const ocpp::v2::RegisteredVariable* variable) {
    if (variable == nullptr or variable->attributes.empty() or not variable->attributes.front().value.has_value()) {
        return {};
    }
    return variable->attributes.front().value->get();
}

} // namespace

TEST(EverestDeviceModelComponentsTest, components_carry_the_reported_state) {
    const auto components = make_station();

    const auto* supply_phases = find(components, "EVSE", 1, "SupplyPhases");
    ASSERT_NE(supply_phases, nullptr);
    EXPECT_EQ(value_of(supply_phases), "3");
    EXPECT_EQ(value_of(find(components, "EVSE", 2, "SupplyPhases")), "1");
    const auto* power = find(components, "EVSE", 1, "Power");
    ASSERT_NE(power, nullptr);
    EXPECT_EQ(power->characteristics.maxLimit, 32 * 230 * 3);
    EXPECT_EQ(value_of(find(components, "Connector", 1, "ConnectorType", 1)), "cType2");
    // Only the wired EVSE gets a DER controller
    EXPECT_NE(find(components, "DCDERCtrlr", 1, "Enabled"), nullptr);
    EXPECT_EQ(find(components, "ACDERCtrlr", 2, "Enabled"), nullptr);
}

TEST(EverestDeviceModelComponentsTest, variables_support_monitoring_and_der_enabled_is_persistent) {
    for (const auto& component : make_station()) {
        for (const auto& variable : component.variables) {
            EXPECT_TRUE(variable.characteristics.supportsMonitoring)
                << component.component.name.get() << "." << variable.variable.name.get();
            const bool der_enabled =
                component.component.name.get() == "DCDERCtrlr" and variable.variable.name.get() == "Enabled";
            for (const auto& attribute : variable.attributes) {
                EXPECT_EQ(attribute.persistent.value_or(false), der_enabled)
                    << component.component.name.get() << "." << variable.variable.name.get();
            }
        }
    }
}

class EverestDeviceModelDatabaseTest : public ::testing::Test {
protected:
    const std::filesystem::path db_path =
        std::filesystem::temp_directory_path() / "ocpp_module_common_everest_device_model_test.db";

    void SetUp() override {
        std::filesystem::remove(db_path);
        ocpp::v2::InitDeviceModelDb init(db_path, DEVICE_MODEL_MIGRATIONS_DIR);
        init.initialize_database({}, true, false);
        init.close_connection();
    }

    void TearDown() override {
        std::filesystem::remove(db_path);
    }

    /// \brief A 2026.10 EVerest device model database with a DER controller whose Enabled the CSMS set
    static void create_legacy_everest_database(const std::filesystem::path& path) {
        ocpp::v2::ComponentKey der_key;
        der_key.name = "DCDERCtrlr";
        der_key.evse_id = 1;
        ocpp::v2::DeviceModelVariable enabled;
        enabled.name = "Enabled";
        enabled.characteristics.dataType = ocpp::v2::DataEnum::boolean;
        enabled.characteristics.supportsMonitoring = false;
        ocpp::v2::DbVariableAttribute attribute;
        attribute.variable_attribute.type = ocpp::v2::AttributeEnum::Actual;
        attribute.variable_attribute.mutability = ocpp::v2::MutabilityEnum::ReadWrite;
        attribute.variable_attribute.value = "false";
        enabled.attributes.push_back(attribute);
        ocpp::v2::InitDeviceModelDb init(path, DEVICE_MODEL_MIGRATIONS_DIR);
        init.initialize_database({{der_key, {enabled}}}, true, false);
        init.close_connection();
        everest::db::sqlite::Connection connection(path);
        ASSERT_TRUE(connection.open_connection());
        ASSERT_TRUE(connection.execute_statement("UPDATE VARIABLE_ATTRIBUTE SET VALUE_SOURCE = 'csms'"));
    }

    void set_stored_values_writable(const bool writable) {
        everest::db::sqlite::Connection connection(db_path);
        ASSERT_TRUE(connection.open_connection());
        ASSERT_TRUE(connection.execute_statement(
            writable ? "DROP TRIGGER reject_stored_values"
                     : "CREATE TRIGGER reject_stored_values BEFORE INSERT ON VARIABLE_ATTRIBUTE_VALUE BEGIN "
                       "SELECT RAISE(ABORT, 'disk full'); END"));
    }
};

TEST_F(EverestDeviceModelDatabaseTest, csms_set_der_enabled_is_imported_once) {
    const auto der =
        ocpp::v2::DERComponentVariables::get_dc_component_variable(1, ocpp::v2::DERComponentVariables::Enabled);
    ocpp::v2::StoredVariableValue legacy{
        der.component, der.variable.value(), ocpp::v2::AttributeEnum::Actual, "false", "csms", "EVEREST"};
    ocpp::v2::DeviceModelStorageSqlite storage(db_path);

    dmn::import_stored_values({legacy}, storage);
    auto stored = storage.get_stored_values();
    ASSERT_EQ(stored.size(), 1);
    EXPECT_EQ(stored.at(0).value, "false");
    EXPECT_EQ(stored.at(0).source, "EVEREST");

    // A value stored since then wins over the legacy value
    stored.at(0).value = "true";
    ASSERT_EQ(storage.set_stored_value(stored.at(0)), ocpp::v2::SetVariableStatusEnum::Accepted);
    dmn::import_stored_values({legacy}, storage);
    EXPECT_EQ(storage.get_stored_values().at(0).value, "true");
}

TEST_F(EverestDeviceModelDatabaseTest, registered_der_enabled_restores_the_imported_value) {
    const auto der =
        ocpp::v2::DERComponentVariables::get_dc_component_variable(1, ocpp::v2::DERComponentVariables::Enabled);
    {
        ocpp::v2::DeviceModelStorageSqlite storage(db_path);
        dmn::import_stored_values(
            {{der.component, der.variable.value(), ocpp::v2::AttributeEnum::Actual, "false", "csms", "EVEREST"}},
            storage);
    }

    ocpp::v2::DeviceModel device_model(std::make_unique<ocpp::v2::DeviceModelStorageSqlite>(db_path));
    ASSERT_TRUE(device_model.register_components("EVEREST", make_station(), nullptr).rejected.empty());
    std::string value;
    EXPECT_EQ(device_model.get_variable(der.component, der.variable.value(), ocpp::v2::AttributeEnum::Actual, value,
                                        false, ocpp::v2::UnsetValue::NotSupported),
              ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_EQ(value, "false");
}

TEST_F(EverestDeviceModelDatabaseTest, monitor_on_evse_power_fires_on_reported_power) {
    ocpp::v2::DeviceModel device_model(std::make_unique<ocpp::v2::DeviceModelStorageSqlite>(db_path));
    ASSERT_TRUE(device_model.register_components("EVEREST", make_station(), nullptr).rejected.empty());

    ocpp::v2::Component evse;
    evse.name = "EVSE";
    evse.evse = ocpp::v2::EVSE{};
    evse.evse->id = 1;
    ocpp::v2::SetMonitoringData request;
    request.component = evse;
    request.variable = ocpp::v2::EvseComponentVariables::Power;
    request.type = ocpp::v2::MonitorEnum::UpperThreshold;
    request.value = 7000;
    request.severity = 3;
    const auto results = device_model.set_monitors({request});
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results.at(0).status, ocpp::v2::SetMonitoringStatusEnum::Accepted);

    std::vector<std::string> evaluated;
    device_model.register_variable_listener(
        [&evaluated](const std::unordered_map<std::int64_t, ocpp::v2::VariableMonitoringMeta>& monitors,
                     const ocpp::v2::Component&, const ocpp::v2::Variable&, const ocpp::v2::VariableCharacteristics&,
                     const ocpp::v2::VariableAttribute&, const std::string&, const std::string& value_current) {
            if (not monitors.empty()) {
                evaluated.push_back(value_current);
            }
        });
    EXPECT_EQ(device_model.notify_value_changed(evse, ocpp::v2::EvseComponentVariables::Power,
                                                ocpp::v2::AttributeEnum::Actual, "7360.000000", "EVEREST"),
              ocpp::v2::SetVariableStatusEnum::Accepted);
    EXPECT_EQ(evaluated, std::vector<std::string>{"7360.000000"});
}

TEST_F(EverestDeviceModelDatabaseTest, everest_device_model_database_is_taken_over_once) {
    const auto everest_db_path =
        std::filesystem::temp_directory_path() / "ocpp_module_common_everest_device_model_legacy.db";
    auto migrated_path = everest_db_path;
    migrated_path += ".migrated";
    std::filesystem::remove(everest_db_path);
    std::filesystem::remove(migrated_path);

    create_legacy_everest_database(everest_db_path);

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    dmn::migrate_everest_device_model_database(everest_db_path, db_path, storage);
    const auto stored = storage.get_stored_values();
    ASSERT_EQ(stored.size(), 1);
    EXPECT_EQ(stored.at(0).component.name.get(), "DCDERCtrlr");
    EXPECT_EQ(stored.at(0).value, "false");
    EXPECT_EQ(stored.at(0).value_source, "csms");
    EXPECT_FALSE(std::filesystem::exists(everest_db_path));
    EXPECT_TRUE(std::filesystem::exists(migrated_path));

    // An interrupted first start repeats the import: values stored meanwhile are kept
    auto changed = stored.at(0);
    changed.value = "true";
    ASSERT_EQ(storage.set_stored_value(changed), ocpp::v2::SetVariableStatusEnum::Accepted);
    std::filesystem::rename(migrated_path, everest_db_path);
    dmn::migrate_everest_device_model_database(everest_db_path, db_path, storage);
    EXPECT_EQ(storage.get_stored_values().at(0).value, "true");

    // Nothing to take over any more
    dmn::migrate_everest_device_model_database(everest_db_path, db_path, storage);
    EXPECT_EQ(storage.get_stored_values().size(), 1);
    std::filesystem::remove(migrated_path);
}

TEST_F(EverestDeviceModelDatabaseTest, everest_device_model_database_is_kept_until_its_values_are_imported) {
    const auto everest_db_path =
        std::filesystem::temp_directory_path() / "ocpp_module_common_everest_device_model_not_imported.db";
    auto migrated_path = everest_db_path;
    migrated_path += ".migrated";
    std::filesystem::remove(everest_db_path);
    std::filesystem::remove(migrated_path);
    create_legacy_everest_database(everest_db_path);

    set_stored_values_writable(false);
    {
        ocpp::v2::DeviceModelStorageSqlite storage(db_path);
        dmn::migrate_everest_device_model_database(everest_db_path, db_path, storage);
        EXPECT_TRUE(storage.get_stored_values().empty());
    }
    EXPECT_TRUE(std::filesystem::exists(everest_db_path));
    EXPECT_FALSE(std::filesystem::exists(migrated_path));

    set_stored_values_writable(true);
    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    dmn::migrate_everest_device_model_database(everest_db_path, db_path, storage);
    EXPECT_EQ(storage.get_stored_values().size(), 1);
    EXPECT_FALSE(std::filesystem::exists(everest_db_path));
    EXPECT_TRUE(std::filesystem::exists(migrated_path));
    std::filesystem::remove(migrated_path);
}

TEST_F(EverestDeviceModelDatabaseTest, unreadable_everest_device_model_database_is_renamed_without_import) {
    const auto everest_db_path =
        std::filesystem::temp_directory_path() / "ocpp_module_common_everest_device_model_empty.db";
    auto migrated_path = everest_db_path;
    migrated_path += ".migrated";
    std::filesystem::remove(migrated_path);
    // A file without any tables, as a truncated database
    std::ofstream(everest_db_path).close();

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    dmn::migrate_everest_device_model_database(everest_db_path, db_path, storage);
    EXPECT_TRUE(storage.get_stored_values().empty());
    EXPECT_FALSE(std::filesystem::exists(everest_db_path));
    EXPECT_TRUE(std::filesystem::exists(migrated_path));
    std::filesystem::remove(migrated_path);
}

TEST_F(EverestDeviceModelDatabaseTest, everest_device_model_database_path_that_is_no_file_is_left_alone) {
    const auto directory = std::filesystem::temp_directory_path() / "ocpp_module_common_everest_device_model_dir";
    std::filesystem::create_directories(directory);

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    dmn::migrate_everest_device_model_database(directory, db_path, storage);
    EXPECT_TRUE(std::filesystem::is_directory(directory));
    std::filesystem::remove_all(directory);
}

TEST_F(EverestDeviceModelDatabaseTest, device_model_database_is_not_taken_over) {
    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    dmn::migrate_everest_device_model_database(db_path, db_path, storage);
    EXPECT_TRUE(std::filesystem::exists(db_path));
}
