// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include <algorithm>
#include <cctype>
#include <chrono>

#include <everest/database/exceptions.hpp>
#include <everest/database/sqlite/connection.hpp>
#include <everest/logging.hpp>

using namespace std::chrono_literals;
using namespace std::string_literals;

namespace {
constexpr auto UNKNOWN_SQL_ERROR{"<unknown>"};
}

namespace everest::db::sqlite {

class DatabaseTransaction : public TransactionInterface {
private:
    Connection& database;
    std::unique_lock<std::timed_mutex> mutex;
    bool is_active = false;

public:
    DatabaseTransaction(Connection& database, std::unique_lock<std::timed_mutex> mutex, bool fkeys_enabled = false,
                        bool fkeys_deferred = false) :
        database{database}, mutex{std::move(mutex)} {
        // "PRAGMA foreign_keys" is a no-op inside a transaction: SQLite only honours it while no
        // BEGIN/SAVEPOINT is pending. It must therefore be set *before* BEGIN, otherwise foreign
        // key enforcement (and ON DELETE CASCADE) is silently skipped for this transaction.
        if (fkeys_enabled) {
            if (!this->database.execute_statement("PRAGMA foreign_keys = ON;")) {
                throw QueryExecutionException("Failed to enable foreign keys");
            }
        }

        if (!this->database.execute_statement("BEGIN TRANSACTION")) {
            throw QueryExecutionException("Failed to begin transaction");
        }
        is_active = true;

        // "PRAGMA defer_foreign_keys", unlike foreign_keys, is valid inside a transaction and
        // resets automatically at COMMIT/ROLLBACK. It postpones enforcement (enabled above) to
        // commit time, allowing constraints to be broken temporarily within the transaction.
        if (fkeys_enabled && fkeys_deferred) {
            if (!this->database.execute_statement("PRAGMA defer_foreign_keys = ON;")) {
                // Let the destructor handle the rollback.
                throw QueryExecutionException("Failed to defer foreign keys");
            }
        }
    }
    DatabaseTransaction(const DatabaseTransaction&) = delete;
    DatabaseTransaction& operator=(const DatabaseTransaction&) = delete;

    // Will by default rollback the transaction if destructed
    ~DatabaseTransaction() override {
        if (is_active) {
            if (!this->database.execute_statement("ROLLBACK TRANSACTION")) {
                EVLOG_critical << "Failed to rollback transaction in destructor. Database may be in an inconsistent "
                                  "state: "
                               << this->database.get_error_message();
            }
        }
    }

    void commit() override {
        if (!is_active) {
            return;
        }
        is_active = false;
        const bool success = this->database.execute_statement("COMMIT TRANSACTION");
        this->mutex.unlock();
        if (!success) {
            throw QueryExecutionException(this->database.get_error_message());
        }
    }

