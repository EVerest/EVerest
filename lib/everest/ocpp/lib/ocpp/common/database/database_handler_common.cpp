// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/common/database/database_handler_common.hpp>

#include <everest/database/sqlite/schema_updater.hpp>
#include <everest/logging.hpp>

using namespace everest::db;
using namespace everest::db::sqlite;

namespace ocpp::common {

DatabaseHandlerCommon::DatabaseHandlerCommon(std::unique_ptr<ConnectionInterface> database,
                                             const fs::path& sql_migration_files_path,
                                             std::uint32_t target_schema_version) noexcept :
    database(std::move(database)),
    sql_migration_files_path(sql_migration_files_path),
    target_schema_version(target_schema_version) {
}

void DatabaseHandlerCommon::open_connection() {
    SchemaUpdater updater{this->database.get()};

    if (!updater.apply_migration_files(this->sql_migration_files_path, target_schema_version)) {
        throw MigrationException("SQL migration failed");
    }

    if (!this->database->open_connection()) {
        throw ConnectionException("Could not open database at provided path.");
    }

    this->init_sql();
}

void DatabaseHandlerCommon::close_connection() {
    this->database->close_connection();
}

namespace {
/// SQL fragment selecting the rows of one connection. The primary connection is stored as NULL, and
/// NULL never compares equal with '=', so IS is required here.
constexpr const char* CONNECTION_PREDICATE = "CONNECTION IS @connection";

void bind_connection(everest::db::sqlite::StatementInterface& stmt, const std::optional<std::string>& connection) {
    if (connection.has_value()) {
        stmt.bind_text("@connection", connection.value(), everest::db::sqlite::SQLiteString::Transient);
    } else {
        stmt.bind_null("@connection");
    }
}
} // namespace

std::vector<DBTransactionMessage>
DatabaseHandlerCommon::get_message_queue_messages(const QueueType queue_type,
                                                  const std::optional<std::string>& connection) {
    std::vector<DBTransactionMessage> messages;

    const std::string table_name = queue_type == QueueType::Normal ? "NORMAL_QUEUE" : "TRANSACTION_QUEUE";

    const std::string sql = "SELECT UNIQUE_ID, MESSAGE, MESSAGE_TYPE, MESSAGE_ATTEMPTS, MESSAGE_TIMESTAMP FROM " +
                            table_name + " WHERE " + CONNECTION_PREDICATE;

    auto stmt = this->database->new_statement(sql);
    bind_connection(*stmt, connection);

    int status = SQLITE_ERROR;
    while ((status = stmt->step()) == SQLITE_ROW) {
        try {
            const std::string message = stmt->column_text(1);
            const std::string unique_id = stmt->column_text(0);
            const std::string message_type = stmt->column_text(2);
            const std::string message_timestamp = stmt->column_text(4);
            const int message_attempts = stmt->column_int(3);

            const json json_message = json::parse(message);

            DBTransactionMessage control_message;
            control_message.message_attempts = message_attempts;
            control_message.timestamp = ocpp::DateTime(message_timestamp);
            control_message.message_type = message_type;
            control_message.unique_id = unique_id;
            control_message.json_message = json_message;
            messages.push_back(std::move(control_message));
        } catch (const json::exception& e) {
            EVLOG_error << "json parse failed because: "
                        << "(" << e.what() << ")";
        } catch (const std::exception& e) {
            EVLOG_error << "can not get queued transaction message from database: "
                        << "(" << e.what() << ")";
        }
    }

    if (status != SQLITE_DONE) {
        EVLOG_error << "Could not get (all) queued transaction messages from database";
        throw QueryExecutionException(this->database->get_error_message());
    }

    return messages;
}

void DatabaseHandlerCommon::insert_message_queue_message(const DBTransactionMessage& db_message,
                                                         const QueueType queue_type,
                                                         const std::optional<std::string>& connection) {
    const std::string table_name = queue_type == QueueType::Normal ? "NORMAL_QUEUE" : "TRANSACTION_QUEUE";

    const std::string sql =
        "INSERT INTO " + table_name +
        " (UNIQUE_ID, MESSAGE, MESSAGE_TYPE, MESSAGE_ATTEMPTS, MESSAGE_TIMESTAMP, CONNECTION) VALUES "
        "(@unique_id, @message, @message_type, @message_attempts, @message_timestamp, @connection)";

    auto stmt = this->database->new_statement(sql);

    const std::string message = db_message.json_message.dump();
    stmt->bind_text("@unique_id", db_message.unique_id);
    stmt->bind_text("@message", message);
    stmt->bind_text("@message_type", db_message.message_type);
    stmt->bind_int("@message_attempts", db_message.message_attempts);
    stmt->bind_text("@message_timestamp", db_message.timestamp.to_rfc3339(), SQLiteString::Transient);
    bind_connection(*stmt, connection);

    if (stmt->step() != SQLITE_DONE) {
        throw QueryExecutionException(this->database->get_error_message());
    }
}

void DatabaseHandlerCommon::remove_message_queue_message(const std::string& unique_id, const QueueType queue_type,
                                                         const std::optional<std::string>& connection) {
    const std::string table_name = queue_type == QueueType::Normal ? "NORMAL_QUEUE" : "TRANSACTION_QUEUE";
    const std::string sql = "DELETE FROM " + table_name + " WHERE UNIQUE_ID = @unique_id AND " + CONNECTION_PREDICATE;

    auto stmt = this->database->new_statement(sql);

    stmt->bind_text("@unique_id", unique_id);
    bind_connection(*stmt, connection);

    if (stmt->step() != SQLITE_DONE) {
        throw QueryExecutionException(this->database->get_error_message());
    }
}

void DatabaseHandlerCommon::clear_message_queue(const QueueType queue_type,
                                                const std::optional<std::string>& connection) {
    const std::string table_name = queue_type == QueueType::Normal ? "NORMAL_QUEUE" : "TRANSACTION_QUEUE";
    // Scoped to one connection rather than clear_table(), which would drop every connection's rows.
    const std::string sql = "DELETE FROM " + table_name + " WHERE " + CONNECTION_PREDICATE;

    auto stmt = this->database->new_statement(sql);
    bind_connection(*stmt, connection);

    if (stmt->step() != SQLITE_DONE) {
        throw QueryExecutionException(this->database->get_error_message());
    }
}

} // namespace ocpp::common
