// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/database/sqlite/statement.hpp>
#include <everest/logging.hpp>
#include <limits>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>
#include <ocpp/v2/network_configuration_default_schema.hpp>
#include <ocpp/v2/utils.hpp>

using namespace everest::db;
using namespace everest::db::sqlite;

namespace ocpp {

using namespace common;

namespace v2 {

namespace {

/// \brief Reads a Component and a Variable from the six columns NAME, EVSE_ID, CONNECTOR_ID, INSTANCE, NAME and
/// INSTANCE of the component tables starting at \p first_column
std::pair<Component, Variable> read_component_variable(StatementInterface& statement, int first_column) {
    Component component;
    component.name = statement.column_text(first_column);
    if (statement.column_type(first_column + 1) != SQLITE_NULL) {
        EVSE evse;
        evse.id = statement.column_int(first_column + 1);
        if (statement.column_type(first_column + 2) != SQLITE_NULL) {
            evse.connectorId = statement.column_int(first_column + 2);
        }
        component.evse = evse;
    }
    if (statement.column_type(first_column + 3) != SQLITE_NULL) {
        component.instance = statement.column_text(first_column + 3);
    }

    Variable variable;
    variable.name = statement.column_text(first_column + 4);
    if (statement.column_type(first_column + 5) != SQLITE_NULL) {
        variable.instance = statement.column_text(first_column + 5);
    }
    return {component, variable};
}

VariableMetaData* find_variable(DeviceModelMap& device_model, const Component& component, const Variable& variable) {
    const auto component_it = device_model.find(component);
    if (component_it == device_model.end()) {
        return nullptr;
    }
    const auto variable_it = component_it->second.find(variable);
    if (variable_it == component_it->second.end()) {
        return nullptr;
    }
    return &variable_it->second;
}

constexpr auto NAME_KEY_CONDITION =
    "COMPONENT_NAME = @component_name AND COMPONENT_INSTANCE = @component_instance AND EVSE_ID = @evse_id AND "
    "CONNECTOR_ID = @connector_id AND VARIABLE_NAME = @variable_name AND VARIABLE_INSTANCE = @variable_instance";

constexpr auto MONITOR_COLUMNS = "TYPE_ID, ID, SEVERITY, \"TRANSACTION\", VALUE, CONFIG_TYPE_ID, REFERENCE_VALUE";

/// \brief Binds the name key of \p component and \p variable. Absent instances and ids are stored as '' and 0,
/// because SQLite treats NULLs as distinct in keys.
void bind_name_key(StatementInterface& statement, const Component& component, const Variable& variable) {
    statement.bind_text("@component_name", component.name.get(), SQLiteString::Transient);
    statement.bind_text("@component_instance", component.instance.has_value() ? component.instance->get() : "",
                        SQLiteString::Transient);
    statement.bind_int("@evse_id", component.evse.has_value() ? component.evse->id : 0);
    statement.bind_int("@connector_id", component.evse.has_value() ? component.evse->connectorId.value_or(0) : 0);
    statement.bind_text("@variable_name", variable.name.get(), SQLiteString::Transient);
    statement.bind_text("@variable_instance", variable.instance.has_value() ? variable.instance->get() : "",
                        SQLiteString::Transient);
}

/// \brief Reads a name key from the six columns starting at \p first_column
std::pair<Component, Variable> read_name_key(StatementInterface& statement, int first_column) {
    Component component;
    component.name = statement.column_text(first_column);
    if (const auto instance = statement.column_text(first_column + 1); !instance.empty()) {
        component.instance = instance;
    }
    if (const auto evse_id = statement.column_int(first_column + 2); evse_id != 0) {
        EVSE evse;
        evse.id = evse_id;
        if (const auto connector_id = statement.column_int(first_column + 3); connector_id != 0) {
            evse.connectorId = connector_id;
        }
        component.evse = evse;
    }

    Variable variable;
    variable.name = statement.column_text(first_column + 4);
    if (const auto instance = statement.column_text(first_column + 5); !instance.empty()) {
        variable.instance = instance;
    }
    return {component, variable};
}

/// \brief Reads a VariableAttribute from the five columns VALUE, MUTABILITY_ID, PERSISTENT, CONSTANT and TYPE_ID
/// starting at \p first_column
VariableAttribute read_attribute(StatementInterface& statement, int first_column) {
    VariableAttribute attribute;
    if (statement.column_type(first_column) != SQLITE_NULL) {
        attribute.value = statement.column_text(first_column);
    }
    attribute.mutability = static_cast<MutabilityEnum>(statement.column_int(first_column + 1));
    attribute.persistent = static_cast<bool>(statement.column_int(first_column + 2));
    attribute.constant = static_cast<bool>(statement.column_int(first_column + 3));
    attribute.type = static_cast<AttributeEnum>(statement.column_int(first_column + 4));
    return attribute;
}

/// \brief Reads a monitor from the MONITOR_COLUMNS starting at \p first_column
VariableMonitoringMeta read_monitor(StatementInterface& statement, int first_column) {
    VariableMonitoring monitor;
    monitor.type = static_cast<MonitorEnum>(statement.column_int(first_column));
    monitor.id = statement.column_int(first_column + 1);
    monitor.severity = statement.column_int(first_column + 2);
    monitor.transaction = static_cast<bool>(statement.column_int(first_column + 3));
    monitor.value = static_cast<float>(statement.column_double(first_column + 4));

    const auto type = static_cast<VariableMonitorType>(statement.column_int(first_column + 5));
    // this is a workaround to set the eventNotificationType which became a required property for the
    // VariableMonitoringType in OCPP2.1
    monitor.eventNotificationType = conversions::variable_monitor_type_to_event_notification_type(type);

    VariableMonitoringMeta monitor_meta;
    monitor_meta.monitor = monitor;
    monitor_meta.reference_value = statement.column_text_nullable(first_column + 6);
    monitor_meta.type = type;
    return monitor_meta;
}

} // namespace

DeviceModelStorageSqlite::DeviceModelStorageSqlite(const fs::path& db_path, const fs::path& migration_files_path,
                                                   const fs::path& config_path) {
    if (db_path.empty() || migration_files_path.empty() || config_path.empty()) {
        EVLOG_AND_THROW(DeviceModelError("Can not initialize device model storage: one of the paths is empty."));
    }
    const auto component_configs = get_all_component_configs(config_path);
    InitDeviceModelDb init_device_model_db(db_path, migration_files_path);
    init_device_model_db.initialize_database(component_configs, false);

    initialize_connection(db_path);
}

DeviceModelStorageSqlite::DeviceModelStorageSqlite(const fs::path& db_path, const fs::path& migration_files_path) {
    const InitDeviceModelDb init_device_model_db(db_path, migration_files_path);
    initialize_connection(db_path);
}

DeviceModelStorageSqlite::DeviceModelStorageSqlite(const fs::path& db_path) {
    initialize_connection(db_path);
}

void DeviceModelStorageSqlite::initialize_connection(const fs::path& db_path) {
    db = std::make_unique<Connection>(db_path);
    if (!db->open_connection()) {
        EVLOG_AND_THROW(std::runtime_error("Could not open device model database at: " + db_path.string()));
    }
    EVLOG_info << "Established connection to device model database: " << db_path;
}

int DeviceModelStorageSqlite::get_component_id(const Component& component_id) {
    const std::string select_query = "SELECT ID FROM COMPONENT WHERE NAME = ? COLLATE NOCASE AND INSTANCE IS ? COLLATE "
                                     "NOCASE AND EVSE_ID IS ? AND CONNECTOR_ID IS ?";

    auto select_stmt = this->db->new_statement(select_query);

    select_stmt->bind_text(1, component_id.name.get(), SQLiteString::Transient);
    if (component_id.instance.has_value()) {
        select_stmt->bind_text(2, component_id.instance.value().get(), SQLiteString::Transient);
    } else {
        select_stmt->bind_null(2);
    }
    if (component_id.evse.has_value()) {
        select_stmt->bind_int(3, component_id.evse.value().id);
        if (component_id.evse.value().connectorId.has_value()) {
            select_stmt->bind_int(4, component_id.evse.value().connectorId.value());
        } else {
            select_stmt->bind_null(4);
        }
    } else {
        select_stmt->bind_null(3);
    }

    if (select_stmt->step() == SQLITE_ROW) {
        return select_stmt->column_int(0);
    }
    return -1;
}

int DeviceModelStorageSqlite::get_variable_id(const Component& component_id, const Variable& variable_id) {
    const auto _component_id = this->get_component_id(component_id);
    if (_component_id == -1) {
        return -1;
    }

    const std::string select_query = "SELECT ID FROM VARIABLE WHERE COMPONENT_ID = ? AND NAME = ? COLLATE NOCASE AND "
                                     "INSTANCE IS ? COLLATE NOCASE";
    auto select_stmt = this->db->new_statement(select_query);

    select_stmt->bind_int(1, _component_id);
    select_stmt->bind_text(2, variable_id.name.get(), SQLiteString::Transient);
    if (variable_id.instance.has_value()) {
        select_stmt->bind_text(3, variable_id.instance.value().get(), SQLiteString::Transient);
    } else {
        select_stmt->bind_null(3);
    }
    if (select_stmt->step() == SQLITE_ROW) {
        return select_stmt->column_int(0);
    }
    return -1;
}

DeviceModelMap DeviceModelStorageSqlite::get_device_model() {
    std::map<Component, std::map<Variable, VariableMetaData>> device_model;

    const std::string select_query =
        "SELECT c.NAME, c.EVSE_ID, c.CONNECTOR_ID, c.INSTANCE, v.NAME, v.INSTANCE, vc.DATATYPE_ID, "
        "vc.SUPPORTS_MONITORING, vc.UNIT, vc.MIN_LIMIT, vc.MAX_LIMIT, vc.VALUES_LIST "
        "FROM COMPONENT c "
        "JOIN VARIABLE v ON c.ID = v.COMPONENT_ID "
        "JOIN VARIABLE_CHARACTERISTICS vc ON vc.VARIABLE_ID = v.ID";

    auto select_stmt = this->db->new_statement(select_query);

    while (select_stmt->step() == SQLITE_ROW) {
        const auto [component, variable] = read_component_variable(*select_stmt, 0);

        VariableCharacteristics characteristics;
        VariableMetaData meta_data;
        characteristics.dataType = static_cast<DataEnum>(select_stmt->column_int(6));
        characteristics.supportsMonitoring = select_stmt->column_int(7) != 0;

        if (select_stmt->column_type(8) != SQLITE_NULL) {
            characteristics.unit = select_stmt->column_text(8);
        }

        if (select_stmt->column_type(9) != SQLITE_NULL) {
            characteristics.minLimit = select_stmt->column_double(9);
        }

        if (select_stmt->column_type(10) != SQLITE_NULL) {
            characteristics.maxLimit = select_stmt->column_double(10);
        }

        if (select_stmt->column_type(11) != SQLITE_NULL) {
            characteristics.valuesList = select_stmt->column_text(11);
        }

        meta_data.characteristics = characteristics;

        device_model[component][variable] = meta_data;
    }

    const std::string attributes_query = "SELECT c.NAME, c.EVSE_ID, c.CONNECTOR_ID, c.INSTANCE, v.NAME, v.INSTANCE, "
                                         "va.VALUE, va.MUTABILITY_ID, va.PERSISTENT, va.CONSTANT, va.TYPE_ID "
                                         "FROM VARIABLE_ATTRIBUTE va "
                                         "JOIN VARIABLE v ON v.ID = va.VARIABLE_ID "
                                         "JOIN COMPONENT c ON c.ID = v.COMPONENT_ID "
                                         "ORDER BY va.ID";

    auto attributes_stmt = this->db->new_statement(attributes_query);

    while (attributes_stmt->step() == SQLITE_ROW) {
        const auto [component, variable] = read_component_variable(*attributes_stmt, 0);
        auto* meta_data = find_variable(device_model, component, variable);
        if (meta_data == nullptr) {
            continue;
        }
        meta_data->attributes.push_back(read_attribute(*attributes_stmt, 6));
    }

    for (auto& stored_monitor : get_all_monitors()) {
        auto* meta_data = find_variable(device_model, stored_monitor.component, stored_monitor.variable);
        if (meta_data == nullptr) {
            // Monitors with an owner belong to a variable registered at runtime
            if (!stored_monitor.owner.has_value()) {
                EVLOG_warning << "Monitor " << stored_monitor.monitor.monitor.id << " of component "
                              << stored_monitor.component << " and variable " << stored_monitor.variable
                              << " has no variable in the component config and is not active";
            }
            continue;
        }
        meta_data->monitors.insert({stored_monitor.monitor.monitor.id, std::move(stored_monitor.monitor)});
    }

    EVLOG_info << "Successfully retrieved Device Model from DeviceModelStorage";
    return device_model;
}

std::optional<VariableAttribute> DeviceModelStorageSqlite::get_variable_attribute(const Component& component_id,
                                                                                  const Variable& variable_id,
                                                                                  const AttributeEnum& attribute_enum) {
    const auto attributes = this->get_variable_attributes(component_id, variable_id, attribute_enum);
    if (!attributes.empty()) {
        return attributes.at(0);
    }
    return std::nullopt;
}

std::vector<VariableAttribute>
DeviceModelStorageSqlite::get_variable_attributes(const Component& component_id, const Variable& variable_id,
                                                  const std::optional<AttributeEnum>& attribute_enum) {
    std::vector<VariableAttribute> attributes;
    const auto _variable_id = this->get_variable_id(component_id, variable_id);
    if (_variable_id == -1) {
        return attributes;
    }

    std::string select_query = "SELECT va.VALUE, va.MUTABILITY_ID, va.PERSISTENT, va.CONSTANT, va.TYPE_ID "
                               "FROM VARIABLE_ATTRIBUTE va "
                               "WHERE va.VARIABLE_ID = @variable_id";

    if (attribute_enum.has_value()) {
        std::stringstream ss;
        ss << select_query << "  AND va.TYPE_ID = " << static_cast<int>(attribute_enum.value());
        select_query = ss.str();
    }

    auto select_stmt = this->db->new_statement(select_query);

    select_stmt->bind_int(1, _variable_id);

    while (select_stmt->step() == SQLITE_ROW) {
        attributes.push_back(read_attribute(*select_stmt, 0));
    }

    return attributes;
}

SetVariableStatusEnum DeviceModelStorageSqlite::set_variable_attribute_value(const Component& component_id,
                                                                             const Variable& variable_id,
                                                                             const AttributeEnum& attribute_enum,
                                                                             const std::string& value,
                                                                             const std::string& source) {
    auto transaction = this->db->begin_transaction();

    const auto _variable_id = this->get_variable_id(component_id, variable_id);
    if (_variable_id == -1) {
        return SetVariableStatusEnum::Rejected;
    }

    const std::string insert_query =
        "UPDATE VARIABLE_ATTRIBUTE SET VALUE = ?, VALUE_SOURCE = ? WHERE VARIABLE_ID = ? AND TYPE_ID = ?";
    auto insert_stmt = this->db->new_statement(insert_query);

    insert_stmt->bind_text(1, value);
    insert_stmt->bind_text(2, source);
    insert_stmt->bind_int(3, _variable_id);
    insert_stmt->bind_int(4, static_cast<int>(attribute_enum));
    if (insert_stmt->step() != SQLITE_DONE) {
        EVLOG_error << this->db->get_error_message();
        return SetVariableStatusEnum::Rejected;
    }

    transaction->commit();
    return SetVariableStatusEnum::Accepted;
}

SetVariableStatusEnum DeviceModelStorageSqlite::set_stored_value(const StoredVariableValue& value) {
    const std::string upsert_query =
        "INSERT INTO VARIABLE_ATTRIBUTE_VALUE (COMPONENT_NAME, COMPONENT_INSTANCE, EVSE_ID, CONNECTOR_ID, "
        "VARIABLE_NAME, VARIABLE_INSTANCE, TYPE_ID, VALUE, VALUE_SOURCE, OWNER) VALUES (@component_name, "
        "@component_instance, @evse_id, @connector_id, @variable_name, @variable_instance, @type_id, @value, "
        "@value_source, @owner) ON CONFLICT (COMPONENT_NAME, COMPONENT_INSTANCE, EVSE_ID, CONNECTOR_ID, "
        "VARIABLE_NAME, VARIABLE_INSTANCE, TYPE_ID) DO UPDATE SET VALUE = excluded.VALUE, VALUE_SOURCE = "
        "excluded.VALUE_SOURCE, OWNER = excluded.OWNER";

    auto transaction = this->db->begin_transaction();
    auto upsert_stmt = this->db->new_statement(upsert_query);
    bind_name_key(*upsert_stmt, value.component, value.variable);
    upsert_stmt->bind_int("@type_id", static_cast<int>(value.attribute));
    upsert_stmt->bind_text("@value", value.value, SQLiteString::Transient);
    if (value.value_source.has_value()) {
        upsert_stmt->bind_text("@value_source", value.value_source.value(), SQLiteString::Transient);
    } else {
        upsert_stmt->bind_null("@value_source");
    }
    if (value.owner.has_value()) {
        upsert_stmt->bind_text("@owner", value.owner.value(), SQLiteString::Transient);
    } else {
        upsert_stmt->bind_null("@owner");
    }
    if (upsert_stmt->step() != SQLITE_DONE) {
        EVLOG_error << this->db->get_error_message();
        return SetVariableStatusEnum::Rejected;
    }

    transaction->commit();
    return SetVariableStatusEnum::Accepted;
}

std::vector<StoredVariableValue> DeviceModelStorageSqlite::get_stored_values() {
    auto select_stmt = this->db->new_statement(
        "SELECT COMPONENT_NAME, COMPONENT_INSTANCE, EVSE_ID, CONNECTOR_ID, VARIABLE_NAME, VARIABLE_INSTANCE, TYPE_ID, "
        "VALUE, VALUE_SOURCE, OWNER FROM VARIABLE_ATTRIBUTE_VALUE");

    std::vector<StoredVariableValue> values;
    while (select_stmt->step() == SQLITE_ROW) {
        auto [component, variable] = read_name_key(*select_stmt, 0);
        StoredVariableValue stored_value;
        stored_value.component = std::move(component);
        stored_value.variable = std::move(variable);
        stored_value.attribute = static_cast<AttributeEnum>(select_stmt->column_int(6));
        stored_value.value = select_stmt->column_text_nullable(7).value_or("");
        stored_value.value_source = select_stmt->column_text_nullable(8);
        stored_value.owner = select_stmt->column_text_nullable(9);
        values.push_back(std::move(stored_value));
    }
    return values;
}

bool DeviceModelStorageSqlite::remove_stored_value(const Component& component_id, const Variable& variable_id,
                                                   const AttributeEnum& attribute_enum) {
    auto transaction = this->db->begin_transaction();
    auto delete_stmt = this->db->new_statement(std::string("DELETE FROM VARIABLE_ATTRIBUTE_VALUE WHERE ") +
                                               NAME_KEY_CONDITION + " AND TYPE_ID = @type_id");
    bind_name_key(*delete_stmt, component_id, variable_id);
    delete_stmt->bind_int("@type_id", static_cast<int>(attribute_enum));
    if (delete_stmt->step() != SQLITE_DONE) {
        EVLOG_error << this->db->get_error_message();
        return false;
    }

    const auto changes = delete_stmt->changes();
    transaction->commit();
    return changes == 1;
}

bool DeviceModelStorageSqlite::update_monitoring_reference(const std::int32_t monitor_id,
                                                           const std::string& reference_value) {
    auto transaction = this->db->begin_transaction();

    const std::string update_query = "UPDATE VARIABLE_MONITORING SET REFERENCE_VALUE = ? WHERE ID = ?";
    auto update_stmt = this->db->new_statement(update_query);

    update_stmt->bind_text(1, reference_value, SQLiteString::Transient);
    update_stmt->bind_int(2, monitor_id);

    if (update_stmt->step() != SQLITE_DONE) {
        EVLOG_error << this->db->get_error_message();
        return false;
    }

    const int changes = update_stmt->changes();
    transaction->commit();
    return changes == 1;
}

std::optional<VariableMonitoringMeta> DeviceModelStorageSqlite::set_monitoring_data(const SetMonitoringData& data,
                                                                                    const VariableMonitorType type) {
    auto transaction = this->db->begin_transaction();

    // The reference of a delta monitor stays NULL if this storage holds no value for the variable; DeviceModel sets
    // it from the value it holds
    std::optional<std::string> actual_value;
    if (data.type == MonitorEnum::Delta) {
        actual_value = this->get_actual_value(data.component, data.variable);
    }

    std::string insert_query =
        "INSERT OR REPLACE INTO VARIABLE_MONITORING (COMPONENT_NAME, COMPONENT_INSTANCE, EVSE_ID, CONNECTOR_ID, "
        "VARIABLE_NAME, VARIABLE_INSTANCE, SEVERITY, \"TRANSACTION\", TYPE_ID, CONFIG_TYPE_ID, VALUE, REFERENCE_VALUE";
    if (data.id.has_value()) {
        insert_query += ", ID";
    }
    insert_query += ") VALUES (@component_name, @component_instance, @evse_id, @connector_id, @variable_name, "
                    "@variable_instance, @severity, @transaction, @type_id, @config_type_id, @value, "
                    "@reference_value";
    if (data.id.has_value()) {
        insert_query += ", @id";
    }
    insert_query += ")";

    auto insert_stmt = this->db->new_statement(insert_query);

    bind_name_key(*insert_stmt, data.component, data.variable);
    insert_stmt->bind_int("@severity", data.severity);
    insert_stmt->bind_int("@transaction", static_cast<int>(data.transaction.value_or(false)));
    insert_stmt->bind_int("@type_id", static_cast<int>(data.type));
    insert_stmt->bind_int("@config_type_id", static_cast<int>(type));
    insert_stmt->bind_double("@value", data.value);

    if (actual_value.has_value()) {
        // If the monitor is a delta, we need to insert the reference value
        insert_stmt->bind_text("@reference_value", actual_value.value(), SQLiteString::Transient);
    } else {
        insert_stmt->bind_null("@reference_value");
    }

    if (data.id.has_value()) {
        insert_stmt->bind_int("@id", data.id.value());
    }

    if (insert_stmt->step() != SQLITE_DONE) {
        EVLOG_error << this->db->get_error_message();
        return std::nullopt;
    }

    const std::int64_t last_row_id = this->db->get_last_inserted_rowid();
    transaction->commit();

    VariableMonitoringMeta meta;

    if (last_row_id > std::numeric_limits<std::int32_t>::max()) {
        EVLOG_warning << "Monitor id exceeds 32 bit integer, clamped at maximum";
    }

    meta.monitor.id = clamp_to<std::int32_t>(last_row_id);
    meta.monitor.severity = data.severity;
    meta.monitor.transaction = data.transaction.value_or(false);
    meta.monitor.type = data.type;
    meta.monitor.value = data.value;
    // this is a workaround to set the eventNotificationType which became a required property for the
    // VariableMonitoringType in OCPP2.1
    meta.monitor.eventNotificationType = conversions::variable_monitor_type_to_event_notification_type(type);
    meta.type = type;
    meta.reference_value = actual_value;

    return meta;
}

std::optional<std::string> DeviceModelStorageSqlite::get_actual_value(const Component& component_id,
                                                                      const Variable& variable_id) {
    if (this->get_variable_id(component_id, variable_id) != -1) {
        const auto attribute = this->get_variable_attribute(component_id, variable_id, AttributeEnum::Actual);
        if (!attribute.has_value() || !attribute->value.has_value()) {
            return std::nullopt;
        }
        return attribute->value->get();
    }

    auto select_stmt = this->db->new_statement(std::string("SELECT VALUE FROM VARIABLE_ATTRIBUTE_VALUE WHERE ") +
                                               NAME_KEY_CONDITION + " AND TYPE_ID = @type_id");
    bind_name_key(*select_stmt, component_id, variable_id);
    select_stmt->bind_int("@type_id", static_cast<int>(AttributeEnum::Actual));
    if (select_stmt->step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return select_stmt->column_text_nullable(0);
}

std::vector<StoredMonitor> DeviceModelStorageSqlite::get_all_monitors() {
    auto select_stmt = this->db->new_statement(
        std::string("SELECT COMPONENT_NAME, COMPONENT_INSTANCE, EVSE_ID, CONNECTOR_ID, VARIABLE_NAME, "
                    "VARIABLE_INSTANCE, OWNER, ") +
        MONITOR_COLUMNS + " FROM VARIABLE_MONITORING ORDER BY ID");

    std::vector<StoredMonitor> monitors;
    while (select_stmt->step() == SQLITE_ROW) {
        auto [component, variable] = read_name_key(*select_stmt, 0);
        StoredMonitor stored_monitor;
        stored_monitor.component = std::move(component);
        stored_monitor.variable = std::move(variable);
        stored_monitor.owner = select_stmt->column_text_nullable(6);
        stored_monitor.monitor = read_monitor(*select_stmt, 7);
        monitors.push_back(std::move(stored_monitor));
    }
    return monitors;
}

ClearMonitoringStatusEnum DeviceModelStorageSqlite::clear_variable_monitor(int monitor_id, bool allow_protected) {
    auto transaction = this->db->begin_transaction();

    const std::string select_query = "SELECT COUNT(*) FROM VARIABLE_MONITORING WHERE ID = ?";

    auto select_stmt = this->db->new_statement(select_query);
    select_stmt->bind_int(1, monitor_id);

    if (select_stmt->step() != SQLITE_ROW) {
        EVLOG_error << this->db->get_error_message();
        return ClearMonitoringStatusEnum::Rejected;
    }
    // If we couldn't find a monitor in the DB
    if (select_stmt->column_int(0) != 1) {
        return ClearMonitoringStatusEnum::NotFound;
    }

    std::string delete_query;

    if (allow_protected) {
        delete_query = "DELETE FROM VARIABLE_MONITORING WHERE ID = ?";
    } else {
        delete_query = "DELETE FROM VARIABLE_MONITORING WHERE ID = ? AND CONFIG_TYPE_ID = ?";
    }

    auto delete_stmt = this->db->new_statement(delete_query);

    delete_stmt->bind_int(1, monitor_id);

    if (!allow_protected) {
        delete_stmt->bind_int(2, static_cast<int>(VariableMonitorType::CustomMonitor));
    }

    if (delete_stmt->step() != SQLITE_DONE) {
        EVLOG_error << this->db->get_error_message();
        return ClearMonitoringStatusEnum::Rejected;
    }

    // Ensure that we deleted 1 row
    const auto changes = delete_stmt->changes();
    transaction->commit();
    return changes == 1 ? ClearMonitoringStatusEnum::Accepted : ClearMonitoringStatusEnum::Rejected;
}

std::int32_t DeviceModelStorageSqlite::clear_custom_variable_monitors() {
    const std::string delete_query = "DELETE FROM VARIABLE_MONITORING WHERE CONFIG_TYPE_ID = ?";

    auto transaction = this->db->begin_transaction();
    auto delete_stmt = this->db->new_statement(delete_query);

    delete_stmt->bind_int(1, static_cast<int>(VariableMonitorType::CustomMonitor));
    if (delete_stmt->step() != SQLITE_DONE) {
        EVLOG_error << this->db->get_error_message();
        return 0;
    }

    const auto changes = delete_stmt->changes();
    transaction->commit();
    return changes;
}

void DeviceModelStorageSqlite::check_integrity() {
    // Function is now empty because checks are already done elsewhere (for example the check for 'required' variables).
}

bool DeviceModelStorageSqlite::create_network_configuration_slot_from_default_schema(std::int32_t new_slot) {
    const std::string new_instance = std::to_string(new_slot);

    // Embedded schema parse. Failure means a build defect, not a runtime data issue.
    std::pair<ComponentKey, std::vector<DeviceModelVariable>> parsed;
    try {
        parsed = parse_component_config_from_string(get_default_network_configuration_schema());
    } catch (const std::exception& e) {
        EVLOG_critical << "create_network_configuration_slot_from_default_schema: embedded default "
                          "schema could not be parsed (build defect): "
                       << e.what();
        return false;
    }
    const auto& variables = parsed.second;

    try {
        auto select_existing = this->db->new_statement("SELECT 1 FROM COMPONENT WHERE NAME = 'NetworkConfiguration' "
                                                       "COLLATE NOCASE AND INSTANCE = @instance");
        select_existing->bind_text("@instance", new_instance, SQLiteString::Transient);
        if (select_existing->step() == SQLITE_ROW) {
            EVLOG_warning << "create_network_configuration_slot_from_default_schema: slot " << new_slot
                          << " already exists";
            return false;
        }

        auto transaction = this->db->begin_transaction();

        auto bind_opt_text = [](everest::db::sqlite::StatementInterface& stmt, const char* p,
                                const std::optional<std::string>& v) {
            if (v.has_value() && !v->empty()) {
                stmt.bind_text(p, *v, SQLiteString::Transient);
            } else {
                stmt.bind_null(p);
            }
        };
        auto bind_opt_int = [](everest::db::sqlite::StatementInterface& stmt, const char* p, const auto& v) {
            if (v.has_value()) {
                stmt.bind_int(p, static_cast<int>(*v));
            } else {
                stmt.bind_null(p);
            }
        };
        auto bind_opt_double = [](everest::db::sqlite::StatementInterface& stmt, const char* p, const auto& v) {
            if (v.has_value()) {
                stmt.bind_double(p, static_cast<double>(*v));
            } else {
                stmt.bind_null(p);
            }
        };

        auto insert_component = this->db->new_statement("INSERT INTO COMPONENT (NAME, INSTANCE, EVSE_ID, CONNECTOR_ID) "
                                                        "VALUES ('NetworkConfiguration', @instance, NULL, NULL)");
        insert_component->bind_text("@instance", new_instance, SQLiteString::Transient);
        if (insert_component->step() != SQLITE_DONE) {
            EVLOG_error << "create_network_configuration_slot_from_default_schema: insert COMPONENT failed: "
                        << this->db->get_error_message();
            return false;
        }
        const auto new_component_id = this->db->get_last_inserted_rowid();

        for (const auto& variable : variables) {
            auto insert_variable =
                this->db->new_statement("INSERT INTO VARIABLE (NAME, INSTANCE, COMPONENT_ID) VALUES (@n, @i, @cid)");
            insert_variable->bind_text("@n", variable.name, SQLiteString::Transient);
            bind_opt_text(*insert_variable, "@i", variable.instance);
            insert_variable->bind_int("@cid", static_cast<int>(new_component_id));
            if (insert_variable->step() != SQLITE_DONE) {
                EVLOG_error << "create_network_configuration_slot_from_default_schema: insert VARIABLE failed: "
                            << this->db->get_error_message();
                return false;
            }
            const auto new_variable_id = this->db->get_last_inserted_rowid();

            auto insert_characteristics = this->db->new_statement(
                "INSERT INTO VARIABLE_CHARACTERISTICS "
                "(DATATYPE_ID, VARIABLE_ID, MAX_LIMIT, MIN_LIMIT, SUPPORTS_MONITORING, UNIT, VALUES_LIST) "
                "VALUES (@dtype, @vid, @max, @min, @supp, @unit, @vlist)");
            insert_characteristics->bind_int("@dtype", static_cast<int>(variable.characteristics.dataType));
            insert_characteristics->bind_int("@vid", static_cast<int>(new_variable_id));
            bind_opt_double(*insert_characteristics, "@max", variable.characteristics.maxLimit);
            bind_opt_double(*insert_characteristics, "@min", variable.characteristics.minLimit);
            insert_characteristics->bind_int("@supp", variable.characteristics.supportsMonitoring ? 1 : 0);
            bind_opt_text(*insert_characteristics, "@unit", variable.characteristics.unit);
            bind_opt_text(*insert_characteristics, "@vlist", variable.characteristics.valuesList);
            if (insert_characteristics->step() != SQLITE_DONE) {
                EVLOG_error << "create_network_configuration_slot_from_default_schema: "
                               "insert VARIABLE_CHARACTERISTICS failed: "
                            << this->db->get_error_message();
                return false;
            }

            for (const auto& db_attribute : variable.attributes) {
                const auto& attribute = db_attribute.variable_attribute;
                auto insert_attribute = this->db->new_statement(
                    "INSERT INTO VARIABLE_ATTRIBUTE "
                    "(VARIABLE_ID, MUTABILITY_ID, PERSISTENT, CONSTANT, TYPE_ID, VALUE_SOURCE, VALUE) "
                    "VALUES (@vid, @mut, 1, 0, @type, 'internal', NULL)");
                insert_attribute->bind_int("@vid", static_cast<int>(new_variable_id));
                bind_opt_int(*insert_attribute, "@mut", attribute.mutability);
                bind_opt_int(*insert_attribute, "@type", attribute.type);
                if (insert_attribute->step() != SQLITE_DONE) {
                    EVLOG_error << "create_network_configuration_slot_from_default_schema: "
                                   "insert VARIABLE_ATTRIBUTE failed: "
                                << this->db->get_error_message();
                    return false;
                }
            }
        }

        transaction->commit();
        EVLOG_info << "Created NetworkConfiguration_" << new_slot << " from embedded default schema ("
                   << variables.size() << " variables)";
        return true;
    } catch (const std::exception& e) {
        EVLOG_error << "create_network_configuration_slot_from_default_schema: exception: " << e.what();
        return false;
    }
}

} // namespace v2
} // namespace ocpp
