// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include <everest/database/exceptions.hpp>
#include <everest/database/sqlite/connection.hpp>
#include <everest/database/sqlite/statement.hpp>
#include <gtest/gtest.h>
#include <sqlite3.h>

#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

namespace everest::db::sqlite {

class TransactionTest : public ::testing::Test {
protected:
    std::unique_ptr<Connection> db;

    void SetUp() override {
        db = std::make_unique<Connection>(fs::path("file::memory:?cache=shared"));
        ASSERT_TRUE(db->open_connection());

        // Enforce foreign key constraints so the deferred-fkeys behaviour is observable.
        ASSERT_TRUE(db->execute_statement("PRAGMA foreign_keys = ON;"));

        ASSERT_TRUE(db->execute_statement("CREATE TABLE parent (id INTEGER PRIMARY KEY);"));
        ASSERT_TRUE(db->execute_statement(
            "CREATE TABLE child (id INTEGER PRIMARY KEY, parent_id INTEGER REFERENCES parent(id));"));
    }

    void TearDown() override {
        db->close_connection();
    }

    int count_rows(const std::string& table) {
        auto stmt = db->new_statement("SELECT COUNT(*) FROM " + table + ";");
        EXPECT_EQ(stmt->step(), SQLITE_ROW);
        return stmt->column_int(0);
    }

    void insert_parent(int id) {
        ASSERT_TRUE(db->execute_statement("INSERT INTO parent (id) VALUES (" + std::to_string(id) + ");"));
    }

    bool try_insert_child(int id, int parent_id) {
        auto stmt = db->new_statement("INSERT INTO child (id, parent_id) VALUES (?, ?);");
        stmt->bind_int(1, id);
        stmt->bind_int(2, parent_id);
        return stmt->step() == SQLITE_DONE;
    }
};

TEST_F(TransactionTest, BeginTransactionReturnsValidObject) {
    auto transaction = db->begin_transaction();
    ASSERT_NE(transaction, nullptr);
}

TEST_F(TransactionTest, CommitPersistsChanges) {
    {
        auto transaction = db->begin_transaction();
        insert_parent(1);
        transaction->commit();
    }

    EXPECT_EQ(count_rows("parent"), 1);
}

TEST_F(TransactionTest, RollbackDiscardsChanges) {
    {
        auto transaction = db->begin_transaction();
        insert_parent(1);
        transaction->rollback();
    }

    EXPECT_EQ(count_rows("parent"), 0);
}

TEST_F(TransactionTest, DestructorRollsBackUncommittedTransaction) {
    {
        auto transaction = db->begin_transaction();
        insert_parent(1);
        // No commit(): destruction must roll back.
    }

    EXPECT_EQ(count_rows("parent"), 0);
}

TEST_F(TransactionTest, DoubleCommitIsNoOp) {
    auto transaction = db->begin_transaction();
    insert_parent(1);
    transaction->commit();
    // Second commit must be a safe no-op and not throw.
    EXPECT_NO_THROW(transaction->commit());

    EXPECT_EQ(count_rows("parent"), 1);
}

TEST_F(TransactionTest, CommitAfterRollbackIsNoOp) {
    auto transaction = db->begin_transaction();
    insert_parent(1);
    transaction->rollback();
    EXPECT_NO_THROW(transaction->commit());

    EXPECT_EQ(count_rows("parent"), 0);
}

TEST_F(TransactionTest, SequentialTransactionsReuseConnection) {
    {
        auto transaction = db->begin_transaction();
        insert_parent(1);
        transaction->commit();
    }
    {
        auto transaction = db->begin_transaction();
        insert_parent(2);
        transaction->commit();
    }

    EXPECT_EQ(count_rows("parent"), 2);
}

TEST_F(TransactionTest, NonDeferredTransactionRejectsOutOfOrderInsert) {
    auto transaction = db->begin_transaction_with_enforced_fkeys();
    // Without deferred foreign keys the constraint is checked immediately, so
    // inserting a child before its parent must fail.
    EXPECT_FALSE(try_insert_child(1, 42));
    transaction->rollback();
}

TEST_F(TransactionTest, PlainTransactionDoesNotEnforceFkeys) {
    // Foreign key enforcement is per-transaction: even though the fixture (or a
    // previous fkeys-enforcing transaction) enabled the pragma on the connection,
    // a plain transaction must not enforce constraints.
    auto transaction = db->begin_transaction();
    EXPECT_TRUE(try_insert_child(1, 42));
    transaction->rollback();
}

TEST_F(TransactionTest, DeferredFkeysAllowsOutOfOrderInsert) {
    {
        auto transaction = db->begin_transaction_with_deferred_fkeys();
        // Child references a parent that does not exist yet; deferral postpones the
        // constraint check until commit.
        ASSERT_TRUE(try_insert_child(1, 1));
        insert_parent(1);
        transaction->commit();
    }

    EXPECT_EQ(count_rows("child"), 1);
    EXPECT_EQ(count_rows("parent"), 1);
}

TEST_F(TransactionTest, DeferredFkeysStillEnforcedAtCommit) {
    auto transaction = db->begin_transaction_with_deferred_fkeys();
    // Insert a child that references a parent which is never created. The
    // violation is only detected at commit time, which must fail.
    ASSERT_TRUE(try_insert_child(1, 99));
    EXPECT_THROW(transaction->commit(), QueryExecutionException);

    // The failed commit must resolve the transaction itself (rollback) before
    // releasing the connection, so no pending transaction may remain.
    EXPECT_FALSE(db->has_pending_transaction());
    EXPECT_EQ(count_rows("child"), 0);

    // The connection must be immediately usable for a follow-up transaction.
    auto next_transaction = db->begin_transaction();
    insert_parent(1);
    next_transaction->commit();
    EXPECT_EQ(count_rows("parent"), 1);
}

} // namespace everest::db::sqlite
