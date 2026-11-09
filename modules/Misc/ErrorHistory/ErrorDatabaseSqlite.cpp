// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ErrorDatabaseSqlite.hpp"

#include <everest/database/exceptions.hpp>
#include <everest/database/sqlite/connection.hpp>
#include <everest/database/sqlite/statement.hpp>
#include <everest/logging.hpp>

#include <utils/date.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace module {

namespace {
void bind_values(everest::db::sqlite::StatementInterface& stmt, const std::vector<std::string>& values) {
    for (std::size_t i = 0; i < values.size(); i++) {
        stmt.bind_text(static_cast<int>(i + 1), values[i], everest::db::sqlite::SQLiteString::Transient);
    }
}

/// thrown when the database can not be used with this schema and has to be reset
struct IncompatibleSchema : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

const std::set<std::string> required_columns = {
    "uuid",      "type",     "description", "message",  "origin_module", "origin_implementation",
    "timestamp", "severity", "state",       "sub_type", "vendor_id"};

/// nullable columns that are added in place to databases created without them
const std::vector<std::pair<std::string, std::string>> added_columns = {{"trigger_value_text_repr", "TEXT"}};

std::set<std::string> get_column_names(everest::db::sqlite::ConnectionInterface& db) {
    auto stmt = db.new_statement("PRAGMA table_info(errors);");
    std::set<std::string> columns;
    int status;
    while ((status = stmt->step()) == SQLITE_ROW) {
        columns.insert(std::get<std::string>(stmt->column_variant("name")));
    }
    if (status != SQLITE_DONE) {
        throw everest::db::QueryExecutionException(db.get_error_message());
    }
    return columns;
}

/// columns that need a value in every row: NOT NULL ones without a default
std::set<std::string> get_columns_needing_a_value(everest::db::sqlite::ConnectionInterface& db) {
    auto stmt = db.new_statement("PRAGMA table_info(errors);");
    std::set<std::string> columns;
    int status;
    while ((status = stmt->step()) == SQLITE_ROW) {
        const auto not_null = stmt->column_variant("notnull");
        const auto default_value = stmt->column_variant("dflt_value");
        const bool is_not_null = std::holds_alternative<int64_t>(not_null) and std::get<int64_t>(not_null) != 0;
        const bool has_default = !std::holds_alternative<std::monostate>(default_value) and
                                 default_value != decltype(default_value){std::string("NULL")};
        if (is_not_null and !has_default) {
            columns.insert(std::get<std::string>(stmt->column_variant("name")));
        }
    }
    if (status != SQLITE_DONE) {
        throw everest::db::QueryExecutionException(db.get_error_message());
    }
    return columns;
}
} // namespace

ErrorDatabaseSqlite::ErrorDatabaseSqlite(const fs::path& db_path_, const bool reset_) :
    db_path(fs::absolute(db_path_)) {
    BOOST_LOG_FUNCTION();
    std::lock_guard<std::mutex> lock(this->db_mutex);

    bool reset = reset_ || !fs::exists(this->db_path);
    if (reset) {
        EVLOG_info << "Resetting database";
        this->reset_database();
    } else {
        EVLOG_info << "Using database at " << this->db_path;
        try {
            this->check_database();
        } catch (const IncompatibleSchema& e) {
            EVLOG_error << "Incompatible database schema: " << e.what();
            EVLOG_info << "Resetting database";
            this->reset_database();
        } catch (const std::exception& e) {
            EVLOG_error << "Error checking or migrating database, keeping it as it is: " << e.what();
        }
    }
}