    void rollback() override {
        if (!is_active) {
            return;
        }
        is_active = false;
        const bool success = this->database.execute_statement("ROLLBACK TRANSACTION");
        this->mutex.unlock();
        if (!success) {
            throw QueryExecutionException(this->database.get_error_message());
        }
    }
};

Connection::Connection(const fs::path& database_file_path) noexcept :
    db(nullptr), database_file_path(database_file_path), open_count(0) {
}

Connection::~Connection() {
    // There could still be a transaction active and we have no way to abort it,
    // so wait a few seconds to give it time to finish
    auto lock = std::unique_lock(this->transaction_mutex, 2s);
    close_connection_internal(true);
}

bool Connection::open_connection() {
    if (this->open_count.fetch_add(1) != 0) {
        EVLOG_debug << "Connection already opened";
        return true;
    }

    // Add special exception for databases in ram; we don't need to create a path
    // for them and we must not attempt to enable WAL on them.
    const bool in_memory = this->database_file_path.string().find(":memory:") != std::string::npos ||
                           this->database_file_path.string().find("mode=memory") != std::string::npos;

    if (!in_memory && !fs::exists(this->database_file_path.parent_path())) {
        fs::create_directories(this->database_file_path.parent_path());
    }

    if (sqlite3_open_v2(this->database_file_path.c_str(), &this->db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
        EVLOG_error << "Error opening database at " << this->database_file_path << ": " << sqlite3_errmsg(db);
        return false;
    }
    // Retry briefly on SQLITE_BUSY instead of failing immediately, so a reader that races with a
    // concurrent writer on the same database waits for the lock to clear rather than surfacing a
    // transient SQLITE_BUSY to the caller.
    constexpr int busy_timeout_ms = 5000;
    sqlite3_busy_timeout(this->db, busy_timeout_ms);

    // Enable WAL journal mode on file-based databases so concurrent readers and a single writer
    // stop serializing. Best-effort: if WAL cannot be established (e.g. on a filesystem that does
    // not support it) we log and continue with whatever journal mode SQLite reports rather than
    // failing the open.
    if (!in_memory) {
        auto statement = this->new_statement("PRAGMA journal_mode=WAL");
        if (statement->step() != SQLITE_ROW) {
            EVLOG_warning << "Could not query journal mode while enabling WAL on " << this->database_file_path << ": "
                          << this->get_error_message();
        } else {
            const std::string journal_mode = statement->column_text(0);
            std::string lowered = journal_mode;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lowered != "wal") {
                EVLOG_warning << "Could not enable WAL journal mode on " << this->database_file_path
                              << ", continuing with '" << journal_mode << "'";
            }
        }
    }

    EVLOG_debug << "Established connection to database: " << this->database_file_path;
    return true;
}

bool Connection::close_connection() {
    return this->close_connection_internal(false);
}

bool Connection::close_connection_internal(bool force_close) {
    if (!force_close && this->open_count.fetch_sub(1) != 1) {
        EVLOG_debug << "Connection should remain open for other users";
        return true;
    }

    if (this->db == nullptr) {
        EVLOG_info << "Database file " << this->database_file_path << " is already closed";
        return true;
    }

    // forcefully finalize all statements before calling sqlite3_close
    sqlite3_stmt* stmt = nullptr;
    while ((stmt = sqlite3_next_stmt(db, stmt)) != nullptr) {
        sqlite3_finalize(stmt);
    }

    if (sqlite3_close_v2(this->db) != SQLITE_OK) {
        EVLOG_error << "Error closing database file " << this->database_file_path << ": " << this->get_error_message();
        return false;
    }
    EVLOG_debug << "Successfully closed database: " << this->database_file_path;
    this->db = nullptr;
    return true;
}

bool Connection::execute_statement(const std::string& statement) {
    char* err_msg = nullptr;
    if (sqlite3_exec(this->db, statement.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
        EVLOG_error << "Could not execute statement \"" << statement
                    << "\": " << ((err_msg == nullptr) ? UNKNOWN_SQL_ERROR : err_msg);
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

const char* Connection::get_error_message() {
    return sqlite3_errmsg(this->db);
}

std::unique_ptr<TransactionInterface> Connection::begin_transaction() {
    return std::make_unique<DatabaseTransaction>(*this, std::unique_lock(this->transaction_mutex));
}

std::unique_ptr<TransactionInterface> Connection::begin_transaction_with_enforced_fkeys() {
    return std::make_unique<DatabaseTransaction>(*this, std::unique_lock(this->transaction_mutex), true);
}

std::unique_ptr<TransactionInterface> Connection::begin_transaction_with_deferred_fkeys() {
    return std::make_unique<DatabaseTransaction>(*this, std::unique_lock(this->transaction_mutex), true, true);
}

std::unique_ptr<StatementInterface> Connection::new_statement(const std::string& sql) {
    return std::make_unique<Statement>(this->db, sql);
}

bool Connection::clear_table(const std::string& table) {
    return this->execute_statement("DELETE FROM "s + table);
}

int64_t Connection::get_last_inserted_rowid() {
    return sqlite3_last_insert_rowid(this->db);
}

uint32_t Connection::get_user_version() {
    auto statement = this->new_statement("PRAGMA user_version");

    if (statement->step() != SQLITE_ROW) {
        throw std::runtime_error("Could not get user_version from database");
    }
    return statement->column_int(0);
}

void Connection::set_user_version(uint32_t version) {
    using namespace std::string_literals;

    if (!this->execute_statement("PRAGMA user_version = "s + std::to_string(version))) {
        throw std::runtime_error("Could not set user_version in database");
    }
}

} // namespace everest::db::sqlite
