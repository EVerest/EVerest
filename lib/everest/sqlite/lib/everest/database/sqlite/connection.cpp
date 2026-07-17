// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>

#include <everest/database/exceptions.hpp>
#include <everest/database/sqlite/connection.hpp>
#include <everest/logging.hpp>

using namespace std::chrono_literals;
using namespace std::string_literals;

namespace {
constexpr auto UNKNOWN_SQL_ERROR{"<unknown>"};
}

namespace everest::db::sqlite {

/// \brief Foreign key handling for the duration of one transaction.
enum class FkMode {
    None,     ///< constraints are not enforced
    Enforced, ///< constraints are enforced on every statement
    Deferred  ///< constraints are enforced at commit time only
};

class DatabaseTransaction : public TransactionInterface {
private:
    Connection& database;
    std::unique_lock<std::timed_mutex> lock;
    bool is_active = false;

public:
    DatabaseTransaction(Connection& database, std::unique_lock<std::timed_mutex> lock, FkMode fk_mode) :
        database{database}, lock{std::move(lock)} {
        if (!this->lock.owns_lock()) {
            throw std::logic_error("DatabaseTransaction requires ownership of the transaction mutex");
        }

        // Both pragmas run *before* BEGIN. "PRAGMA foreign_keys" would be no-op while a
        // BEGIN/SAVEPOINT is pending. "PRAGMA defer_foreign_keys" applies to the next transaction
        // and resets at COMMIT/ROLLBACK; it allows constraints to be broken temporarily within the
        // transaction. Ordering them before BEGIN also means no exception can escape this
        // constructor while a transaction is open
        //
        // "PRAGMA foreign_keys" setting is sticky (survives past COMMIT/ROLLBACK), so
        // it is set explicitly ON or OFF
        if (fk_mode == FkMode::None) {
            if (!this->database.execute_statement("PRAGMA foreign_keys = OFF;")) {
                throw QueryExecutionException("Failed to disable foreign keys");
            }
        } else {
            if (!this->database.execute_statement("PRAGMA foreign_keys = ON;")) {
                throw QueryExecutionException("Failed to enable foreign keys");
            }
            if (fk_mode == FkMode::Deferred &&
                !this->database.execute_statement("PRAGMA defer_foreign_keys = ON;")) {
                throw QueryExecutionException("Failed to defer foreign keys");
            }
        }

        if (!this->database.execute_statement("BEGIN TRANSACTION")) {
            throw QueryExecutionException("Failed to begin transaction");
        }
        is_active = true;
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
        if (this->database.execute_statement("COMMIT TRANSACTION")) {
            this->lock.unlock();
            return;
        }
        // A failed COMMIT (e.g. SQLITE_BUSY) can leave the transaction open.
        // Resolve that state before releasing the lock.
        // The error message is captured first: the ROLLBACK below would overwrite it.
        const std::string error_message = this->database.get_error_message();
        if (this->database.has_pending_transaction() &&
            !this->database.execute_statement("ROLLBACK TRANSACTION")) {
            EVLOG_critical << "Failed to rollback after failed commit. Database may be in an inconsistent state: "
                           << this->database.get_error_message();
        }
        this->lock.unlock();
        throw QueryExecutionException(error_message);
    }

    void rollback() override {
        if (!is_active) {
            return;
        }
        is_active = false;
        if (this->database.execute_statement("ROLLBACK TRANSACTION")) {
            this->lock.unlock();
            return;
        }
        // A failed ROLLBACK can leave the transaction open and is not mitigated.
        const std::string error_message = this->database.get_error_message();
        if (this->database.has_pending_transaction()) {
            EVLOG_critical << "Transaction still pending after failed rollback. Database may be in an inconsistent "
                              "state: "
                           << error_message;
        }
        this->lock.unlock();
        throw QueryExecutionException(error_message);
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

bool Connection::has_pending_transaction() const {
    return sqlite3_get_autocommit(this->db) == 0;
}

std::unique_ptr<TransactionInterface> Connection::begin_transaction() {
    return std::make_unique<DatabaseTransaction>(*this, std::unique_lock(this->transaction_mutex), FkMode::None);
}

std::unique_ptr<TransactionInterface> Connection::begin_transaction_with_enforced_fkeys() {
    return std::make_unique<DatabaseTransaction>(*this, std::unique_lock(this->transaction_mutex), FkMode::Enforced);
}

std::unique_ptr<TransactionInterface> Connection::begin_transaction_with_deferred_fkeys() {
    return std::make_unique<DatabaseTransaction>(*this, std::unique_lock(this->transaction_mutex), FkMode::Deferred);
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