void ErrorDatabaseSqlite::check_database() {
    BOOST_LOG_FUNCTION();
    EVLOG_info << "Checking database";
    if (fs::is_regular_file(this->db_path) and fs::file_size(this->db_path) > 0) {
        constexpr std::string_view sqlite_header("SQLite format 3\0", 16);
        std::array<char, sqlite_header.size()> header{};
        std::ifstream file(this->db_path, std::ios::binary);
        file.read(header.data(), header.size());
        if (!file.is_open() or file.bad()) {
            throw std::runtime_error("Could not read database file");
        }
        if (std::string_view(header.data(), file.gcount()) != sqlite_header) {
            throw IncompatibleSchema("File is not an SQLite database");
        }
    }
    auto db = std::make_shared<everest::db::sqlite::Connection>(this->db_path);
    if (!db->open_connection()) {
        EVLOG_error << "Error opening database";
        throw everest::db::ConnectionException(db->get_error_message());
    }
    std::string sql = "SELECT name";
    sql += " FROM sqlite_schema";
    sql += " WHERE type = 'table' AND name NOT LIKE 'sqlite_%';";
    auto stmt = db->new_statement(sql);
    bool has_errors_table = false;
    int status;
    while ((status = stmt->step()) == SQLITE_ROW) {
        std::string table_name = stmt->column_text(0);
        if (table_name == "errors") {
            has_errors_table = true;
            EVLOG_debug << "Found errors table";
        } else {
            EVLOG_warning << "Found unknown table: " << table_name;
        }
    }
    if (status != SQLITE_DONE) {
        throw everest::db::QueryExecutionException(db->get_error_message());
    }
    if (!has_errors_table) {
        throw IncompatibleSchema("Database does not contain errors table");
    }
    const auto columns = get_column_names(*db);
    if (!std::includes(columns.begin(), columns.end(), required_columns.begin(), required_columns.end())) {
        throw IncompatibleSchema("Errors table does not contain all required columns");
    }
    for (const auto& column : get_columns_needing_a_value(*db)) {
        if (required_columns.count(column) == 0) {
            throw IncompatibleSchema("Errors table contains the unknown column " + column + " that needs a value");
        }
    }
    std::set<std::string> known_columns = required_columns;
    std::vector<std::pair<std::string, std::string>> missing_columns;
    for (const auto& column : added_columns) {
        known_columns.insert(column.first);
        if (columns.count(column.first) == 0) {
            missing_columns.push_back(column);
        }
    }
    for (const auto& column : columns) {
        if (known_columns.count(column) == 0) {
            EVLOG_warning << "Ignoring unknown column " << column << " in errors table";
        }
    }
    if (missing_columns.empty()) {
        return;
    }
    auto transaction = db->begin_transaction();
    for (const auto& [name, type] : missing_columns) {
        EVLOG_info << "Adding column " << name << " to errors table";
        if (!db->execute_statement("ALTER TABLE errors ADD COLUMN " + name + " " + type + ";")) {
            throw everest::db::QueryExecutionException(db->get_error_message());
        }
    }
    transaction->commit();
}

void ErrorDatabaseSqlite::reset_database() {
    BOOST_LOG_FUNCTION();
    try {
        fs::path database_directory = this->db_path.parent_path();
        if (!fs::exists(database_directory)) {
            fs::create_directories(database_directory);
        }
        for (const auto* suffix : {"", "-wal", "-shm", "-journal"}) {
            fs::remove(this->db_path.string() + suffix);
        }
        everest::db::sqlite::Connection db(this->db_path);
        if (!db.open_connection()) {
            EVLOG_error << "Error opening database during reset";
            throw everest::db::ConnectionException(db.get_error_message());
        }
        std::string sql = "CREATE TABLE errors("
                          "uuid TEXT PRIMARY      KEY     NOT NULL,"
                          "type                   TEXT    NOT NULL,"
                          "description            TEXT    NOT NULL,"
                          "message                TEXT    NOT NULL,"
                          "origin_module          TEXT    NOT NULL,"
                          "origin_implementation  TEXT    NOT NULL,"
                          "timestamp              TEXT    NOT NULL,"
                          "severity               TEXT    NOT NULL,"
                          "state                  TEXT    NOT NULL,"
                          "sub_type               TEXT    NOT NULL,"
                          "vendor_id              TEXT    NOT NULL";
        for (const auto& [name, type] : added_columns) {
            sql += ", " + name + " " + type;
        }
        sql += ");";

        if (!db.execute_statement(sql)) {
            EVLOG_error << "Error creating database during reset";
            throw everest::db::QueryExecutionException(db.get_error_message());
        }
    } catch (const std::exception& e) {
        EVLOG_error << "Error resetting the database: " << e.what();
    }
}

void ErrorDatabaseSqlite::add_error(Everest::error::ErrorPtr error) {
    std::lock_guard<std::mutex> lock(this->db_mutex);
    this->add_error_without_mutex(error);
}

