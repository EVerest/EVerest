// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "session_store.hpp"

#include <algorithm>
#include <cstddef>
#include <sqlite3.h>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <everest/database/exceptions.hpp>
#include <everest/database/sqlite/schema_updater.hpp>
#include <everest/logging.hpp>

using everest::db::sqlite::ConnectionInterface;
using everest::db::sqlite::SQLiteString;
using everest::db::sqlite::StatementInterface;

namespace module::storage {

namespace {

/// \brief Column indices of the shared SELECT column list, must stay in sync with SELECT_COLUMNS
enum class SessionColumnIndex : int {
    COL_SESSION_ID = 0,
    COL_OCPP_TRANSACTION_ID,
    COL_OCPP_TRANSACTION_TIMESTAMP_START,
    COL_OCPP_TRANSACTION_TIMESTAMP_STOP,
    COL_EVSE_ID,
    COL_EVSE_ID_STRING,
    COL_CONNECTOR_ID,
    COL_STATE,
    COL_TIMESTAMP_START,
    COL_START_REASON,
    COL_TIMESTAMP_STOP,
    COL_TRANSACTION_TIMESTAMP_START,
    COL_TRANSACTION_TIMESTAMP_STOP,
    COL_ENERGY_WH_IMPORT_START,
    COL_ENERGY_WH_IMPORT_STOP,
    COL_ID_TOKEN_HASH,
    COL_ID_TOKEN_TYPE,
    COL_AUTHORIZATION_TYPE,
    COL_STOP_REASON,
    COL_SIGNED_METER_VALUE_START,
    COL_SIGNED_METER_VALUE_STOP,
    COL_COST,
    COL_ID,
};

constexpr std::string_view SELECT_COLUMNS{
    "SESSION_ID, OCPP_TRANSACTION_ID, OCPP_TRANSACTION_TIMESTAMP_START, OCPP_TRANSACTION_TIMESTAMP_STOP, EVSE_ID, "
    "EVSE_ID_STRING, CONNECTOR_ID, STATE, TIMESTAMP_START, START_REASON, TIMESTAMP_STOP, TRANSACTION_TIMESTAMP_START, "
    "TRANSACTION_TIMESTAMP_STOP, ENERGY_WH_IMPORT_START, ENERGY_WH_IMPORT_STOP, ID_TOKEN_HASH, ID_TOKEN_TYPE, "
    "AUTHORIZATION_TYPE, STOP_REASON, SIGNED_METER_VALUE_START, SIGNED_METER_VALUE_STOP, COST, ID"};

constexpr std::int32_t DEFAULT_PAGE_LIMIT{100};
constexpr std::int32_t MAX_PAGE_LIMIT{500};
/// \brief Approximate serialized size a page may reach before it is cut short
constexpr std::size_t PAGE_BYTE_BUDGET{256 * 1024};

constexpr int to_int(SessionColumnIndex column) {
    return static_cast<int>(column);
}

/// \brief Checks that \p timestamp is readable by SQLite's julianday(), which the filter queries compare with
bool is_sqlite_timestamp(everest::db::sqlite::ConnectionInterface& connection, const std::string& timestamp) {
    auto statement = connection.new_statement("SELECT julianday(@timestamp)");
    statement->bind_text("@timestamp", timestamp, SQLiteString::Transient);
    return statement->step() == SQLITE_ROW and statement->column_type(0) != SQLITE_NULL;
}

std::optional<std::string> column_text(StatementInterface& statement, SessionColumnIndex column) {
    return statement.column_text_nullable(to_int(column));
}

template <typename T> std::optional<T> column_json(StatementInterface& statement, SessionColumnIndex column) {
    const auto text = column_text(statement, column);
    if (not text.has_value()) {
        return std::nullopt;
    }
    return nlohmann::json::parse(text.value()).get<T>();
}

/// \brief Builds the nested transaction from the current row of \p statement
/// \throws nlohmann::json::exception or std::out_of_range on malformed content
types::session_storage::Transaction row_to_transaction(StatementInterface& statement) {
    types::session_storage::Transaction transaction{};

    transaction.timestamp_start = statement.column_text(to_int(SessionColumnIndex::COL_TRANSACTION_TIMESTAMP_START));
    transaction.timestamp_stop = column_text(statement, SessionColumnIndex::COL_TRANSACTION_TIMESTAMP_STOP);

    // Written together with the transaction start timestamp, so it is always present here
    transaction.energy_Wh_import_start =
        static_cast<float>(statement.column_double(to_int(SessionColumnIndex::COL_ENERGY_WH_IMPORT_START)));
    if (statement.column_type(to_int(SessionColumnIndex::COL_ENERGY_WH_IMPORT_STOP)) != SQLITE_NULL) {
        transaction.energy_Wh_import_stop =
            static_cast<float>(statement.column_double(to_int(SessionColumnIndex::COL_ENERGY_WH_IMPORT_STOP)));
    }

    transaction.id_token_hash = column_text(statement, SessionColumnIndex::COL_ID_TOKEN_HASH);

    const auto id_token_type = column_text(statement, SessionColumnIndex::COL_ID_TOKEN_TYPE);
    if (id_token_type.has_value()) {
        transaction.id_token_type = types::authorization::string_to_id_token_type(id_token_type.value());
    }

    const auto authorization_type = column_text(statement, SessionColumnIndex::COL_AUTHORIZATION_TYPE);
    if (authorization_type.has_value()) {
        transaction.authorization_type = types::authorization::string_to_authorization_type(authorization_type.value());
    }

    const auto stop_reason = column_text(statement, SessionColumnIndex::COL_STOP_REASON);
    if (stop_reason.has_value()) {
        transaction.stop_reason = types::evse_manager::string_to_stop_transaction_reason(stop_reason.value());
    }

    transaction.signed_meter_value_start =
        column_json<types::units_signed::SignedMeterValue>(statement, SessionColumnIndex::COL_SIGNED_METER_VALUE_START);
    transaction.signed_meter_value_stop =
        column_json<types::units_signed::SignedMeterValue>(statement, SessionColumnIndex::COL_SIGNED_METER_VALUE_STOP);

    return transaction;
}

/// \brief Builds a record from the current row of \p statement, which must select SELECT_COLUMNS
/// \throws nlohmann::json::exception or std::out_of_range on malformed content
types::session_storage::Session row_to_session(StatementInterface& statement) {
    types::session_storage::Session session{};

    session.session_id = statement.column_text(to_int(SessionColumnIndex::COL_SESSION_ID));
    session.ocpp_transaction_id = column_text(statement, SessionColumnIndex::COL_OCPP_TRANSACTION_ID);
    session.ocpp_transaction_timestamp_start =
        column_text(statement, SessionColumnIndex::COL_OCPP_TRANSACTION_TIMESTAMP_START);
    session.ocpp_transaction_timestamp_stop =
        column_text(statement, SessionColumnIndex::COL_OCPP_TRANSACTION_TIMESTAMP_STOP);
    session.evse_id = statement.column_int(to_int(SessionColumnIndex::COL_EVSE_ID));
    session.evse_id_string = statement.column_text(to_int(SessionColumnIndex::COL_EVSE_ID_STRING));
    session.connector_id = statement.column_int(to_int(SessionColumnIndex::COL_CONNECTOR_ID));
    session.state =
        types::session_storage::string_to_session_state(statement.column_text(to_int(SessionColumnIndex::COL_STATE)));
    session.timestamp_start = statement.column_text(to_int(SessionColumnIndex::COL_TIMESTAMP_START));
    session.timestamp_stop = column_text(statement, SessionColumnIndex::COL_TIMESTAMP_STOP);
    session.start_reason = types::evse_manager::string_to_start_session_reason(
        statement.column_text(to_int(SessionColumnIndex::COL_START_REASON)));

    if (statement.column_type(to_int(SessionColumnIndex::COL_TRANSACTION_TIMESTAMP_START)) != SQLITE_NULL) {
        session.transaction = row_to_transaction(statement);
    }

    session.cost = column_json<types::session_cost::SessionCost>(statement, SessionColumnIndex::COL_COST);

    return session;
}

void bind_optional_text(StatementInterface& statement, const std::string& parameter,
                        const std::optional<std::string>& value) {
    if (not value.has_value()) {
        statement.bind_null(parameter);
        return;
    }
    statement.bind_text(parameter, value.value(), SQLiteString::Transient);
}

template <typename T>
void bind_optional_json(StatementInterface& statement, const std::string& parameter, const std::optional<T>& value) {
    if (not value.has_value()) {
        statement.bind_null(parameter);
        return;
    }
    statement.bind_text(parameter, nlohmann::json(value.value()).dump(), SQLiteString::Transient);
}

std::optional<std::string> select_epoch(ConnectionInterface& connection) {
    auto statement = connection.new_statement("SELECT VALUE FROM METADATA WHERE KEY='EPOCH'");
    if (statement->step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return statement->column_text(0);
}

/// \brief Marks Open records of \p evse_id older than \p new_id Stale
void mark_older_open_stale(ConnectionInterface& connection, std::int32_t evse_id, std::int64_t new_id) {
    static const std::string sql =
        "UPDATE SESSIONS SET STATE='Stale' WHERE EVSE_ID=@evse_id AND STATE='Open' AND ID<@new_id";

    auto statement = connection.new_statement(sql);
    statement->bind_int("@evse_id", evse_id);
    statement->bind_int64("@new_id", new_id);

    if (statement->step() != SQLITE_DONE) {
        throw everest::db::QueryExecutionException(connection.get_error_message());
    }
}

/// \brief Deletes all but the newest \p max_sessions records
void prune(ConnectionInterface& connection, int max_sessions) {
    static const std::string sql =
        "DELETE FROM SESSIONS WHERE ID IN (SELECT ID FROM SESSIONS ORDER BY ID DESC LIMIT -1 OFFSET @max_sessions)";

    auto statement = connection.new_statement(sql);
    statement->bind_int("@max_sessions", max_sessions);

    if (statement->step() != SQLITE_DONE) {
        throw everest::db::QueryExecutionException(connection.get_error_message());
    }
}

std::optional<types::session_storage::Session> select_one(ConnectionInterface& connection, const std::string& sql,
                                                          const std::string& parameter, const std::string& value) {
    try {
        auto statement = connection.new_statement(sql);
        statement->bind_text(parameter, value, SQLiteString::Transient);

        if (statement->step() != SQLITE_ROW) {
            return std::nullopt;
        }
        return row_to_session(*statement);
    } catch (const std::exception& e) {
        EVLOG_error << "Could not read the session record for " << parameter << " " << value << ": " << e.what();
        return std::nullopt;
    }
}

/// \brief The record id encoded in \p continuation_token within \p epoch, 0 when the
///        token is absent, invalid or from another database epoch
std::int64_t parse_continuation_token(const std::string& epoch, const std::optional<std::string>& continuation_token) {
    if (not continuation_token.has_value()) {
        return 0;
    }
    const auto& token = continuation_token.value();

    // All record ids are >= 1, so 0 starts the iteration from the beginning
    const auto restart = [&token]() -> std::int64_t {
        EVLOG_warning << "Restarting the session iteration, the continuation token " << token
                      << " is malformed or outdated";
        return 0;
    };

    const auto separator = token.find(':');
    if (separator == std::string::npos or token.compare(0, separator, epoch) != 0) {
        return restart();
    }

    const auto id_text = token.substr(separator + 1);
    std::size_t parsed_characters{0};
    std::int64_t id{0};
    try {
        id = std::stoll(id_text, &parsed_characters);
    } catch (const std::exception&) {
        return restart();
    }

    if (parsed_characters != id_text.size() or id <= 0) {
        return restart();
    }
    return id;
}

} // namespace

SessionStore::SessionStore(std::unique_ptr<everest::db::sqlite::ConnectionInterface> connection,
                           std::filesystem::path migration_files_path, std::uint32_t target_schema_version,
                           int max_sessions) :
    m_migration_files_path{std::move(migration_files_path)},
    m_target_schema_version{target_schema_version},
    m_max_sessions{max_sessions},
    m_database{Database{std::move(connection), std::string{}}} {
}

bool SessionStore::open() {
    auto database = m_database.handle();
    try {
        everest::db::sqlite::SchemaUpdater updater{database->connection.get()};
        if (not updater.apply_migration_files(m_migration_files_path, m_target_schema_version)) {
            EVLOG_error << "Could not apply the migration files from " << m_migration_files_path;
            return false;
        }

        // The schema updater closes the connection when it is done, so open it again
        if (not database->connection->open_connection()) {
            EVLOG_error << "Could not open the session storage database connection";
            return false;
        }

        auto epoch = select_epoch(*database->connection);
        if (not epoch.has_value()) {
            if (not database->connection->execute_statement(
                    "INSERT INTO METADATA (KEY, VALUE) VALUES ('EPOCH', lower(hex(randomblob(8))))")) {
                EVLOG_error << "Could not store the session storage database epoch: "
                            << database->connection->get_error_message();
                return false;
            }
            epoch = select_epoch(*database->connection);
        }

        if (not epoch.has_value()) {
            EVLOG_error << "Could not read the session storage database epoch";
            return false;
        }

        database->epoch = epoch.value();
        return true;
    } catch (const std::exception& e) {
        EVLOG_error << "Could not open the session storage database: " << e.what();
        return false;
    }
}

bool SessionStore::store_session_started(const SessionStart& session) {
    auto database = m_database.handle();
    try {
        auto database_transaction = database->connection->begin_transaction();

        static const std::string sql =
            "INSERT INTO SESSIONS (SESSION_ID, EVSE_ID, EVSE_ID_STRING, CONNECTOR_ID, STATE, TIMESTAMP_START, "
            "START_REASON) "
            "VALUES (@session_id, @evse_id, @evse_id_string, @connector_id, 'Open', @timestamp_start, @start_reason)";

        auto statement = database->connection->new_statement(sql);

        statement->bind_text("@session_id", session.session_id, SQLiteString::Transient);
        statement->bind_int("@evse_id", session.evse_id);
        statement->bind_text("@evse_id_string", session.evse_id_string, SQLiteString::Transient);
        statement->bind_int("@connector_id", session.connector_id);
        statement->bind_text("@timestamp_start", session.timestamp_start, SQLiteString::Transient);
        statement->bind_text("@start_reason", types::evse_manager::start_session_reason_to_string(session.start_reason),
                             SQLiteString::Transient);

        if (statement->step() != SQLITE_DONE) {
            // Defensive: a duplicate session id must leave the existing record untouched
            EVLOG_warning << "Could not store the start of session " << session.session_id << ": "
                          << database->connection->get_error_message();
            return false;
        }

        const auto new_id = database->connection->get_last_inserted_rowid();

        mark_older_open_stale(*database->connection, session.evse_id, new_id);
        prune(*database->connection, m_max_sessions);

        database_transaction->commit();
        return true;
    } catch (const std::exception& e) {
        EVLOG_error << "Could not store the start of session " << session.session_id << ": " << e.what();
        return false;
    }
}

bool SessionStore::store_transaction_started(const TransactionStart& transaction) {
    auto database = m_database.handle();
    try {
        static const std::string sql =
            "UPDATE SESSIONS SET TRANSACTION_TIMESTAMP_START=@timestamp_start, "
            "ENERGY_WH_IMPORT_START=@energy_start, ID_TOKEN_HASH=@id_token_hash, ID_TOKEN_TYPE=@id_token_type, "
            "AUTHORIZATION_TYPE=@authorization_type, SIGNED_METER_VALUE_START=@signed_meter_value_start "
            "WHERE SESSION_ID=@session_id AND STATE='Open' AND TRANSACTION_TIMESTAMP_START IS NULL";

        auto statement = database->connection->new_statement(sql);

        statement->bind_text("@timestamp_start", transaction.timestamp_start, SQLiteString::Transient);
        statement->bind_double("@energy_start", transaction.energy_Wh_import_start);
        bind_optional_text(*statement, "@id_token_hash", transaction.id_token_hash);

        std::optional<std::string> id_token_type{};
        if (transaction.id_token_type.has_value()) {
            id_token_type = types::authorization::id_token_type_to_string(transaction.id_token_type.value());
        }
        bind_optional_text(*statement, "@id_token_type", id_token_type);

        std::optional<std::string> authorization_type{};
        if (transaction.authorization_type.has_value()) {
            authorization_type =
                types::authorization::authorization_type_to_string(transaction.authorization_type.value());
        }
        bind_optional_text(*statement, "@authorization_type", authorization_type);
        bind_optional_json(*statement, "@signed_meter_value_start", transaction.signed_meter_value_start);
        statement->bind_text("@session_id", transaction.session_id, SQLiteString::Transient);

        if (statement->step() != SQLITE_DONE) {
            EVLOG_error << "Could not store the transaction of session " << transaction.session_id << ": "
                        << database->connection->get_error_message();
            return false;
        }

        if (statement->changes() == 0) {
            EVLOG_warning << "No open session record without a transaction for session " << transaction.session_id
                          << " to start a transaction on";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        EVLOG_error << "Could not store the transaction of session " << transaction.session_id << ": " << e.what();
        return false;
    }
}

bool SessionStore::store_transaction_finished(const TransactionFinish& finish) {
    auto database = m_database.handle();
    try {
        std::string sql = "UPDATE SESSIONS SET TRANSACTION_TIMESTAMP_STOP=@timestamp_stop, "
                          "ENERGY_WH_IMPORT_STOP=@energy_stop, STOP_REASON=@stop_reason, "
                          "SIGNED_METER_VALUE_STOP=@signed_meter_value_stop, "
                          "SIGNED_METER_VALUE_START=COALESCE(SIGNED_METER_VALUE_START, @signed_meter_value_start)";
        if (finish.closes_session) {
            sql += ", STATE='Finished', TIMESTAMP_STOP=@timestamp_stop";
        }
        sql += " WHERE SESSION_ID=@session_id AND STATE='Open' AND TRANSACTION_TIMESTAMP_START IS NOT NULL AND "
               "TRANSACTION_TIMESTAMP_STOP IS NULL";

        auto statement = database->connection->new_statement(sql);

        statement->bind_text("@timestamp_stop", finish.timestamp_stop, SQLiteString::Transient);
        statement->bind_double("@energy_stop", finish.energy_Wh_import_stop);

        std::optional<std::string> stop_reason{};
        if (finish.stop_reason.has_value()) {
            stop_reason = types::evse_manager::stop_transaction_reason_to_string(finish.stop_reason.value());
        }
        bind_optional_text(*statement, "@stop_reason", stop_reason);
        bind_optional_json(*statement, "@signed_meter_value_stop", finish.signed_meter_value_stop);
        bind_optional_json(*statement, "@signed_meter_value_start", finish.signed_meter_value_start);
        statement->bind_text("@session_id", finish.session_id, SQLiteString::Transient);

        if (statement->step() != SQLITE_DONE) {
            EVLOG_error << "Could not finish the transaction of session " << finish.session_id << ": "
                        << database->connection->get_error_message();
            return false;
        }

        if (statement->changes() == 0) {
            EVLOG_warning << "No running transaction of session " << finish.session_id << " to finish";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        EVLOG_error << "Could not finish the transaction of session " << finish.session_id << ": " << e.what();
        return false;
    }
}

bool SessionStore::store_session_finished(const std::string& session_id, const std::string& timestamp_stop) {
    auto database = m_database.handle();
    try {
        static const std::string sql = "UPDATE SESSIONS SET STATE='Finished', TIMESTAMP_STOP=@timestamp_stop "
                                       "WHERE SESSION_ID=@session_id AND STATE='Open'";

        auto statement = database->connection->new_statement(sql);
        statement->bind_text("@timestamp_stop", timestamp_stop, SQLiteString::Transient);
        statement->bind_text("@session_id", session_id, SQLiteString::Transient);

        if (statement->step() != SQLITE_DONE) {
            EVLOG_error << "Could not finish session " << session_id << ": "
                        << database->connection->get_error_message();
            return false;
        }

        if (statement->changes() == 0) {
            EVLOG_warning << "No open session record for session " << session_id << " to finish";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        EVLOG_error << "Could not finish session " << session_id << ": " << e.what();
        return false;
    }
}

bool SessionStore::store_ocpp_transaction_event(const OcppTransactionEvent& event) {
    auto database = m_database.handle();
    try {
        // An event without an id must not clear the id a previous event delivered
        std::string sql = "UPDATE SESSIONS SET OCPP_TRANSACTION_ID=COALESCE(@ocpp_transaction_id, OCPP_TRANSACTION_ID)";
        if (event.transaction_event == types::ocpp::TransactionEvent::Started) {
            sql += ", OCPP_TRANSACTION_TIMESTAMP_START=@timestamp";
        }
        if (event.transaction_event == types::ocpp::TransactionEvent::Ended) {
            sql += ", OCPP_TRANSACTION_TIMESTAMP_STOP=@timestamp";
        }
        sql += " WHERE SESSION_ID=@session_id";

        auto statement = database->connection->new_statement(sql);
        bind_optional_text(*statement, "@ocpp_transaction_id", event.transaction_id);
        if (event.transaction_event != types::ocpp::TransactionEvent::Updated) {
            statement->bind_text("@timestamp", event.timestamp, SQLiteString::Transient);
        }
        statement->bind_text("@session_id", event.session_id, SQLiteString::Transient);

        if (statement->step() != SQLITE_DONE) {
            EVLOG_error << "Could not store the OCPP transaction event of session " << event.session_id << ": "
                        << database->connection->get_error_message();
            return false;
        }

        if (statement->changes() == 0) {
            EVLOG_warning << "No session record for session " << event.session_id
                          << " to attach the OCPP transaction event to";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        EVLOG_error << "Could not store the OCPP transaction event of session " << event.session_id << ": " << e.what();
        return false;
    }
}

bool SessionStore::update_cost(const std::string& session_id, const types::session_cost::SessionCost& cost) {
    auto database = m_database.handle();
    try {
        static const std::string sql = "UPDATE SESSIONS SET COST=@cost WHERE SESSION_ID=@session_id";

        auto statement = database->connection->new_statement(sql);
        statement->bind_text("@cost", nlohmann::json(cost).dump(), SQLiteString::Transient);
        statement->bind_text("@session_id", session_id, SQLiteString::Transient);

        if (statement->step() != SQLITE_DONE) {
            EVLOG_error << "Could not store the cost of session " << session_id << ": "
                        << database->connection->get_error_message();
            return false;
        }

        if (statement->changes() == 0) {
            EVLOG_warning << "No session record for session " << session_id << " to attach the cost to";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        EVLOG_error << "Could not store the cost of session " << session_id << ": " << e.what();
        return false;
    }
}

types::session_storage::SessionList
SessionStore::get_sessions(const types::session_storage::GetSessionsRequest& request) {
    auto database = m_database.handle();
    types::session_storage::SessionList list{};
    try {
        const auto effective_limit = std::clamp(request.limit.value_or(DEFAULT_PAGE_LIMIT), 1, MAX_PAGE_LIMIT);
        const auto cursor = parse_continuation_token(database->epoch, request.continuation_token);
        const auto filter = request.filter.value_or(types::session_storage::SessionFilter{});

        std::optional<std::string> started_after{};
        if (filter.started_after.has_value()) {
            if (is_sqlite_timestamp(*database->connection, filter.started_after.value())) {
                started_after = filter.started_after;
            } else {
                // Applying it anyway would compare against NULL and match nothing
                EVLOG_warning << "Ignoring the started_after filter, '" << filter.started_after.value()
                              << "' is not a readable timestamp";
            }
        }

        std::string sql = "SELECT " + std::string{SELECT_COLUMNS} + " FROM SESSIONS WHERE ID > @cursor";
        if (filter.state.has_value()) {
            sql += " AND STATE = @state";
        }
        if (filter.evse_id.has_value()) {
            sql += " AND EVSE_ID = @evse_id";
        }
        if (started_after.has_value()) {
            // Chronological instead of lexicographic compare, records may store any RFC3339 spelling
            sql += " AND julianday(TIMESTAMP_START) > julianday(@started_after)";
        }
        sql += " ORDER BY ID ASC LIMIT @limit";

        auto statement = database->connection->new_statement(sql);
        statement->bind_int64("@cursor", cursor);
        if (filter.state.has_value()) {
            statement->bind_text("@state", types::session_storage::session_state_to_string(filter.state.value()),
                                 SQLiteString::Transient);
        }
        if (filter.evse_id.has_value()) {
            statement->bind_int("@evse_id", filter.evse_id.value());
        }
        if (started_after.has_value()) {
            statement->bind_text("@started_after", started_after.value(), SQLiteString::Transient);
        }
        statement->bind_int("@limit", effective_limit);

        std::int64_t last_seen_id{0};
        std::int32_t processed_rows{0};
        std::size_t page_bytes{0};
        bool budget_exceeded{false};

        while (statement->step() == SQLITE_ROW) {
            last_seen_id = statement->column_int64(to_int(SessionColumnIndex::COL_ID));
            ++processed_rows;

            try {
                const auto session = row_to_session(*statement);
                page_bytes += nlohmann::json(session).dump().size();
                list.sessions.push_back(session);
            } catch (const std::exception& e) {
                // A single unreadable record must not hide all the others
                EVLOG_error << "Skipping unreadable session record: " << e.what();
                continue;
            }

            if (page_bytes > PAGE_BYTE_BUDGET) {
                budget_exceeded = true;
                break;
            }
        }

        if (budget_exceeded or processed_rows == effective_limit) {
            list.continuation_token = database->epoch + ":" + std::to_string(last_seen_id);
        }
        return list;
    } catch (const std::exception& e) {
        EVLOG_error << "Could not read the stored sessions: " << e.what();
        return types::session_storage::SessionList{};
    }
}

std::optional<types::session_storage::Session>
SessionStore::get_session(const types::session_storage::SessionIdentifier& identifier) {
    auto database = m_database.handle();

    if (identifier.session_id.has_value()) {
        return select_one(*database->connection,
                          "SELECT " + std::string{SELECT_COLUMNS} + " FROM SESSIONS WHERE SESSION_ID=@session_id",
                          "@session_id", identifier.session_id.value());
    }

    if (identifier.ocpp_transaction_id.has_value()) {
        // The same OCPP transaction id can occur more than once, the newest record wins
        return select_one(*database->connection,
                          "SELECT " + std::string{SELECT_COLUMNS} +
                              " FROM SESSIONS WHERE OCPP_TRANSACTION_ID=@ocpp_transaction_id ORDER BY ID DESC LIMIT 1",
                          "@ocpp_transaction_id", identifier.ocpp_transaction_id.value());
    }

    EVLOG_warning << "Cannot look up a session record without an identifier";
    return std::nullopt;
}

int SessionStore::clear_sessions() {
    auto database = m_database.handle();
    try {
        auto statement = database->connection->new_statement("DELETE FROM SESSIONS");
        if (statement->step() != SQLITE_DONE) {
            EVLOG_error << "Could not clear the stored sessions: " << database->connection->get_error_message();
            return 0;
        }
        return statement->changes();
    } catch (const std::exception& e) {
        EVLOG_error << "Could not clear the stored sessions: " << e.what();
        return 0;
    }
}

} // namespace module::storage
