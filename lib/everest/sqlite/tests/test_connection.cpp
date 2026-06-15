// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include <everest/database/sqlite/connection.hpp>
#include <everest/database/sqlite/statement.hpp>
#include <gtest/gtest.h>
#include <sqlite3.h>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace everest::db::sqlite {

class ConnectionWalTest : public ::testing::Test {
protected:
    fs::path db_path;

    void SetUp() override {
        const auto unique = "everest_sqlite_conn_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + "_" +
                            std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".db";
        db_path = fs::temp_directory_path() / unique;
        std::error_code ec;
        fs::remove(db_path, ec);
        fs::remove(db_path.string() + "-wal", ec);
        fs::remove(db_path.string() + "-shm", ec);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove(db_path, ec);
        fs::remove(db_path.string() + "-wal", ec);
        fs::remove(db_path.string() + "-shm", ec);
    }
};

TEST_F(ConnectionWalTest, FileDatabaseUsesWalJournalMode) {
    Connection db(db_path);
    ASSERT_TRUE(db.open_connection());

    {
        auto stmt = db.new_statement("PRAGMA journal_mode");
        ASSERT_EQ(stmt->step(), SQLITE_ROW);
        EXPECT_EQ(stmt->column_text(0), "wal");
    }

    db.close_connection();
}

TEST_F(ConnectionWalTest, InMemoryDatabaseOpensWithoutWalFailure) {
    Connection db(fs::path("file::memory:?cache=shared"));
    ASSERT_TRUE(db.open_connection());
    db.close_connection();
}

TEST_F(ConnectionWalTest, ExistingRollbackDatabaseMigratesToWal) {
    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open_v2(db_path.c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(raw, "PRAGMA journal_mode=DELETE; CREATE TABLE t(id INTEGER);", nullptr, nullptr, nullptr),
              SQLITE_OK);
    sqlite3_close(raw);

    Connection db(db_path);
    ASSERT_TRUE(db.open_connection());

    {
        auto stmt = db.new_statement("PRAGMA journal_mode");
        ASSERT_EQ(stmt->step(), SQLITE_ROW);
        EXPECT_EQ(stmt->column_text(0), "wal");
    }

    db.close_connection();
}

} // namespace everest::db::sqlite