void ErrorDatabaseSqlite::add_error_without_mutex(Everest::error::ErrorPtr error) {
    BOOST_LOG_FUNCTION();
    try {
        everest::db::sqlite::Connection db(this->db_path);
        if (!db.open_connection()) {
            EVLOG_error << "Error opening database";
            throw everest::db::ConnectionException(db.get_error_message());
        }
        // only added columns the table has, so errors are still stored while a migration is pending
        const auto columns = get_column_names(db);
        std::string sql = "INSERT INTO errors(uuid, type, description, message, origin_module, origin_implementation, "
                          "timestamp, severity, state, sub_type, vendor_id";
        std::string values = "?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11";
        for (const auto& column : added_columns) {
            if (columns.count(column.first) == 1) {
                sql += ", " + column.first;
                values += ", :" + column.first;
            }
        }
        sql += ") VALUES(" + values + ");";
        auto stmt = db.new_statement(sql);
        stmt->bind_text(1, error->uuid.to_string(), everest::db::sqlite::SQLiteString::Transient);
        stmt->bind_text(2, error->type);
        stmt->bind_text(3, error->description);
        stmt->bind_text(4, error->message);
        stmt->bind_text(5, error->origin.module_id);
        stmt->bind_text(6, error->origin.implementation_id);
        stmt->bind_text(7, Everest::Date::to_rfc3339(error->timestamp), everest::db::sqlite::SQLiteString::Transient);
        stmt->bind_text(8, Everest::error::severity_to_string(error->severity),
                        everest::db::sqlite::SQLiteString::Transient);
        stmt->bind_text(9, Everest::error::state_to_string(error->state), everest::db::sqlite::SQLiteString::Transient);
        stmt->bind_text(10, error->sub_type);
        stmt->bind_text(11, error->vendor_id);
        // unbound parameters are NULL
        if (error->trigger_value.has_value() and columns.count("trigger_value_text_repr") == 1) {
            stmt->bind_text(":trigger_value_text_repr", error->trigger_value.value().text_repr);
        }
        if (stmt->step() != SQLITE_DONE) {
            throw everest::db::QueryExecutionException(db.get_error_message());
        }
    } catch (const std::exception& e) {
        EVLOG_error << "Error adding error to database: " << e.what();
    }
}

ErrorDatabaseSqlite::SqlCondition
ErrorDatabaseSqlite::filter_to_sql_condition(const Everest::error::ErrorFilter& filter) {
    SqlCondition condition{};
    switch (filter.get_filter_type()) {
    case Everest::error::FilterType::State: {
        condition = {"(state = ?)", {Everest::error::state_to_string(filter.get_state_filter())}};
    } break;
    case Everest::error::FilterType::Origin: {
        condition = {"(origin_module = ? AND origin_implementation = ?)",
                     {filter.get_origin_filter().module_id, filter.get_origin_filter().implementation_id}};
    } break;
    case Everest::error::FilterType::Type: {
        condition = {"(type = ?)", {filter.get_type_filter().value}};
    } break;
    case Everest::error::FilterType::Severity: {
        switch (filter.get_severity_filter()) {
        case Everest::error::SeverityFilter::LOW_GE: {
            condition = {"(severity = ? OR severity = ? OR severity = ?)",
                         {Everest::error::severity_to_string(Everest::error::Severity::Low),
                          Everest::error::severity_to_string(Everest::error::Severity::Medium),
                          Everest::error::severity_to_string(Everest::error::Severity::High)}};
        } break;
        case Everest::error::SeverityFilter::MEDIUM_GE: {
            condition = {"(severity = ? OR severity = ?)",
                         {Everest::error::severity_to_string(Everest::error::Severity::Medium),
                          Everest::error::severity_to_string(Everest::error::Severity::High)}};
        } break;
        case Everest::error::SeverityFilter::HIGH_GE: {
            condition = {"(severity = ?)", {Everest::error::severity_to_string(Everest::error::Severity::High)}};
        } break;
        }
    } break;
    case Everest::error::FilterType::TimePeriod: {
        condition = {"(timestamp BETWEEN ? AND ?)",
                     {Everest::Date::to_rfc3339(filter.get_time_period_filter().from),
                      Everest::Date::to_rfc3339(filter.get_time_period_filter().to)}};
    } break;
    case Everest::error::FilterType::Handle: {
        condition = {"(uuid = ?)", {filter.get_handle_filter().to_string()}};
    } break;
    case Everest::error::FilterType::SubType: {
        condition = {"(sub_type = ?)", {filter.get_sub_type_filter().value}};
    } break;
    case Everest::error::FilterType::VendorId: {
        condition = {"(vendor_id = ?)", {filter.get_vendor_id_filter().value}};
    } break;
    }
    return condition;
}

std::optional<ErrorDatabaseSqlite::SqlCondition>
ErrorDatabaseSqlite::filters_to_sql_condition(const std::list<Everest::error::ErrorFilter>& filters) {
    std::optional<SqlCondition> condition = std::nullopt;
    if (!filters.empty()) {
        auto it = filters.begin();
        condition = filter_to_sql_condition(*it);
        it++;
        while (it != filters.end()) {
            const SqlCondition next = ErrorDatabaseSqlite::filter_to_sql_condition(*it);
            condition->clause += " AND " + next.clause;
            condition->values.insert(condition->values.end(), next.values.begin(), next.values.end());
            it++;
        }
    }
    return condition;
}

std::list<Everest::error::ErrorPtr>
ErrorDatabaseSqlite::get_errors(const std::list<Everest::error::ErrorFilter>& filters) const {
    std::lock_guard<std::mutex> lock(this->db_mutex);
    return this->get_errors(ErrorDatabaseSqlite::filters_to_sql_condition(filters));
}

