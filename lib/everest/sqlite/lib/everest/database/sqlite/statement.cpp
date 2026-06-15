// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include <cstddef>

#include <everest/database/exceptions.hpp>
#include <everest/database/sqlite/helpers.hpp>
#include <everest/database/sqlite/statement.hpp>
#include <everest/logging.hpp>
#include <sqlite3.h>

namespace everest::db::sqlite {

Statement::Statement(sqlite3* db, const std::string& query) : db(db), stmt(nullptr) {
    if (sqlite3_prepare_v2(db, query.c_str(), clamp_to<int>(query.size()), &this->stmt, nullptr) != SQLITE_OK) {
        EVLOG_error << sqlite3_errmsg(db);
        throw QueryExecutionException("Could not prepare statement for database.");
    }
}

Statement::~Statement() {
    if (this->stmt != nullptr) {
        if (sqlite3_finalize(this->stmt) != SQLITE_OK) {
            EVLOG_error << "Error finalizing statement: " << sqlite3_errmsg(this->db);
        }
    }
}

int Statement::step() {
    return sqlite3_step(this->stmt);
}

int Statement::reset() {
    return sqlite3_reset(this->stmt);
}

int Statement::changes() {
    // Rows affected by the last INSERT, UPDATE, DELETE
    return sqlite3_changes(this->db);
}

int Statement::bind_text(const int idx, const std::string& val, SQLiteString lifetime) {
    return sqlite3_bind_text(this->stmt, idx, val.c_str(), clamp_to<int>(val.length()),
                             lifetime == SQLiteString::Static ? SQLITE_STATIC : SQLITE_TRANSIENT);
}

int Statement::bind_text(const std::string& param, const std::string& val, SQLiteString lifetime) {
    const int index = sqlite3_bind_parameter_index(this->stmt, param.c_str());
    if (index <= 0) {
        throw std::out_of_range("Parameter not found in SQL query");
    }
    return bind_text(index, val, lifetime);
}

int Statement::bind_int(const int idx, const int val) {
    return sqlite3_bind_int(this->stmt, idx, val);
}

int Statement::bind_int(const std::string& param, const int val) {
    const int index = sqlite3_bind_parameter_index(this->stmt, param.c_str());
    if (index <= 0) {
        throw std::out_of_range("Parameter not found in SQL query");
    }
    return bind_int(index, val);
}

int Statement::bind_int64(const int idx, const int64_t val) {
    return sqlite3_bind_int64(this->stmt, idx, val);
}

int Statement::bind_int64(const std::string& param, const int64_t val) {
    const int index = sqlite3_bind_parameter_index(this->stmt, param.c_str());
    if (index <= 0) {
        throw std::out_of_range("Parameter not found in SQL query");
    }
    return bind_int64(index, val);
}

int Statement::bind_double(const int idx, const double val) {
    return sqlite3_bind_double(this->stmt, idx, val);
}

int Statement::bind_double(const std::string& param, const double val) {
    const int index = sqlite3_bind_parameter_index(this->stmt, param.c_str());
    if (index <= 0) {
        throw std::out_of_range("Parameter not found in SQL query");
    }
    return bind_double(index, val);
}

int Statement::bind_null(const int idx) {
    return sqlite3_bind_null(this->stmt, idx);
}

int Statement::bind_null(const std::string& param) {
    const int index = sqlite3_bind_parameter_index(this->stmt, param.c_str());
    if (index <= 0) {
        throw std::out_of_range("Parameter not found in SQL query");
    }
    return bind_null(index);
}

int Statement::get_number_of_rows() {
    return sqlite3_data_count(this->stmt);
}

int Statement::column_type(const int idx) {
    return sqlite3_column_type(this->stmt, idx);
}

SqliteVariant Statement::column_variant(const std::string& name) {
    SqliteVariant ret{};
    const int column_count = sqlite3_column_count(this->stmt);
    for (int i = 0; i < column_count; ++i) {
        const auto val = sqlite3_column_name(this->stmt, i);
        if (val == nullptr) {
            return ret;
        }
        const std::string column_name{val};
        if (name == column_name) {
            switch (sqlite3_column_type(this->stmt, i)) {
            case SQLITE_INTEGER:
                ret = column_int64(i);
                break;
            case SQLITE_FLOAT:
                ret = column_double(i);
                break;
            case SQLITE_BLOB:
                // For now we will treat blob as text until this feature is needed
            case SQLITE_TEXT:
                ret = column_text(i);
                break;
            case SQLITE_NULL:
            default:
                break;
            }
            break;
        }
    }
    return ret;
}

std::string Statement::column_text(const int idx) {
    const auto* p = sqlite3_column_text(this->stmt, idx);
    if (p == nullptr) {
        // sqlite3_column_text returns NULL for a NULL column value, or when the statement
        // is not positioned on a row (e.g. step() returned SQLITE_BUSY/ERROR/MISUSE and a
        // caller misuses the result). Constructing std::string from nullptr is undefined
        // and aborts the process; surface as a typed exception instead so callers can log
        // and recover.
        throw QueryExecutionException("column_text(" + std::to_string(idx) +
                                      ") returned NULL; use column_text_nullable for nullable columns");
    }
    return reinterpret_cast<const char*>(p);
}

std::optional<std::string> Statement::column_text_nullable(const int idx) {
    auto p = sqlite3_column_text(this->stmt, idx);
    if (p != nullptr) {
        return reinterpret_cast<const char*>(p);
    }
    return std::optional<std::string>{};
}

int Statement::column_int(const int idx) {
    return sqlite3_column_int(this->stmt, idx);
}

int64_t Statement::column_int64(const int idx) {
    return sqlite3_column_int64(this->stmt, idx);
}

double Statement::column_double(const int idx) {
    return sqlite3_column_double(this->stmt, idx);
}

} // namespace everest::db::sqlite
