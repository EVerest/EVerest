// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#include <utils/config/slot_manager.hpp>

#include <everest/compile_time_settings.hpp>
#include <everest/database/exceptions.hpp>
#include <everest/database/sqlite/connection.hpp>
#include <everest/database/sqlite/schema_updater.hpp>
#include <everest/logging.hpp>

#include <utils/date.hpp>

using namespace everest::db;
using namespace everest::db::sqlite;

namespace everest::config {

namespace fs = std::filesystem;

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

GenericResponseStatus SqliteConfigSlotManager::write_config_slot(int slot_id) {
    auto transaction = this->db->begin_transaction();

    auto config_stmt = this->db->new_statement("INSERT INTO CONFIG (ID) VALUES (?) ON CONFLICT(ID) DO NOTHING;");
    config_stmt->bind_int(1, slot_id);
    if (config_stmt->step() != SQLITE_DONE) {
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