std::list<Everest::error::ErrorPtr>
ErrorDatabaseSqlite::get_errors(const std::optional<SqlCondition>& condition) const {
    BOOST_LOG_FUNCTION();
    std::list<Everest::error::ErrorPtr> result;
    try {
        everest::db::sqlite::Connection db(this->db_path);
        if (!db.open_connection()) {
            EVLOG_error << "Error opening database";
            throw everest::db::ConnectionException(db.get_error_message());
        }
        std::string sql = "SELECT * FROM errors";
        if (condition.has_value()) {
            sql += " WHERE " + condition->clause;
        }
        EVLOG_debug << "Executing SQL statement: " << sql;
        auto stmt = db.new_statement(sql);
        if (condition.has_value()) {
            bind_values(*stmt, condition->values);
        }
        int status;
        while ((status = stmt->step()) == SQLITE_ROW) {
            const Everest::error::ErrorType err_type(std::get<std::string>(stmt->column_variant("type")));
            const std::string err_description = std::get<std::string>(stmt->column_variant("description"));
            const std::string err_msg = std::get<std::string>(stmt->column_variant("message"));
            const std::string err_origin_module_id = std::get<std::string>(stmt->column_variant("origin_module"));
            const std::string err_origin_impl_id = std::get<std::string>(stmt->column_variant("origin_implementation"));
            const ImplementationIdentifier err_origin(err_origin_module_id, err_origin_impl_id);
            const Everest::error::Error::time_point err_timestamp =
                Everest::Date::from_rfc3339(std::get<std::string>(stmt->column_variant("timestamp")));
            const Everest::error::Severity err_severity =
                Everest::error::string_to_severity(std::get<std::string>(stmt->column_variant("severity")));
            const Everest::error::State err_state =
                Everest::error::string_to_state(std::get<std::string>(stmt->column_variant("state")));
            const Everest::error::ErrorHandle err_handle(
                Everest::error::ErrorHandle(std::get<std::string>(stmt->column_variant("uuid"))));
            const Everest::error::ErrorSubType err_sub_type(std::get<std::string>(stmt->column_variant("sub_type")));
            const std::string err_vendor_id = std::get<std::string>(stmt->column_variant("vendor_id"));
            Everest::error::ErrorPtr error = std::make_shared<Everest::error::Error>(
                err_type, err_sub_type, err_msg, err_description, err_origin, err_vendor_id, err_severity,
                err_timestamp, err_handle, err_state);
            const auto trigger_value_text_repr = stmt->column_variant("trigger_value_text_repr");
            if (const auto* text_repr = std::get_if<std::string>(&trigger_value_text_repr)) {
                error->trigger_value = Everest::error::TriggerValue{*text_repr};
            }
            result.push_back(error);
        }
        if (status != SQLITE_DONE) {
            throw everest::db::QueryExecutionException(db.get_error_message());
        }
    } catch (const std::exception& e) {
        EVLOG_error << "Error getting errors from database: " << e.what();
    }
    return result;
}

std::list<Everest::error::ErrorPtr>
ErrorDatabaseSqlite::edit_errors(const std::list<Everest::error::ErrorFilter>& filters, EditErrorFunc edit_func) {
    std::lock_guard<std::mutex> lock(this->db_mutex);
    std::list<Everest::error::ErrorPtr> result = this->remove_errors_without_mutex(filters);
    for (Everest::error::ErrorPtr& error : result) {
        edit_func(error);
        this->add_error_without_mutex(error);
    }
    return result;
}

std::list<Everest::error::ErrorPtr>
ErrorDatabaseSqlite::remove_errors(const std::list<Everest::error::ErrorFilter>& filters) {
    std::lock_guard<std::mutex> lock(this->db_mutex);
    return this->remove_errors_without_mutex(filters);
}

std::list<Everest::error::ErrorPtr>
ErrorDatabaseSqlite::remove_errors_without_mutex(const std::list<Everest::error::ErrorFilter>& filters) {
    BOOST_LOG_FUNCTION();
    std::optional<SqlCondition> condition = ErrorDatabaseSqlite::filters_to_sql_condition(filters);
    std::list<Everest::error::ErrorPtr> result = this->get_errors(condition);
    try {
        everest::db::sqlite::Connection db(this->db_path);
        if (!db.open_connection()) {
            EVLOG_error << "Error opening database";
            throw everest::db::ConnectionException(db.get_error_message());
        }
        std::string sql = "DELETE FROM errors";
        if (condition.has_value()) {
            sql += " WHERE " + condition->clause;
        }
        auto stmt = db.new_statement(sql);
        if (condition.has_value()) {
            bind_values(*stmt, condition->values);
        }
        if (stmt->step() != SQLITE_DONE) {
            throw everest::db::QueryExecutionException(db.get_error_message());
        }
    } catch (const std::exception& e) {
        EVLOG_error << "Error removing errors from database: " << e.what();
    }
    return result;
}

} // namespace module
