// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#include <utils/config/slot_manager.hpp>

#include <everest/compile_time_settings.hpp>
#include <everest/database/exceptions.hpp>
#include <everest/database/sqlite/connection.hpp>
#include <everest/database/sqlite/schema_updater.hpp>
#include <everest/logging.hpp>

#include <utils/config/settings.hpp>
#include <utils/date.hpp>

using namespace everest::db;
using namespace everest::db::sqlite;

namespace everest::config {

namespace fs = std::filesystem;

/// \brief Helper for accessing the column indices of the SETTING table (same as sqlite_storage.cpp)
enum class SettingColumnIndex : int {
    COL_ID = 0,
    COL_PREFIX,
    COL_CONFIG_FILE,
    COL_CONFIGS_DIR,
    COL_SCHEMAS_DIR,
    COL_MODULES_DIR,
    COL_INTERFACES_DIR,
    COL_TYPES_DIR,
    COL_ERRORS_DIR,
    COL_WWW_DIR,
    COL_LOGGING_CONFIG_FILE,
    COL_CONTROLLER_PORT,
    COL_CONTROLLER_RPC_TIMEOUT_MS,
    COL_MQTT_BROKER_SOCKET_PATH,
    COL_MQTT_BROKER_HOST,
    COL_MQTT_BROKER_PORT,
    COL_MQTT_EVEREST_PREFIX,
    COL_MQTT_EXTERNAL_PREFIX,
    COL_TELEMETRY_PREFIX,
    COL_TELEMETRY_ENABLED,
    COL_VALIDATE_SCHEMA,
    COL_RUN_AS_USER,
    COL_FORWARD_EXCEPTIONS,
};

namespace {
int to_int(SettingColumnIndex idx) {
    return static_cast<int>(idx);
}
} // namespace

SqliteConfigSlotManager::SqliteConfigSlotManager(const std::filesystem::path& db_path,
                                                 const std::filesystem::path& migrations_path) {
    db = std::make_unique<Connection>(db_path);

    SchemaUpdater updater{db.get()};
    if (!updater.apply_migration_files(migrations_path, TARGET_MIGRATION_FILE_VERSION)) {
        if (db_path.parent_path().empty()) {
            EVLOG_error << "Could not apply migrations for database at provided path: \"" << db_path.string()
                        << "\" likely because the database path is just a filename. You MUST provide a full path to "
                           "the database.";
        }
        throw MigrationException("SQL migration failed");
    }

    if (!db->open_connection()) {
        throw std::runtime_error("Could not open database at provided path: " + db_path.string());
    } else {
        EVLOG_info << "Established connection to database successfully: " << db_path;
    }
}

int SqliteConfigSlotManager::next_slot_id() {
    const std::string sql = "SELECT COALESCE(MAX(ID) + 1, 0) FROM CONFIG";
    auto stmt = this->db->new_statement(sql);
    if (stmt->step() == SQLITE_ROW) {
        return stmt->column_int(0);
    }
    return 0;
}

bool SqliteConfigSlotManager::is_valid(int slot_id) {
    const std::string sql = "SELECT 1 FROM CONFIG_META WHERE ID = @config_id AND VALID = 1";
    auto stmt = this->db->new_statement(sql);
    stmt->bind_int("@config_id", slot_id);
    return stmt->step() == SQLITE_ROW;
}

GenericResponseStatus SqliteConfigSlotManager::write_config_slot(int slot_id,
                                                                   const Everest::ManagerSettings& manager_settings) {
    auto transaction = this->db->begin_transaction();

    // Ensure the CONFIG identity row exists before writing framework settings.
    auto config_stmt = this->db->new_statement(
        "INSERT INTO CONFIG (ID) VALUES (?) ON CONFLICT(ID) DO NOTHING;");
    config_stmt->bind_int(1, slot_id);
    if (config_stmt->step() != SQLITE_DONE) {
        return GenericResponseStatus::Failed;
    }

    std::vector<std::string> keys = {"ID",
                                     "PREFIX",
                                     "CONFIG_FILE",
                                     "CONFIGS_DIR",
                                     "SCHEMAS_DIR",
                                     "MODULES_DIR",
                                     "INTERFACES_DIR",
                                     "TYPES_DIR",
                                     "ERRORS_DIR",
                                     "WWW_DIR",
                                     "LOGGING_CONFIG_FILE",
                                     "CONTROLLER_PORT",
                                     "CONTROLLER_RPC_TIMEOUT_MS",
                                     "MQTT_BROKER_SOCKET_PATH",
                                     "MQTT_BROKER_HOST",
                                     "MQTT_BROKER_PORT",
                                     "MQTT_EVEREST_PREFIX",
                                     "MQTT_EXTERNAL_PREFIX",
                                     "TELEMETRY_PREFIX",
                                     "TELEMETRY_ENABLED",
                                     "VALIDATE_SCHEMA",
                                     "RUN_AS_USER",
                                     "FORWARD_EXCEPTIONS"};

    std::string sql = "INSERT INTO FRAMEWORK_SETTINGS (";
    for (size_t i = 0; i < keys.size(); ++i) {
        sql += keys.at(i);
        if (i < keys.size() - 1) {
            sql += ", ";
        }
    }
    sql += ") VALUES (";
    for (size_t i = 0; i < keys.size(); ++i) {
        sql += (i == 0 ? "?" : ", ?");
    }
    sql += ") ON CONFLICT(ID) DO UPDATE SET ";
    for (size_t i = 1; i < keys.size(); ++i) {
        sql += keys.at(i) + " = excluded." + keys.at(i);
        if (i < keys.size() - 1) {
            sql += ", ";
        }
    }
    sql += ";";

    auto stmt = this->db->new_statement(sql);

    stmt->bind_int(to_int(SettingColumnIndex::COL_ID) + 1, slot_id);

    auto bind_text_opt = [&](SettingColumnIndex index, int offset, const std::optional<std::string>& opt) {
        if (opt.has_value()) {
            stmt->bind_text(to_int(index) + offset, opt.value(), SQLiteString::Transient);
        } else {
            stmt->bind_null(to_int(index) + offset);
        }
    };

    auto bind_path_opt = [&](SettingColumnIndex index, int offset, const std::optional<fs::path>& opt) {
        if (opt.has_value()) {
            stmt->bind_text(to_int(index) + offset, opt.value().string(), SQLiteString::Transient);
        } else {
            stmt->bind_null(to_int(index) + offset);
        }
    };

    auto bind_int_opt = [&](SettingColumnIndex index, int offset, const std::optional<int>& opt) {
        if (opt.has_value()) {
            stmt->bind_int(to_int(index) + offset, opt.value());
        } else {
            stmt->bind_null(to_int(index) + offset);
        }
    };

    auto bind_bool_opt = [&](SettingColumnIndex index, int offset, const std::optional<bool>& opt) {
        if (opt) {
            stmt->bind_int(to_int(index) + offset, opt.value() ? 1 : 0);
        } else {
            stmt->bind_null(to_int(index) + offset);
        }
    };

    bind_path_opt(SettingColumnIndex::COL_PREFIX, 1, manager_settings.runtime_settings.prefix);
    bind_path_opt(SettingColumnIndex::COL_CONFIG_FILE, 1, manager_settings.config_file);
    bind_path_opt(SettingColumnIndex::COL_CONFIGS_DIR, 1, manager_settings.configs_dir);
    bind_path_opt(SettingColumnIndex::COL_SCHEMAS_DIR, 1, manager_settings.schemas_dir);
    bind_path_opt(SettingColumnIndex::COL_MODULES_DIR, 1, manager_settings.runtime_settings.modules_dir);
    bind_path_opt(SettingColumnIndex::COL_INTERFACES_DIR, 1, manager_settings.interfaces_dir);
    bind_path_opt(SettingColumnIndex::COL_TYPES_DIR, 1, manager_settings.types_dir);
    bind_path_opt(SettingColumnIndex::COL_ERRORS_DIR, 1, manager_settings.errors_dir);
    bind_path_opt(SettingColumnIndex::COL_WWW_DIR, 1, manager_settings.www_dir);
    bind_path_opt(SettingColumnIndex::COL_LOGGING_CONFIG_FILE, 1,
                  manager_settings.runtime_settings.logging_config_file);

    bind_int_opt(SettingColumnIndex::COL_CONTROLLER_PORT, 1, manager_settings.controller_port);
    bind_int_opt(SettingColumnIndex::COL_CONTROLLER_RPC_TIMEOUT_MS, 1, manager_settings.controller_rpc_timeout_ms);

    bind_text_opt(SettingColumnIndex::COL_MQTT_BROKER_SOCKET_PATH, 1,
                  manager_settings.mqtt_settings.broker_socket_path);
    bind_text_opt(SettingColumnIndex::COL_MQTT_BROKER_HOST, 1, manager_settings.mqtt_settings.broker_host);
    bind_int_opt(SettingColumnIndex::COL_MQTT_BROKER_PORT, 1, manager_settings.mqtt_settings.broker_port);
    bind_text_opt(SettingColumnIndex::COL_MQTT_EVEREST_PREFIX, 1, manager_settings.mqtt_settings.everest_prefix);
    bind_text_opt(SettingColumnIndex::COL_MQTT_EXTERNAL_PREFIX, 1, manager_settings.mqtt_settings.external_prefix);
    bind_text_opt(SettingColumnIndex::COL_TELEMETRY_PREFIX, 1, manager_settings.runtime_settings.telemetry_prefix);

    bind_bool_opt(SettingColumnIndex::COL_TELEMETRY_ENABLED, 1, manager_settings.runtime_settings.telemetry_enabled);
    bind_bool_opt(SettingColumnIndex::COL_VALIDATE_SCHEMA, 1, manager_settings.runtime_settings.validate_schema);
    bind_text_opt(SettingColumnIndex::COL_RUN_AS_USER, 1, manager_settings.run_as_user);
    bind_bool_opt(SettingColumnIndex::COL_FORWARD_EXCEPTIONS, 1, manager_settings.runtime_settings.forward_exceptions);

    if (stmt->step() != SQLITE_DONE) {
        return GenericResponseStatus::Failed;
    }

    transaction->commit();
    return GenericResponseStatus::OK;
}

std::vector<StoredSlotInfo> SqliteConfigSlotManager::list_slots() {
    // Join SETTING with CONFIG_META.
    const std::string sql = "SELECT c.ID, COALESCE(cm.LAST_UPDATED, ''), COALESCE(cm.VALID, 0), cm.CONFIG_FILE_PATH "
                            "FROM CONFIG c LEFT JOIN CONFIG_META cm ON cm.ID = c.ID "
                            "ORDER BY c.ID";
    auto stmt = this->db->new_statement(sql);

    std::vector<StoredSlotInfo> result;
    while (stmt->step() == SQLITE_ROW) {
        StoredSlotInfo info;
        info.id = stmt->column_int(0);
        info.last_updated = stmt->column_text(1);
        info.is_valid = stmt->column_int(2) != 0;
        info.config_file_path = stmt->column_text_nullable(3);
        result.push_back(std::move(info));
    }
    return result;
}

GenericResponseStatus SqliteConfigSlotManager::delete_slot(int slot_id) {
    try {
        // Enable foreign keys so ON DELETE CASCADE propagates from SETTING to all dependent tables.
        this->db->execute_statement("PRAGMA foreign_keys = ON;");
        auto stmt = this->db->new_statement("DELETE FROM CONFIG WHERE ID = @config_id;");
        stmt->bind_int("@config_id", slot_id);
        stmt->step();
        this->db->execute_statement("PRAGMA foreign_keys = OFF;");
        return GenericResponseStatus::OK;
    } catch (const std::exception& e) {
        EVLOG_error << "Failed to delete slot with id " << slot_id << ": " << e.what();
        return GenericResponseStatus::Failed;
    }
}

} // namespace everest::config
