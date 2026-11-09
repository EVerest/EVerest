// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <catch2/catch_all.hpp>

#include <everest/database/sqlite/connection.hpp>
#include <everest/database/sqlite/statement.hpp>

#include "../ErrorDatabaseSqlite.hpp"
#include "helpers.hpp"

#include <fstream>
#include <memory>
#include <set>
#include <string>

namespace {

const std::string base_schema = "CREATE TABLE errors("
                                "uuid TEXT PRIMARY KEY NOT NULL,"
                                "type TEXT NOT NULL,"
                                "description TEXT NOT NULL,"
                                "message TEXT NOT NULL,"
                                "origin_module TEXT NOT NULL,"
                                "origin_implementation TEXT NOT NULL,"
                                "timestamp TEXT NOT NULL,"
                                "severity TEXT NOT NULL,"
                                "state TEXT NOT NULL,"
                                "sub_type TEXT NOT NULL,"
                                "vendor_id TEXT NOT NULL";

fs::path create_database(const std::string& sql) {
    const auto db_path = get_bin_dir() / "databases" / get_unique_db_name();
    fs::create_directories(db_path.parent_path());
    everest::db::sqlite::Connection db(db_path);
    REQUIRE(db.open_connection());
    REQUIRE(db.execute_statement(sql));
    return db_path;
}

void insert_base_row(const fs::path& db_path, const Everest::error::Error& error) {
    everest::db::sqlite::Connection db(db_path);
    REQUIRE(db.open_connection());
    auto stmt = db.new_statement("INSERT INTO errors(uuid, type, description, message, origin_module, "
                                 "origin_implementation, timestamp, severity, state, sub_type, vendor_id) "
                                 "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);");
    stmt->bind_text(1, error.uuid.to_string(), everest::db::sqlite::SQLiteString::Transient);
    stmt->bind_text(2, error.type);
    stmt->bind_text(3, error.description);
    stmt->bind_text(4, error.message);
    stmt->bind_text(5, error.origin.module_id);
    stmt->bind_text(6, error.origin.implementation_id);
    stmt->bind_text(7, Everest::Date::to_rfc3339(error.timestamp), everest::db::sqlite::SQLiteString::Transient);
    stmt->bind_text(8, Everest::error::severity_to_string(error.severity),
                    everest::db::sqlite::SQLiteString::Transient);
    stmt->bind_text(9, Everest::error::state_to_string(error.state), everest::db::sqlite::SQLiteString::Transient);
    stmt->bind_text(10, error.sub_type);
    stmt->bind_text(11, error.vendor_id);
    REQUIRE(stmt->step() == SQLITE_DONE);
}

std::set<std::string> get_column_names(const fs::path& db_path) {
    everest::db::sqlite::Connection db(db_path);
    REQUIRE(db.open_connection());
    auto stmt = db.new_statement("PRAGMA table_info(errors);");
    std::set<std::string> columns;
    while (stmt->step() == SQLITE_ROW) {
        columns.insert(std::get<std::string>(stmt->column_variant("name")));
    }
    return columns;
}

bool has_column(const fs::path& db_path, const std::string& column) {
    return get_column_names(db_path).count(column) == 1;
}

/// the error as stored in a database without the added columns
Everest::error::ErrorPtr base_error(const Everest::error::ErrorPtr& error) {
    auto result = std::make_shared<Everest::error::Error>(*error);
    result->trigger_value.reset();
    result->origin.mapping.reset();
    return result;
}

} // namespace

SCENARIO("Opening an existing ErrorDatabaseSqlite database", "[!throws]") {
    GIVEN("A database whose errors table lacks required columns") {
        const auto db_path = create_database("CREATE TABLE errors(uuid TEXT PRIMARY KEY NOT NULL, unknown TEXT);");
        TestDatabase db(db_path);
        WHEN("Adding an error") {
            const auto test_errors = get_test_errors();
            db.add_error(test_errors.at(0));
            THEN("The database was reset and stores the error") {
                check_expected_errors_in_list({test_errors.at(0)}, db.get_errors({}));
            }
        }
    }

    GIVEN("A database with the base columns, an unknown one and an error in it") {
        const auto db_path = create_database(base_schema + ", unknown TEXT);");
        const auto stored_error = base_error(get_test_errors().at(1));
        insert_base_row(db_path, *stored_error);
        TestDatabase db(db_path);
        THEN("The database and its error are kept") {
            check_expected_errors_in_list({stored_error}, db.get_errors({}));
        }
    }

    GIVEN("A database with the base columns, an unknown NOT NULL one without a default and an error in it") {
        const auto db_path = create_database(base_schema + ", unknown TEXT NOT NULL);");
        {
            everest::db::sqlite::Connection connection(db_path);
            REQUIRE(connection.open_connection());
            REQUIRE(connection.execute_statement(
                "INSERT INTO errors VALUES('uuid', 'type', 'description', 'message', 'module', "
                "'implementation', '2026-11-09T10:00:00.000Z', 'High', 'Active', 'sub_type', 'vendor', 'value');"));
        }
        TestDatabase db(db_path);
        WHEN("Adding an error") {
            const auto test_errors = get_test_errors();
            db.add_error(test_errors.at(0));
            THEN("The database was reset and stores the error") {
                check_expected_errors_in_list({test_errors.at(0)}, db.get_errors({}));
            }
        }
    }

    GIVEN("A database with the base columns, an unknown NOT NULL one with a default and an error in it") {
        const auto db_path = create_database(base_schema + ", unknown TEXT NOT NULL DEFAULT '');");
        const auto stored_error = base_error(get_test_errors().at(1));
        insert_base_row(db_path, *stored_error);
        TestDatabase db(db_path);
        WHEN("Adding an error") {
            const auto test_errors = get_test_errors();
            db.add_error(test_errors.at(0));
            THEN("The database and its errors are kept") {
                check_expected_errors_in_list({stored_error, test_errors.at(0)}, db.get_errors({}));
            }
        }
    }

    GIVEN("A database with only the base columns and an error in it") {
        const auto db_path = create_database(base_schema + ");");
        const auto test_errors = get_test_errors();
        const auto stored_error = base_error(test_errors.at(1));
        insert_base_row(db_path, *stored_error);
        TestDatabase db(db_path);
        THEN("The added columns are created and the error is kept") {
            CHECK(has_column(db_path, "trigger_value_text_repr"));
            CHECK(has_column(db_path, "origin_mapping_evse"));
            CHECK(has_column(db_path, "origin_mapping_connector"));
            check_expected_errors_in_list({stored_error}, db.get_errors({}));
        }
        WHEN("Adding an error with a trigger value and opening the database again") {
            REQUIRE(test_errors.at(0)->trigger_value.has_value());
            db.add_error(test_errors.at(0));
            module::ErrorDatabaseSqlite reopened(db_path);
            THEN("Both errors are kept") {
                check_expected_errors_in_list({stored_error, test_errors.at(0)},
                                              reopened.get_errors(std::list<Everest::error::ErrorFilter>()));
            }
        }
    }

    GIVEN("A database with the base columns and some of the added ones") {
        const auto db_path = create_database(base_schema + ", trigger_value_text_repr TEXT);");
        const auto stored_error = base_error(get_test_errors().at(1));
        insert_base_row(db_path, *stored_error);
        TestDatabase db(db_path);
        THEN("The remaining columns are added and the error is kept") {
            CHECK(has_column(db_path, "origin_mapping_evse"));
            CHECK(has_column(db_path, "origin_mapping_connector"));
            check_expected_errors_in_list({stored_error}, db.get_errors({}));
        }
    }

    GIVEN("A database with only the base columns that another connection is writing to while it is opened") {
        const auto db_path = create_database(base_schema + ");");
        const auto test_errors = get_test_errors();
        const auto stored_error = base_error(test_errors.at(1));
        insert_base_row(db_path, *stored_error);
        auto writer = std::make_unique<everest::db::sqlite::Connection>(db_path);
        REQUIRE(writer->open_connection());
        REQUIRE(writer->execute_statement("BEGIN IMMEDIATE;"));
        // the migration fails after the busy timeout of the sqlite connection (5 s)
        TestDatabase db(db_path);
        writer.reset();
        THEN("The migration is skipped, errors are still stored and edited, and the next start migrates") {
            CHECK_FALSE(has_column(db_path, "trigger_value_text_repr"));
            db.edit_errors(
                {Everest::error::ErrorFilter(Everest::error::HandleFilter(stored_error->uuid))},
                [](Everest::error::ErrorPtr error) { error->state = Everest::error::State::ClearedByReboot; });
            stored_error->state = Everest::error::State::ClearedByReboot;
            db.add_error(test_errors.at(0));
            const auto added_error = base_error(test_errors.at(0));
            check_expected_errors_in_list({stored_error, added_error}, db.get_errors({}));

            module::ErrorDatabaseSqlite reopened(db_path);
            CHECK(has_column(db_path, "trigger_value_text_repr"));
            CHECK(has_column(db_path, "origin_mapping_evse"));
            CHECK(has_column(db_path, "origin_mapping_connector"));
            check_expected_errors_in_list({stored_error, added_error},
                                          reopened.get_errors(std::list<Everest::error::ErrorFilter>()));
        }
    }

    GIVEN("A database without an errors table") {
        const auto db_path = create_database("CREATE TABLE other(id INTEGER);");
        TestDatabase db(db_path);
        WHEN("Adding an error") {
            const auto test_errors = get_test_errors();
            db.add_error(test_errors.at(0));
            THEN("The database was reset and stores the error") {
                check_expected_errors_in_list({test_errors.at(0)}, db.get_errors({}));
            }
        }
    }

    GIVEN("A file that is not an SQLite database") {
        const auto db_path = get_bin_dir() / "databases" / get_unique_db_name();
        fs::create_directories(db_path.parent_path());
        std::ofstream(db_path) << "not a database";
        TestDatabase db(db_path);
        WHEN("Adding an error") {
            const auto test_errors = get_test_errors();
            db.add_error(test_errors.at(0));
            THEN("The database was reset and stores the error") {
                check_expected_errors_in_list({test_errors.at(0)}, db.get_errors({}));
            }
        }
    }

    GIVEN("A database file that can not be read") {
        const auto db_path = create_database(base_schema + ");");
        const auto stored_error = get_test_errors().at(1);
        insert_base_row(db_path, *stored_error);
        fs::permissions(db_path, fs::perms::none);
        const bool readable = std::ifstream(db_path).is_open();
        { module::ErrorDatabaseSqlite db(db_path); }
        fs::permissions(db_path, fs::perms::owner_read | fs::perms::owner_write);
        THEN("It is kept as it is") {
            if (readable) {
                SKIP("Permissions do not apply to this user");
            }
            TestDatabase db(db_path);
            check_expected_errors_in_list({stored_error}, db.get_errors({}));
        }
    }

    GIVEN("A database path that can not be opened") {
        const auto db_path = get_bin_dir() / "databases" / get_unique_db_name();
        fs::create_directories(db_path);
        { module::ErrorDatabaseSqlite db(db_path); }
        THEN("It is kept as it is") {
            CHECK(fs::is_directory(db_path));
        }
        fs::remove(db_path);
    }
}
