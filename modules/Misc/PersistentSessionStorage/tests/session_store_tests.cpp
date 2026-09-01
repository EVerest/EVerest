// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <database_testing_utils.hpp>
#include <everest/database/sqlite/connection.hpp>

#include <storage/session_store.hpp>

namespace {

using everest::db::sqlite::Connection;
using everest::db::sqlite::ConnectionInterface;
using everest::db::sqlite::SQLiteString;
using module::storage::OcppTransactionEvent;
using module::storage::SessionStart;
using module::storage::SessionStore;
using module::storage::TransactionFinish;
using module::storage::TransactionStart;
using types::session_storage::GetSessionsRequest;
using types::session_storage::Session;
using types::session_storage::SessionFilter;
using types::session_storage::SessionIdentifier;
using types::session_storage::SessionState;

constexpr auto IN_MEMORY_DATABASE = "file::memory:?cache=shared";
constexpr int DEFAULT_MAX_SESSIONS = 100;
constexpr int PAGE_ITERATION_LIMIT = 1000;

GetSessionsRequest make_request(std::optional<std::int32_t> limit = std::nullopt,
                                std::optional<std::string> continuation_token = std::nullopt,
                                std::optional<SessionFilter> filter = std::nullopt) {
    GetSessionsRequest request{};
    request.limit = limit;
    request.continuation_token = std::move(continuation_token);
    request.filter = std::move(filter);
    return request;
}

/// \brief Every record \p store returns for \p filter, collected page by page
std::vector<Session> page_through_all(SessionStore& store, std::optional<SessionFilter> filter,
                                      std::optional<std::int32_t> limit) {
    std::vector<Session> sessions{};
    std::optional<std::string> continuation_token{};

    for (int iteration = 0; iteration < PAGE_ITERATION_LIMIT; ++iteration) {
        const auto page = store.get_sessions(make_request(limit, continuation_token, filter));
        sessions.insert(sessions.end(), page.sessions.begin(), page.sessions.end());
        if (not page.continuation_token.has_value()) {
            return sessions;
        }
        continuation_token = page.continuation_token;
    }

    ADD_FAILURE() << "Paging did not terminate within " << PAGE_ITERATION_LIMIT << " pages";
    return sessions;
}

std::vector<Session> page_through_all(SessionStore& store) {
    return page_through_all(store, std::nullopt, std::nullopt);
}

std::vector<std::string> session_ids(const std::vector<Session>& sessions) {
    std::vector<std::string> ids{};
    ids.reserve(sessions.size());
    for (const auto& session : sessions) {
        ids.push_back(session.session_id);
    }
    return ids;
}

/// \brief \p token with its record id replaced by \p id, keeping the database epoch
std::string token_with_id(const std::string& token, std::int64_t id) {
    return token.substr(0, token.find(':') + 1) + std::to_string(id);
}

types::units_signed::SignedMeterValue make_signed_meter_value(const std::string& data) {
    types::units_signed::SignedMeterValue signed_meter_value{};
    signed_meter_value.signed_meter_data = data;
    signed_meter_value.signing_method = "ECDSA-secp256r1-SHA256";
    signed_meter_value.encoding_method = "OCMF";
    signed_meter_value.public_key = "public-key";
    signed_meter_value.timestamp = "2026-08-21T10:00:00Z";
    return signed_meter_value;
}

types::session_cost::SessionCost make_session_cost(const std::string& session_id, int cost_value) {
    types::money::Currency currency{};
    currency.code = types::money::CurrencyCode::EUR;
    currency.decimals = 2;

    types::session_cost::SessionCostChunk chunk{};
    chunk.timestamp_from = "2026-08-21T10:00:00Z";
    chunk.timestamp_to = "2026-08-21T11:00:00Z";
    chunk.cost = types::money::MoneyAmount{cost_value};
    chunk.category = types::session_cost::CostCategory::Energy;

    types::session_cost::SessionCost cost{};
    cost.session_id = session_id;
    cost.currency = currency;
    cost.status = types::session_cost::SessionStatus::Finished;
    cost.cost_chunks = std::vector<types::session_cost::SessionCostChunk>{chunk};
    return cost;
}

SessionStart make_session(const std::string& session_id, std::int32_t evse_id, const std::string& timestamp) {
    SessionStart session{};
    session.session_id = session_id;
    session.evse_id = evse_id;
    session.evse_id_string = "DE*PNX*E" + std::to_string(evse_id);
    session.connector_id = 1;
    session.timestamp_start = timestamp;
    session.start_reason = types::evse_manager::StartSessionReason::EVConnected;
    return session;
}

TransactionStart make_transaction_start(const std::string& session_id,
                                        const std::string& timestamp = "2026-08-21T10:30:00Z") {
    TransactionStart transaction{};
    transaction.session_id = session_id;
    transaction.timestamp_start = timestamp;
    transaction.energy_Wh_import_start = 1000.0F;
    transaction.id_token_hash = std::string(64, 'a');
    transaction.id_token_type = types::authorization::IdTokenType::ISO14443;
    transaction.authorization_type = types::authorization::AuthorizationType::RFID;
    return transaction;
}

TransactionFinish make_finish(const std::string& session_id, const std::string& timestamp) {
    TransactionFinish finish{};
    finish.session_id = session_id;
    finish.timestamp_stop = timestamp;
    finish.energy_Wh_import_stop = 5000.0F;
    finish.stop_reason = types::evse_manager::StopTransactionReason::EVDisconnected;
    return finish;
}

OcppTransactionEvent make_ocpp_event(const std::string& session_id, types::ocpp::TransactionEvent transaction_event,
                                     const std::optional<std::string>& transaction_id,
                                     const std::string& timestamp = "2026-08-21T10:20:00Z") {
    OcppTransactionEvent event{};
    event.session_id = session_id;
    event.transaction_event = transaction_event;
    event.transaction_id = transaction_id;
    event.timestamp = timestamp;
    return event;
}

class SessionStoreTest : public everest::db::sqlite::DatabaseTestingUtils {
protected:
    std::unique_ptr<SessionStore> store{};

    void SetUp() override {
        store = make_store(DEFAULT_MAX_SESSIONS);
        ASSERT_TRUE(store->open());
    }

    std::unique_ptr<SessionStore> make_store(int max_sessions) {
        return std::make_unique<SessionStore>(std::make_unique<Connection>(IN_MEMORY_DATABASE),
                                              std::filesystem::path{MIGRATION_FILES_PATH},
                                              SESSION_STORAGE_MIGRATION_FILE_VERSION, max_sessions);
    }

    std::vector<Session> all_sessions() {
        return page_through_all(*store);
    }

    void store_records(SessionStore& store, int count) {
        for (int index = 1; index <= count; ++index) {
            ASSERT_TRUE(
                store.store_session_started(make_session("s" + std::to_string(index), 1, "2026-08-21T10:00:00Z")));
        }
    }

    void corrupt_start_reason(const std::string& session_id) {
        auto statement =
            database->new_statement("UPDATE SESSIONS SET START_REASON='Bogus' WHERE SESSION_ID=@session_id");
        statement->bind_text("@session_id", session_id, SQLiteString::Transient);
        ASSERT_EQ(statement->step(), SQLITE_DONE);
    }

    std::vector<std::int64_t> row_ids() {
        std::vector<std::int64_t> ids{};
        auto statement = database->new_statement("SELECT ID FROM SESSIONS ORDER BY ID ASC");
        while (statement->step() == SQLITE_ROW) {
            ids.push_back(statement->column_int64(0));
        }
        return ids;
    }

    std::optional<Session> get_by_session_id(const std::string& session_id) {
        SessionIdentifier identifier{};
        identifier.session_id = session_id;
        return store->get_session(identifier);
    }

    std::optional<Session> get_by_ocpp_transaction_id(const std::string& ocpp_transaction_id) {
        SessionIdentifier identifier{};
        identifier.ocpp_transaction_id = ocpp_transaction_id;
        return store->get_session(identifier);
    }
};

// ---------------------------------------------------------------------------
// Migration
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, open_applies_the_migrations) {
    ExpectUserVersion(SESSION_STORAGE_MIGRATION_FILE_VERSION);
}

TEST_F(SessionStoreTest, open_creates_the_sessions_table) {
    EXPECT_TRUE(DoesTableExist("SESSIONS"));
}

TEST_F(SessionStoreTest, open_creates_all_columns) {
    for (const auto* column : {"ID",
                               "SESSION_ID",
                               "OCPP_TRANSACTION_ID",
                               "OCPP_TRANSACTION_TIMESTAMP_START",
                               "OCPP_TRANSACTION_TIMESTAMP_STOP",
                               "EVSE_ID",
                               "EVSE_ID_STRING",
                               "CONNECTOR_ID",
                               "STATE",
                               "TIMESTAMP_START",
                               "START_REASON",
                               "TIMESTAMP_STOP",
                               "TRANSACTION_TIMESTAMP_START",
                               "TRANSACTION_TIMESTAMP_STOP",
                               "ENERGY_WH_IMPORT_START",
                               "ENERGY_WH_IMPORT_STOP",
                               "ID_TOKEN_HASH",
                               "ID_TOKEN_TYPE",
                               "AUTHORIZATION_TYPE",
                               "STOP_REASON",
                               "SIGNED_METER_VALUE_START",
                               "SIGNED_METER_VALUE_STOP",
                               "COST"}) {
        EXPECT_TRUE(DoesColumnExist("SESSIONS", column)) << "missing column " << column;
    }
}

TEST_F(SessionStoreTest, opening_an_already_migrated_database_succeeds) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));

    auto second_store = make_store(DEFAULT_MAX_SESSIONS);
    ASSERT_TRUE(second_store->open());

    const auto sessions = page_through_all(*second_store);
    ASSERT_EQ(sessions.size(), 1);
    EXPECT_EQ(sessions.at(0).session_id, "s1");
}

// ---------------------------------------------------------------------------
// Insert and round-trip
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, session_without_a_transaction_round_trips) {
    auto expected = make_session("s1", 3, "2026-08-21T10:00:00Z");
    expected.connector_id = 2;
    expected.start_reason = types::evse_manager::StartSessionReason::Authorized;
    ASSERT_TRUE(store->store_session_started(expected));

    const auto sessions = all_sessions();
    ASSERT_EQ(sessions.size(), 1);
    const auto& actual = sessions.at(0);

    EXPECT_EQ(actual.session_id, "s1");
    EXPECT_EQ(actual.evse_id, 3);
    EXPECT_EQ(actual.evse_id_string, "DE*PNX*E3");
    EXPECT_EQ(actual.connector_id, 2);
    EXPECT_EQ(actual.state, SessionState::Open);
    EXPECT_EQ(actual.timestamp_start, "2026-08-21T10:00:00Z");
    EXPECT_EQ(actual.start_reason, types::evse_manager::StartSessionReason::Authorized);
    EXPECT_FALSE(actual.transaction.has_value());
    EXPECT_FALSE(actual.timestamp_stop.has_value());
    EXPECT_FALSE(actual.ocpp_transaction_id.has_value());
    EXPECT_FALSE(actual.ocpp_transaction_timestamp_start.has_value());
    EXPECT_FALSE(actual.ocpp_transaction_timestamp_stop.has_value());
    EXPECT_FALSE(actual.cost.has_value());
}

TEST_F(SessionStoreTest, full_record_round_trips) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 3, "2026-08-21T10:00:00Z")));

    auto transaction = make_transaction_start("s1", "2026-08-21T10:15:00Z");
    transaction.signed_meter_value_start = make_signed_meter_value("start-data");
    ASSERT_TRUE(store->store_transaction_started(transaction));

    auto finish = make_finish("s1", "2026-08-21T11:45:00Z");
    finish.stop_reason = types::evse_manager::StopTransactionReason::Local;
    finish.signed_meter_value_stop = make_signed_meter_value("stop-data");
    ASSERT_TRUE(store->store_transaction_finished(finish));
    ASSERT_TRUE(store->store_session_finished("s1", "2026-08-21T12:00:00Z"));
    ASSERT_TRUE(store->store_ocpp_transaction_event(
        make_ocpp_event("s1", types::ocpp::TransactionEvent::Started, "ocpp-s1", "2026-08-21T10:14:00Z")));
    ASSERT_TRUE(store->store_ocpp_transaction_event(
        make_ocpp_event("s1", types::ocpp::TransactionEvent::Ended, "ocpp-s1", "2026-08-21T11:46:00Z")));
    ASSERT_TRUE(store->update_cost("s1", make_session_cost("s1", 4321)));

    const auto sessions = all_sessions();
    ASSERT_EQ(sessions.size(), 1);
    const auto& actual = sessions.at(0);

    EXPECT_EQ(actual.session_id, "s1");
    EXPECT_EQ(actual.evse_id, 3);
    EXPECT_EQ(actual.evse_id_string, "DE*PNX*E3");
    EXPECT_EQ(actual.connector_id, 1);
    EXPECT_EQ(actual.state, SessionState::Finished);
    EXPECT_EQ(actual.timestamp_start, "2026-08-21T10:00:00Z");
    EXPECT_EQ(actual.timestamp_stop, "2026-08-21T12:00:00Z");
    EXPECT_EQ(actual.start_reason, types::evse_manager::StartSessionReason::EVConnected);
    EXPECT_EQ(actual.ocpp_transaction_id, "ocpp-s1");
    EXPECT_EQ(actual.ocpp_transaction_timestamp_start, "2026-08-21T10:14:00Z");
    EXPECT_EQ(actual.ocpp_transaction_timestamp_stop, "2026-08-21T11:46:00Z");

    ASSERT_TRUE(actual.transaction.has_value());
    const auto& stored = actual.transaction.value();
    EXPECT_EQ(stored.timestamp_start, "2026-08-21T10:15:00Z");
    EXPECT_EQ(stored.timestamp_stop, "2026-08-21T11:45:00Z");
    EXPECT_FLOAT_EQ(stored.energy_Wh_import_start, 1000.0F);
    ASSERT_TRUE(stored.energy_Wh_import_stop.has_value());
    EXPECT_FLOAT_EQ(stored.energy_Wh_import_stop.value(), 5000.0F);
    EXPECT_EQ(stored.id_token_hash, std::string(64, 'a'));
    EXPECT_EQ(stored.id_token_type, types::authorization::IdTokenType::ISO14443);
    EXPECT_EQ(stored.authorization_type, types::authorization::AuthorizationType::RFID);
    EXPECT_EQ(stored.stop_reason, types::evse_manager::StopTransactionReason::Local);

    ASSERT_TRUE(stored.signed_meter_value_start.has_value());
    const auto& signed_meter_value_start = stored.signed_meter_value_start.value();
    EXPECT_EQ(signed_meter_value_start.signed_meter_data, "start-data");
    EXPECT_EQ(signed_meter_value_start.signing_method, "ECDSA-secp256r1-SHA256");
    EXPECT_EQ(signed_meter_value_start.encoding_method, "OCMF");
    EXPECT_EQ(signed_meter_value_start.public_key, "public-key");
    EXPECT_EQ(signed_meter_value_start.timestamp, "2026-08-21T10:00:00Z");

    ASSERT_TRUE(stored.signed_meter_value_stop.has_value());
    EXPECT_EQ(stored.signed_meter_value_stop.value().signed_meter_data, "stop-data");

    ASSERT_TRUE(actual.cost.has_value());
    const auto& cost = actual.cost.value();
    EXPECT_EQ(cost.session_id, "s1");
    EXPECT_EQ(cost.status, types::session_cost::SessionStatus::Finished);
    ASSERT_TRUE(cost.cost_chunks.has_value());
    ASSERT_EQ(cost.cost_chunks.value().size(), 1);
    ASSERT_TRUE(cost.cost_chunks.value().at(0).cost.has_value());
    EXPECT_EQ(cost.cost_chunks.value().at(0).cost.value().value, 4321);
    EXPECT_EQ(cost.cost_chunks.value().at(0).category, types::session_cost::CostCategory::Energy);
}

TEST_F(SessionStoreTest, unset_optionals_of_a_transaction_come_back_absent) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));

    TransactionStart transaction{};
    transaction.session_id = "s1";
    transaction.timestamp_start = "2026-08-21T10:30:00Z";
    transaction.energy_Wh_import_start = 1000.0F;
    ASSERT_TRUE(store->store_transaction_started(transaction));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->transaction.has_value());
    const auto& stored = actual->transaction.value();
    EXPECT_FALSE(stored.id_token_hash.has_value());
    EXPECT_FALSE(stored.id_token_type.has_value());
    EXPECT_FALSE(stored.authorization_type.has_value());
    EXPECT_FALSE(stored.signed_meter_value_start.has_value());
}

TEST_F(SessionStoreTest, a_running_transaction_has_no_stop_side_fields) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->state, SessionState::Open);
    EXPECT_FALSE(actual->timestamp_stop.has_value());
    ASSERT_TRUE(actual->transaction.has_value());
    const auto& stored = actual->transaction.value();
    EXPECT_FALSE(stored.timestamp_stop.has_value());
    EXPECT_FALSE(stored.energy_Wh_import_stop.has_value());
    EXPECT_FALSE(stored.signed_meter_value_stop.has_value());
    EXPECT_FALSE(stored.stop_reason.has_value());
}

// ---------------------------------------------------------------------------
// Ordering
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, records_are_returned_in_insertion_order) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T12:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 2, "2026-08-21T11:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s3", 3, "2026-08-21T10:00:00Z")));

    EXPECT_EQ(session_ids(all_sessions()), std::vector<std::string>({"s1", "s2", "s3"}));
}

// ---------------------------------------------------------------------------
// Duplicates
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, duplicate_session_id_is_rejected) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));

    const auto before = get_by_session_id("s1");
    ASSERT_TRUE(before.has_value());

    EXPECT_FALSE(store->store_session_started(make_session("s1", 2, "2026-08-21T11:00:00Z")));

    const auto sessions = all_sessions();
    ASSERT_EQ(sessions.size(), 1);
    EXPECT_EQ(sessions.at(0), before.value());
}

TEST_F(SessionStoreTest, duplicate_session_id_does_not_stale_or_prune) {
    auto small_store = make_store(2);
    ASSERT_TRUE(small_store->open());

    ASSERT_TRUE(small_store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(small_store->store_session_started(make_session("s2", 2, "2026-08-21T11:00:00Z")));

    // Would have staled s2 and pruned s1 away if it had succeeded
    EXPECT_FALSE(small_store->store_session_started(make_session("s1", 2, "2026-08-21T12:00:00Z")));

    const auto sessions = page_through_all(*small_store);
    ASSERT_EQ(sessions.size(), 2);
    EXPECT_EQ(sessions.at(0).session_id, "s1");
    EXPECT_EQ(sessions.at(0).state, SessionState::Open);
    EXPECT_EQ(sessions.at(1).session_id, "s2");
    EXPECT_EQ(sessions.at(1).state, SessionState::Open);
}

// ---------------------------------------------------------------------------
// Staling
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, older_open_record_of_the_same_evse_becomes_stale) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 1, "2026-08-21T11:00:00Z")));

    const auto staled = get_by_session_id("s1");
    ASSERT_TRUE(staled.has_value());
    EXPECT_EQ(staled->state, SessionState::Stale);
}

TEST_F(SessionStoreTest, a_session_that_never_reached_a_transaction_becomes_stale) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 1, "2026-08-21T11:00:00Z")));

    const auto staled = get_by_session_id("s1");
    ASSERT_TRUE(staled.has_value());
    EXPECT_EQ(staled->state, SessionState::Stale);
    EXPECT_FALSE(staled->transaction.has_value());
}

TEST_F(SessionStoreTest, staled_record_keeps_its_start_fields) {
    const auto original = make_session("s1", 1, "2026-08-21T10:00:00Z");
    ASSERT_TRUE(store->store_session_started(original));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 1, "2026-08-21T11:00:00Z")));

    const auto staled = get_by_session_id("s1");
    ASSERT_TRUE(staled.has_value());
    EXPECT_EQ(staled->timestamp_start, original.timestamp_start);
    EXPECT_EQ(staled->evse_id, original.evse_id);
    EXPECT_EQ(staled->evse_id_string, original.evse_id_string);
    EXPECT_EQ(staled->connector_id, original.connector_id);
    EXPECT_EQ(staled->start_reason, original.start_reason);
    ASSERT_TRUE(staled->transaction.has_value());
    EXPECT_FLOAT_EQ(staled->transaction->energy_Wh_import_start, 1000.0F);
}

TEST_F(SessionStoreTest, staled_record_has_no_stop_side_fields) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 1, "2026-08-21T11:00:00Z")));

    const auto staled = get_by_session_id("s1");
    ASSERT_TRUE(staled.has_value());
    EXPECT_FALSE(staled->timestamp_stop.has_value());
    ASSERT_TRUE(staled->transaction.has_value());
    EXPECT_FALSE(staled->transaction->timestamp_stop.has_value());
    EXPECT_FALSE(staled->transaction->energy_Wh_import_stop.has_value());
    EXPECT_FALSE(staled->transaction->signed_meter_value_stop.has_value());
    EXPECT_FALSE(staled->transaction->stop_reason.has_value());
}

TEST_F(SessionStoreTest, newly_inserted_record_stays_open) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 1, "2026-08-21T11:00:00Z")));

    const auto newest = get_by_session_id("s2");
    ASSERT_TRUE(newest.has_value());
    EXPECT_EQ(newest->state, SessionState::Open);
}

TEST_F(SessionStoreTest, open_record_of_another_evse_is_not_staled) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 2, "2026-08-21T11:00:00Z")));

    const auto other_evse = get_by_session_id("s1");
    ASSERT_TRUE(other_evse.has_value());
    EXPECT_EQ(other_evse->state, SessionState::Open);
}

TEST_F(SessionStoreTest, finished_record_of_the_same_evse_is_not_staled) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_finished("s1", "2026-08-21T10:30:00Z"));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 1, "2026-08-21T11:00:00Z")));

    const auto finished = get_by_session_id("s1");
    ASSERT_TRUE(finished.has_value());
    EXPECT_EQ(finished->state, SessionState::Finished);
}

TEST_F(SessionStoreTest, already_stale_record_stays_stale) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 1, "2026-08-21T11:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s3", 1, "2026-08-21T12:00:00Z")));

    const auto first = get_by_session_id("s1");
    const auto second = get_by_session_id("s2");
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->state, SessionState::Stale);
    EXPECT_EQ(second->state, SessionState::Stale);
}

// ---------------------------------------------------------------------------
// Pruning
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, pruning_keeps_the_newest_records) {
    auto small_store = make_store(3);
    ASSERT_TRUE(small_store->open());

    for (const auto* session_id : {"s1", "s2", "s3", "s4"}) {
        ASSERT_TRUE(small_store->store_session_started(make_session(session_id, 1, "2026-08-21T10:00:00Z")));
    }

    EXPECT_EQ(session_ids(page_through_all(*small_store)), std::vector<std::string>({"s2", "s3", "s4"}));
}

TEST_F(SessionStoreTest, pruning_with_maximum_of_one_keeps_only_the_newest) {
    auto small_store = make_store(1);
    ASSERT_TRUE(small_store->open());

    ASSERT_TRUE(small_store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(small_store->store_session_started(make_session("s2", 1, "2026-08-21T11:00:00Z")));

    EXPECT_EQ(session_ids(page_through_all(*small_store)), std::vector<std::string>({"s2"}));
}

TEST_F(SessionStoreTest, a_lowered_maximum_prunes_on_the_next_insert) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 2, "2026-08-21T11:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s3", 3, "2026-08-21T12:00:00Z")));

    auto small_store = make_store(2);
    ASSERT_TRUE(small_store->open());
    ASSERT_TRUE(small_store->store_session_started(make_session("s4", 4, "2026-08-21T13:00:00Z")));

    EXPECT_EQ(session_ids(page_through_all(*small_store)), std::vector<std::string>({"s3", "s4"}));
}

TEST_F(SessionStoreTest, pruning_deletes_open_records_too) {
    auto small_store = make_store(1);
    ASSERT_TRUE(small_store->open());

    ASSERT_TRUE(small_store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(small_store->store_session_started(make_session("s2", 2, "2026-08-21T11:00:00Z")));

    EXPECT_FALSE(get_by_session_id("s1").has_value());
    const auto survivor = get_by_session_id("s2");
    ASSERT_TRUE(survivor.has_value());
    EXPECT_EQ(survivor->state, SessionState::Open);
}

TEST_F(SessionStoreTest, ids_keep_increasing_across_pruning) {
    auto small_store = make_store(2);
    ASSERT_TRUE(small_store->open());

    for (const auto* session_id : {"s1", "s2", "s3", "s4", "s5"}) {
        ASSERT_TRUE(small_store->store_session_started(make_session(session_id, 1, "2026-08-21T10:00:00Z")));
    }

    EXPECT_EQ(row_ids(), std::vector<std::int64_t>({4, 5}));
    EXPECT_EQ(session_ids(page_through_all(*small_store)), std::vector<std::string>({"s4", "s5"}));
}

// ---------------------------------------------------------------------------
// Starting a transaction
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, transaction_start_fields_round_trip) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));

    auto transaction = make_transaction_start("s1", "2026-08-21T10:42:00Z");
    transaction.energy_Wh_import_start = 4711.5F;
    transaction.id_token_type = types::authorization::IdTokenType::eMAID;
    transaction.authorization_type = types::authorization::AuthorizationType::PlugAndCharge;
    transaction.signed_meter_value_start = make_signed_meter_value("start-data");
    ASSERT_TRUE(store->store_transaction_started(transaction));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->transaction.has_value());
    const auto& stored = actual->transaction.value();
    EXPECT_EQ(stored.timestamp_start, "2026-08-21T10:42:00Z");
    EXPECT_FLOAT_EQ(stored.energy_Wh_import_start, 4711.5F);
    EXPECT_EQ(stored.id_token_hash, std::string(64, 'a'));
    EXPECT_EQ(stored.id_token_type, types::authorization::IdTokenType::eMAID);
    EXPECT_EQ(stored.authorization_type, types::authorization::AuthorizationType::PlugAndCharge);
    ASSERT_TRUE(stored.signed_meter_value_start.has_value());
    EXPECT_EQ(stored.signed_meter_value_start.value().signed_meter_data, "start-data");
}

TEST_F(SessionStoreTest, transaction_start_leaves_the_session_open) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->state, SessionState::Open);
}

TEST_F(SessionStoreTest, transaction_start_of_an_unknown_session_fails) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));

    EXPECT_FALSE(store->store_transaction_started(make_transaction_start("unknown")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_FALSE(actual->transaction.has_value());
}

TEST_F(SessionStoreTest, transaction_start_of_a_finished_session_fails) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_finished("s1", "2026-08-21T10:30:00Z"));

    EXPECT_FALSE(store->store_transaction_started(make_transaction_start("s1")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_FALSE(actual->transaction.has_value());
}

TEST_F(SessionStoreTest, transaction_start_of_a_stale_session_fails) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 1, "2026-08-21T11:00:00Z")));

    EXPECT_FALSE(store->store_transaction_started(make_transaction_start("s1")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->state, SessionState::Stale);
    EXPECT_FALSE(actual->transaction.has_value());
}

TEST_F(SessionStoreTest, a_second_transaction_of_the_same_session_is_rejected) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1", "2026-08-21T10:30:00Z")));

    EXPECT_FALSE(store->store_transaction_started(make_transaction_start("s1", "2026-08-21T10:45:00Z")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->transaction.has_value());
    EXPECT_EQ(actual->transaction->timestamp_start, "2026-08-21T10:30:00Z");
}

TEST_F(SessionStoreTest, a_transaction_after_a_finished_transaction_is_rejected) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1", "2026-08-21T10:30:00Z")));
    ASSERT_TRUE(store->store_transaction_finished(make_finish("s1", "2026-08-21T11:00:00Z")));

    EXPECT_FALSE(store->store_transaction_started(make_transaction_start("s1", "2026-08-21T11:30:00Z")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->transaction.has_value());
    EXPECT_EQ(actual->transaction->timestamp_start, "2026-08-21T10:30:00Z");
}

TEST_F(SessionStoreTest, transaction_start_after_clear_fails) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_EQ(store->clear_sessions(), 1);

    EXPECT_FALSE(store->store_transaction_started(make_transaction_start("s1")));
    EXPECT_TRUE(all_sessions().empty());
}

// ---------------------------------------------------------------------------
// Finishing a transaction
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, finish_completes_the_transaction) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));
    const auto finish = make_finish("s1", "2026-08-21T11:00:00Z");
    ASSERT_TRUE(store->store_transaction_finished(finish));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->transaction.has_value());
    const auto& stored = actual->transaction.value();
    EXPECT_EQ(stored.timestamp_stop, finish.timestamp_stop);
    ASSERT_TRUE(stored.energy_Wh_import_stop.has_value());
    EXPECT_FLOAT_EQ(stored.energy_Wh_import_stop.value(), finish.energy_Wh_import_stop);
    EXPECT_EQ(stored.stop_reason, finish.stop_reason);
}

TEST_F(SessionStoreTest, finish_without_closing_keeps_the_session_open) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));
    ASSERT_TRUE(store->store_transaction_finished(make_finish("s1", "2026-08-21T11:00:00Z")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->state, SessionState::Open);
    EXPECT_FALSE(actual->timestamp_stop.has_value());
}

TEST_F(SessionStoreTest, a_closing_finish_finishes_the_transaction_and_the_session) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));

    auto finish = make_finish("s1", "2026-08-21T11:00:00Z");
    finish.stop_reason = types::evse_manager::StopTransactionReason::PowerLoss;
    finish.closes_session = true;
    ASSERT_TRUE(store->store_transaction_finished(finish));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->state, SessionState::Finished);
    EXPECT_EQ(actual->timestamp_stop, "2026-08-21T11:00:00Z");
    ASSERT_TRUE(actual->transaction.has_value());
    EXPECT_EQ(actual->transaction->timestamp_stop, "2026-08-21T11:00:00Z");
    ASSERT_TRUE(actual->transaction->energy_Wh_import_stop.has_value());
    EXPECT_FLOAT_EQ(actual->transaction->energy_Wh_import_stop.value(), 5000.0F);
    EXPECT_EQ(actual->transaction->stop_reason, types::evse_manager::StopTransactionReason::PowerLoss);
}

TEST_F(SessionStoreTest, finish_leaves_the_start_fields_untouched) {
    const auto original = make_session("s1", 1, "2026-08-21T10:00:00Z");
    ASSERT_TRUE(store->store_session_started(original));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1", "2026-08-21T10:30:00Z")));
    ASSERT_TRUE(store->store_transaction_finished(make_finish("s1", "2026-08-21T11:00:00Z")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->timestamp_start, original.timestamp_start);
    EXPECT_EQ(actual->evse_id, original.evse_id);
    EXPECT_EQ(actual->evse_id_string, original.evse_id_string);
    EXPECT_EQ(actual->connector_id, original.connector_id);
    ASSERT_TRUE(actual->transaction.has_value());
    EXPECT_EQ(actual->transaction->timestamp_start, "2026-08-21T10:30:00Z");
    EXPECT_FLOAT_EQ(actual->transaction->energy_Wh_import_start, 1000.0F);
}

TEST_F(SessionStoreTest, finish_without_stop_reason) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));
    auto finish = make_finish("s1", "2026-08-21T11:00:00Z");
    finish.stop_reason = std::nullopt;
    ASSERT_TRUE(store->store_transaction_finished(finish));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->transaction.has_value());
    EXPECT_FALSE(actual->transaction->stop_reason.has_value());
}

TEST_F(SessionStoreTest, finish_stores_the_signed_stop_meter_value) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));
    auto finish = make_finish("s1", "2026-08-21T11:00:00Z");
    finish.signed_meter_value_stop = make_signed_meter_value("stop-data");
    ASSERT_TRUE(store->store_transaction_finished(finish));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->transaction.has_value());
    ASSERT_TRUE(actual->transaction->signed_meter_value_stop.has_value());
    EXPECT_EQ(actual->transaction->signed_meter_value_stop.value().signed_meter_data, "stop-data");
}

TEST_F(SessionStoreTest, finish_backfills_a_late_signed_start_meter_value) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));
    auto finish = make_finish("s1", "2026-08-21T11:00:00Z");
    finish.signed_meter_value_start = make_signed_meter_value("late-start-data");
    ASSERT_TRUE(store->store_transaction_finished(finish));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->transaction.has_value());
    ASSERT_TRUE(actual->transaction->signed_meter_value_start.has_value());
    EXPECT_EQ(actual->transaction->signed_meter_value_start.value().signed_meter_data, "late-start-data");
}

TEST_F(SessionStoreTest, finish_does_not_clear_an_existing_signed_start_meter_value) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    auto transaction = make_transaction_start("s1");
    transaction.signed_meter_value_start = make_signed_meter_value("start-data");
    ASSERT_TRUE(store->store_transaction_started(transaction));

    auto finish = make_finish("s1", "2026-08-21T11:00:00Z");
    finish.signed_meter_value_start = std::nullopt;
    ASSERT_TRUE(store->store_transaction_finished(finish));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->transaction.has_value());
    ASSERT_TRUE(actual->transaction->signed_meter_value_start.has_value());
    EXPECT_EQ(actual->transaction->signed_meter_value_start.value().signed_meter_data, "start-data");
}

TEST_F(SessionStoreTest, finish_does_not_overwrite_an_existing_signed_start_meter_value) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    auto transaction = make_transaction_start("s1");
    transaction.signed_meter_value_start = make_signed_meter_value("start-data");
    ASSERT_TRUE(store->store_transaction_started(transaction));

    auto finish = make_finish("s1", "2026-08-21T11:00:00Z");
    finish.signed_meter_value_start = make_signed_meter_value("late-start-data");
    ASSERT_TRUE(store->store_transaction_finished(finish));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->transaction.has_value());
    ASSERT_TRUE(actual->transaction->signed_meter_value_start.has_value());
    EXPECT_EQ(actual->transaction->signed_meter_value_start.value().signed_meter_data, "start-data");
}

TEST_F(SessionStoreTest, finish_of_an_unknown_session_fails) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));

    EXPECT_FALSE(store->store_transaction_finished(make_finish("unknown", "2026-08-21T11:00:00Z")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->transaction.has_value());
    EXPECT_FALSE(actual->transaction->timestamp_stop.has_value());
}

TEST_F(SessionStoreTest, finish_of_a_session_without_a_transaction_fails) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));

    EXPECT_FALSE(store->store_transaction_finished(make_finish("s1", "2026-08-21T11:00:00Z")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_FALSE(actual->transaction.has_value());
    EXPECT_EQ(actual->state, SessionState::Open);
}

TEST_F(SessionStoreTest, a_closing_finish_of_a_session_without_a_transaction_does_not_finish_it) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));

    auto finish = make_finish("s1", "2026-08-21T11:00:00Z");
    finish.closes_session = true;
    EXPECT_FALSE(store->store_transaction_finished(finish));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->state, SessionState::Open);
    EXPECT_FALSE(actual->timestamp_stop.has_value());
}

TEST_F(SessionStoreTest, finish_after_clear_fails) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));
    ASSERT_EQ(store->clear_sessions(), 1);

    EXPECT_FALSE(store->store_transaction_finished(make_finish("s1", "2026-08-21T11:00:00Z")));
    EXPECT_TRUE(all_sessions().empty());
}

TEST_F(SessionStoreTest, second_finish_of_the_same_transaction_fails) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));
    const auto first_finish = make_finish("s1", "2026-08-21T11:00:00Z");
    ASSERT_TRUE(store->store_transaction_finished(first_finish));

    auto second_finish = make_finish("s1", "2026-08-21T12:00:00Z");
    second_finish.stop_reason = types::evse_manager::StopTransactionReason::Other;
    EXPECT_FALSE(store->store_transaction_finished(second_finish));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->transaction.has_value());
    EXPECT_EQ(actual->transaction->timestamp_stop, first_finish.timestamp_stop);
    EXPECT_EQ(actual->transaction->stop_reason, first_finish.stop_reason);
}

TEST_F(SessionStoreTest, finish_of_a_stale_record_fails) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 1, "2026-08-21T11:00:00Z")));

    EXPECT_FALSE(store->store_transaction_finished(make_finish("s1", "2026-08-21T12:00:00Z")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->state, SessionState::Stale);
    ASSERT_TRUE(actual->transaction.has_value());
    EXPECT_FALSE(actual->transaction->timestamp_stop.has_value());
}

// ---------------------------------------------------------------------------
// Finishing a session
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, session_finish_completes_the_record) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));
    ASSERT_TRUE(store->store_transaction_finished(make_finish("s1", "2026-08-21T11:00:00Z")));
    ASSERT_TRUE(store->store_session_finished("s1", "2026-08-21T11:05:00Z"));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->state, SessionState::Finished);
    EXPECT_EQ(actual->timestamp_stop, "2026-08-21T11:05:00Z");
    ASSERT_TRUE(actual->transaction.has_value());
    EXPECT_EQ(actual->transaction->timestamp_stop, "2026-08-21T11:00:00Z");
}

TEST_F(SessionStoreTest, session_finish_works_without_a_transaction) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));

    EXPECT_TRUE(store->store_session_finished("s1", "2026-08-21T10:05:00Z"));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->state, SessionState::Finished);
    EXPECT_EQ(actual->timestamp_stop, "2026-08-21T10:05:00Z");
    EXPECT_FALSE(actual->transaction.has_value());
}

TEST_F(SessionStoreTest, session_finish_of_an_unknown_session_fails) {
    EXPECT_FALSE(store->store_session_finished("unknown", "2026-08-21T11:00:00Z"));
}

TEST_F(SessionStoreTest, session_finish_of_an_already_finished_session_fails) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_finished("s1", "2026-08-21T11:00:00Z"));

    EXPECT_FALSE(store->store_session_finished("s1", "2026-08-21T12:00:00Z"));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->timestamp_stop, "2026-08-21T11:00:00Z");
}

TEST_F(SessionStoreTest, session_finish_of_a_stale_session_fails) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 1, "2026-08-21T11:00:00Z")));

    EXPECT_FALSE(store->store_session_finished("s1", "2026-08-21T12:00:00Z"));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->state, SessionState::Stale);
    EXPECT_FALSE(actual->timestamp_stop.has_value());
}

TEST_F(SessionStoreTest, session_finish_after_clear_fails) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_EQ(store->clear_sessions(), 1);

    EXPECT_FALSE(store->store_session_finished("s1", "2026-08-21T11:00:00Z"));
    EXPECT_TRUE(all_sessions().empty());
}

TEST_F(SessionStoreTest, session_finish_does_not_finish_a_running_transaction) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1")));
    ASSERT_TRUE(store->store_session_finished("s1", "2026-08-21T11:00:00Z"));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->state, SessionState::Finished);
    ASSERT_TRUE(actual->transaction.has_value());
    EXPECT_FALSE(actual->transaction->timestamp_stop.has_value());
    EXPECT_FALSE(actual->transaction->energy_Wh_import_stop.has_value());
}

// ---------------------------------------------------------------------------
// OCPP transaction events
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, ocpp_transaction_id_is_stored) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(
        store->store_ocpp_transaction_event(make_ocpp_event("s1", types::ocpp::TransactionEvent::Started, "42")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->ocpp_transaction_id, "42");
}

TEST_F(SessionStoreTest, ocpp_transaction_id_is_stored_on_a_session_without_a_transaction) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(
        store->store_ocpp_transaction_event(make_ocpp_event("s1", types::ocpp::TransactionEvent::Started, "42")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->ocpp_transaction_id, "42");
    EXPECT_FALSE(actual->transaction.has_value());
}

TEST_F(SessionStoreTest, ocpp_transaction_event_of_an_unknown_session_fails) {
    EXPECT_FALSE(
        store->store_ocpp_transaction_event(make_ocpp_event("unknown", types::ocpp::TransactionEvent::Started, "42")));
}

TEST_F(SessionStoreTest, ocpp_transaction_event_can_be_stored_on_a_finished_record) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_finished("s1", "2026-08-21T11:00:00Z"));

    EXPECT_TRUE(store->store_ocpp_transaction_event(
        make_ocpp_event("s1", types::ocpp::TransactionEvent::Ended, "42", "2026-08-21T10:59:00Z")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->ocpp_transaction_id, "42");
    EXPECT_EQ(actual->ocpp_transaction_timestamp_stop, "2026-08-21T10:59:00Z");
}

TEST_F(SessionStoreTest, ocpp_transaction_event_is_idempotent) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));

    EXPECT_TRUE(
        store->store_ocpp_transaction_event(make_ocpp_event("s1", types::ocpp::TransactionEvent::Started, "42")));
    EXPECT_TRUE(
        store->store_ocpp_transaction_event(make_ocpp_event("s1", types::ocpp::TransactionEvent::Started, "42")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->ocpp_transaction_id, "42");
    EXPECT_EQ(actual->ocpp_transaction_timestamp_start, "2026-08-21T10:20:00Z");
}

TEST_F(SessionStoreTest, ocpp_started_event_stores_the_start_timestamp) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_ocpp_transaction_event(
        make_ocpp_event("s1", types::ocpp::TransactionEvent::Started, "42", "2026-08-21T10:05:00Z")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->ocpp_transaction_id, "42");
    EXPECT_EQ(actual->ocpp_transaction_timestamp_start, "2026-08-21T10:05:00Z");
    EXPECT_FALSE(actual->ocpp_transaction_timestamp_stop.has_value());
}

TEST_F(SessionStoreTest, ocpp_ended_event_stores_the_stop_timestamp) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_ocpp_transaction_event(
        make_ocpp_event("s1", types::ocpp::TransactionEvent::Ended, "42", "2026-08-21T11:05:00Z")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->ocpp_transaction_id, "42");
    EXPECT_EQ(actual->ocpp_transaction_timestamp_stop, "2026-08-21T11:05:00Z");
    EXPECT_FALSE(actual->ocpp_transaction_timestamp_start.has_value());
}

TEST_F(SessionStoreTest, ocpp_updated_event_stores_only_the_transaction_id) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_ocpp_transaction_event(
        make_ocpp_event("s1", types::ocpp::TransactionEvent::Started, std::nullopt, "2026-08-21T10:05:00Z")));

    ASSERT_TRUE(store->store_ocpp_transaction_event(
        make_ocpp_event("s1", types::ocpp::TransactionEvent::Updated, "42", "2026-08-21T10:06:00Z")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->ocpp_transaction_id, "42");
    EXPECT_EQ(actual->ocpp_transaction_timestamp_start, "2026-08-21T10:05:00Z");
    EXPECT_FALSE(actual->ocpp_transaction_timestamp_stop.has_value());
}

TEST_F(SessionStoreTest, ocpp_event_without_a_transaction_id_keeps_the_stored_id) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(
        store->store_ocpp_transaction_event(make_ocpp_event("s1", types::ocpp::TransactionEvent::Updated, "42")));

    ASSERT_TRUE(store->store_ocpp_transaction_event(
        make_ocpp_event("s1", types::ocpp::TransactionEvent::Ended, std::nullopt, "2026-08-21T11:05:00Z")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->ocpp_transaction_id, "42");
    EXPECT_EQ(actual->ocpp_transaction_timestamp_stop, "2026-08-21T11:05:00Z");
}

TEST_F(SessionStoreTest, ocpp_transaction_timestamps_round_trip_via_get_sessions) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 2, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_ocpp_transaction_event(
        make_ocpp_event("s1", types::ocpp::TransactionEvent::Started, "42", "2026-08-21T10:05:00Z")));
    ASSERT_TRUE(store->store_ocpp_transaction_event(
        make_ocpp_event("s1", types::ocpp::TransactionEvent::Ended, "42", "2026-08-21T11:05:00Z")));

    const auto sessions = all_sessions();
    ASSERT_EQ(sessions.size(), 2);
    EXPECT_EQ(sessions.at(0).ocpp_transaction_timestamp_start, "2026-08-21T10:05:00Z");
    EXPECT_EQ(sessions.at(0).ocpp_transaction_timestamp_stop, "2026-08-21T11:05:00Z");
    EXPECT_FALSE(sessions.at(1).ocpp_transaction_timestamp_start.has_value());
    EXPECT_FALSE(sessions.at(1).ocpp_transaction_timestamp_stop.has_value());
}

// ---------------------------------------------------------------------------
// Cost
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, cost_is_stored) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->update_cost("s1", make_session_cost("s1", 1000)));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->cost.has_value());
    ASSERT_TRUE(actual->cost->cost_chunks.has_value());
    ASSERT_EQ(actual->cost->cost_chunks->size(), 1);
    ASSERT_TRUE(actual->cost->cost_chunks->at(0).cost.has_value());
    EXPECT_EQ(actual->cost->cost_chunks->at(0).cost->value, 1000);
}

TEST_F(SessionStoreTest, cost_update_replaces_the_previous_value) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->update_cost("s1", make_session_cost("s1", 1000)));
    ASSERT_TRUE(store->update_cost("s1", make_session_cost("s1", 2000)));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(actual->cost.has_value());
    ASSERT_TRUE(actual->cost->cost_chunks.has_value());
    ASSERT_EQ(actual->cost->cost_chunks->size(), 1);
    ASSERT_TRUE(actual->cost->cost_chunks->at(0).cost.has_value());
    EXPECT_EQ(actual->cost->cost_chunks->at(0).cost->value, 2000);
}

TEST_F(SessionStoreTest, cost_update_of_an_unknown_session_fails) {
    EXPECT_FALSE(store->update_cost("unknown", make_session_cost("unknown", 1000)));
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, lookup_by_known_session_id) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));

    const auto actual = get_by_session_id("s1");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->session_id, "s1");
}

TEST_F(SessionStoreTest, lookup_by_unknown_session_id) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));

    EXPECT_FALSE(get_by_session_id("unknown").has_value());
}

TEST_F(SessionStoreTest, lookup_by_known_ocpp_transaction_id) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(
        store->store_ocpp_transaction_event(make_ocpp_event("s1", types::ocpp::TransactionEvent::Started, "42")));

    const auto actual = get_by_ocpp_transaction_id("42");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->session_id, "s1");
}

TEST_F(SessionStoreTest, lookup_by_unknown_ocpp_transaction_id) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(
        store->store_ocpp_transaction_event(make_ocpp_event("s1", types::ocpp::TransactionEvent::Started, "42")));

    EXPECT_FALSE(get_by_ocpp_transaction_id("43").has_value());
}

TEST_F(SessionStoreTest, lookup_with_an_empty_identifier) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));

    EXPECT_FALSE(store->get_session(SessionIdentifier{}).has_value());
}

TEST_F(SessionStoreTest, lookup_with_both_members_set_prefers_the_session_id) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 2, "2026-08-21T11:00:00Z")));
    ASSERT_TRUE(
        store->store_ocpp_transaction_event(make_ocpp_event("s2", types::ocpp::TransactionEvent::Started, "42")));

    SessionIdentifier identifier{};
    identifier.session_id = "s1";
    identifier.ocpp_transaction_id = "42";

    const auto actual = store->get_session(identifier);
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->session_id, "s1");
}

TEST_F(SessionStoreTest, lookup_by_shared_ocpp_transaction_id_returns_the_newest) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 2, "2026-08-21T11:00:00Z")));
    ASSERT_TRUE(
        store->store_ocpp_transaction_event(make_ocpp_event("s1", types::ocpp::TransactionEvent::Started, "42")));
    ASSERT_TRUE(
        store->store_ocpp_transaction_event(make_ocpp_event("s2", types::ocpp::TransactionEvent::Started, "42")));

    const auto actual = get_by_ocpp_transaction_id("42");
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->session_id, "s2");
}

// ---------------------------------------------------------------------------
// Clearing
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, clear_deletes_all_records) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 2, "2026-08-21T11:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s3", 3, "2026-08-21T12:00:00Z")));

    EXPECT_EQ(store->clear_sessions(), 3);
    EXPECT_TRUE(all_sessions().empty());
}

TEST_F(SessionStoreTest, clear_of_an_empty_table_deletes_nothing) {
    EXPECT_EQ(store->clear_sessions(), 0);
}

TEST_F(SessionStoreTest, insert_after_clear_works) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_EQ(store->clear_sessions(), 1);

    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T11:00:00Z")));

    const auto sessions = all_sessions();
    ASSERT_EQ(sessions.size(), 1);
    EXPECT_EQ(sessions.at(0).session_id, "s1");
    EXPECT_EQ(sessions.at(0).timestamp_start, "2026-08-21T11:00:00Z");
}

// ---------------------------------------------------------------------------
// Paging
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, a_page_holds_at_most_the_requested_number_of_records) {
    store_records(*store, 5);

    const auto page = store->get_sessions(make_request(2));
    EXPECT_EQ(page.sessions.size(), 2);
    EXPECT_TRUE(page.continuation_token.has_value());
}

TEST_F(SessionStoreTest, paging_returns_every_record_exactly_once) {
    store_records(*store, 5);

    const auto first = store->get_sessions(make_request(2));
    ASSERT_TRUE(first.continuation_token.has_value());
    const auto second = store->get_sessions(make_request(2, first.continuation_token));
    ASSERT_TRUE(second.continuation_token.has_value());
    const auto third = store->get_sessions(make_request(2, second.continuation_token));

    EXPECT_EQ(session_ids(first.sessions), std::vector<std::string>({"s1", "s2"}));
    EXPECT_EQ(session_ids(second.sessions), std::vector<std::string>({"s3", "s4"}));
    EXPECT_EQ(session_ids(third.sessions), std::vector<std::string>({"s5"}));
    EXPECT_FALSE(third.continuation_token.has_value());
}

TEST_F(SessionStoreTest, a_record_count_that_is_a_multiple_of_the_limit_ends_with_an_empty_page) {
    store_records(*store, 4);

    const auto first = store->get_sessions(make_request(2));
    ASSERT_TRUE(first.continuation_token.has_value());
    const auto second = store->get_sessions(make_request(2, first.continuation_token));
    ASSERT_TRUE(second.continuation_token.has_value());
    const auto third = store->get_sessions(make_request(2, second.continuation_token));

    EXPECT_EQ(first.sessions.size(), 2);
    EXPECT_EQ(second.sessions.size(), 2);
    EXPECT_TRUE(third.sessions.empty());
    EXPECT_FALSE(third.continuation_token.has_value());
}

TEST_F(SessionStoreTest, a_request_without_a_limit_returns_everything_that_fits_the_default_page) {
    store_records(*store, 3);

    const auto page = store->get_sessions(GetSessionsRequest{});
    EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"s1", "s2", "s3"}));
    EXPECT_FALSE(page.continuation_token.has_value());
}

TEST_F(SessionStoreTest, a_limit_above_the_maximum_is_clamped) {
    auto big_store = make_store(1000);
    ASSERT_TRUE(big_store->open());
    store_records(*big_store, 501);

    const auto first = big_store->get_sessions(make_request(9999));
    ASSERT_EQ(first.sessions.size(), 500);
    ASSERT_TRUE(first.continuation_token.has_value());

    const auto second = big_store->get_sessions(make_request(9999, first.continuation_token));
    EXPECT_EQ(second.sessions.size(), 1);
    EXPECT_FALSE(second.continuation_token.has_value());
}

TEST_F(SessionStoreTest, a_malformed_token_starts_from_the_beginning) {
    store_records(*store, 3);

    const auto page = store->get_sessions(make_request(1, "not-a-token"));
    EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"s1"}));
}

TEST_F(SessionStoreTest, a_token_of_another_database_epoch_starts_from_the_beginning) {
    store_records(*store, 3);

    const auto page = store->get_sessions(make_request(1, "00000000deadbeef:3"));
    EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"s1"}));
}

TEST_F(SessionStoreTest, a_token_beyond_the_newest_record_returns_an_empty_page) {
    store_records(*store, 3);

    const auto first = store->get_sessions(make_request(1));
    ASSERT_TRUE(first.continuation_token.has_value());

    const auto page = store->get_sessions(make_request(1, token_with_id(first.continuation_token.value(), 1000)));
    EXPECT_TRUE(page.sessions.empty());
    EXPECT_FALSE(page.continuation_token.has_value());
}

TEST_F(SessionStoreTest, a_token_stays_valid_for_a_new_store_instance_of_the_same_database) {
    store_records(*store, 3);

    const auto first = store->get_sessions(make_request(1));
    ASSERT_TRUE(first.continuation_token.has_value());
    store.reset();

    auto second_store = make_store(DEFAULT_MAX_SESSIONS);
    ASSERT_TRUE(second_store->open());

    const auto page = second_store->get_sessions(make_request(std::nullopt, first.continuation_token));
    EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"s2", "s3"}));
}

TEST_F(SessionStoreTest, a_token_of_a_pruned_record_resumes_at_the_oldest_surviving_record) {
    auto small_store = make_store(3);
    ASSERT_TRUE(small_store->open());
    store_records(*small_store, 3);

    const auto first = small_store->get_sessions(make_request(1));
    ASSERT_TRUE(first.continuation_token.has_value());

    for (const auto* session_id : {"s4", "s5", "s6"}) {
        ASSERT_TRUE(small_store->store_session_started(make_session(session_id, 1, "2026-08-21T11:00:00Z")));
    }

    const auto page = small_store->get_sessions(make_request(std::nullopt, first.continuation_token));
    EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"s4", "s5", "s6"}));
}

TEST_F(SessionStoreTest, a_token_taken_before_a_clear_returns_the_records_stored_afterwards) {
    store_records(*store, 2);

    const auto first = store->get_sessions(make_request(1));
    ASSERT_TRUE(first.continuation_token.has_value());
    ASSERT_EQ(store->clear_sessions(), 2);
    ASSERT_TRUE(store->store_session_started(make_session("s3", 1, "2026-08-21T11:00:00Z")));

    const auto page = store->get_sessions(make_request(std::nullopt, first.continuation_token));
    EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"s3"}));
}

// ---------------------------------------------------------------------------
// Filtering
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, the_state_filter_returns_only_records_in_that_state) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_finished("s1", "2026-08-21T10:30:00Z"));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 2, "2026-08-21T11:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s3", 2, "2026-08-21T12:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s4", 3, "2026-08-21T13:00:00Z")));

    SessionFilter filter{};
    filter.state = SessionState::Open;

    const auto first = store->get_sessions(make_request(1, std::nullopt, filter));
    ASSERT_TRUE(first.continuation_token.has_value());
    const auto second = store->get_sessions(make_request(1, first.continuation_token, filter));

    EXPECT_EQ(session_ids(first.sessions), std::vector<std::string>({"s3"}));
    EXPECT_EQ(session_ids(second.sessions), std::vector<std::string>({"s4"}));
    EXPECT_EQ(session_ids(page_through_all(*store, filter, 1)), std::vector<std::string>({"s3", "s4"}));
}

TEST_F(SessionStoreTest, the_evse_id_filter_returns_only_records_of_that_evse) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 2, "2026-08-21T11:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s3", 1, "2026-08-21T12:00:00Z")));

    SessionFilter filter{};
    filter.evse_id = 1;

    const auto page = store->get_sessions(make_request(std::nullopt, std::nullopt, filter));
    EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"s1", "s3"}));
}

TEST_F(SessionStoreTest, the_started_after_filter_excludes_the_boundary_timestamp) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 2, "2026-08-21T11:00:00Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s3", 3, "2026-08-21T12:00:00Z")));

    SessionFilter filter{};
    filter.started_after = "2026-08-21T11:00:00Z";

    const auto page = store->get_sessions(make_request(std::nullopt, std::nullopt, filter));
    EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"s3"}));
}

TEST_F(SessionStoreTest, the_started_after_filter_uses_the_session_start_not_the_transaction_start) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(store->store_transaction_started(make_transaction_start("s1", "2026-08-21T12:00:00Z")));

    SessionFilter filter{};
    filter.started_after = "2026-08-21T11:00:00Z";

    const auto page = store->get_sessions(make_request(std::nullopt, std::nullopt, filter));
    EXPECT_TRUE(page.sessions.empty());
}

TEST_F(SessionStoreTest, the_started_after_filter_compares_instants_not_timestamp_spellings) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:59:59.999Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 2, "2026-08-21T11:00:00.500Z")));

    SessionFilter filter{};
    filter.started_after = "2026-08-21T11:00:00Z";

    const auto page = store->get_sessions(make_request(std::nullopt, std::nullopt, filter));
    EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"s2"}));
}

TEST_F(SessionStoreTest, the_started_after_filter_accepts_utc_offset_timestamps) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00.000Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 2, "2026-08-21T12:00:00.000Z")));

    SessionFilter filter{};
    filter.started_after = "2026-08-21T13:00:00+02:00"; // 11:00:00Z

    const auto page = store->get_sessions(make_request(std::nullopt, std::nullopt, filter));
    EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"s2"}));
}

TEST_F(SessionStoreTest, the_started_after_filter_excludes_the_boundary_in_any_spelling) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T11:00:00Z")));

    SessionFilter filter{};
    filter.started_after = "2026-08-21T11:00:00.000Z";

    const auto page = store->get_sessions(make_request(std::nullopt, std::nullopt, filter));
    EXPECT_TRUE(page.sessions.empty());
}

TEST_F(SessionStoreTest, the_started_after_filter_accepts_nanosecond_timestamps) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00.000Z")));
    ASSERT_TRUE(store->store_session_started(make_session("s2", 2, "2026-08-21T12:00:00.000Z")));

    SessionFilter filter{};
    filter.started_after = "2026-08-21T11:00:00.123456789Z";

    const auto page = store->get_sessions(make_request(std::nullopt, std::nullopt, filter));
    EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"s2"}));
}

TEST_F(SessionStoreTest, a_started_after_value_that_is_not_a_timestamp_is_ignored) {
    ASSERT_TRUE(store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));

    for (const auto* started_after : {"yesterday", "2026-08-21T10:00:00Z extra"}) {
        SessionFilter filter{};
        filter.started_after = started_after;

        const auto page = store->get_sessions(make_request(std::nullopt, std::nullopt, filter));
        EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"s1"})) << started_after;
    }
}

// ---------------------------------------------------------------------------
// Page size budget and unreadable records
// ---------------------------------------------------------------------------

TEST_F(SessionStoreTest, a_page_stops_early_when_the_records_get_too_large) {
    for (const auto* session_id : {"s1", "s2", "s3"}) {
        ASSERT_TRUE(store->store_session_started(make_session(session_id, 1, "2026-08-21T10:00:00Z")));
        auto transaction = make_transaction_start(session_id);
        transaction.signed_meter_value_start = make_signed_meter_value(std::string(300 * 1024, 'x'));
        ASSERT_TRUE(store->store_transaction_started(transaction));
    }

    const auto page = store->get_sessions(make_request(10));
    EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"s1"}));
    ASSERT_TRUE(page.continuation_token.has_value());
    EXPECT_EQ(session_ids(page_through_all(*store, std::nullopt, 10)), std::vector<std::string>({"s1", "s2", "s3"}));
}

TEST_F(SessionStoreTest, an_unreadable_record_does_not_stall_the_iteration) {
    store_records(*store, 3);
    corrupt_start_reason("s2");

    EXPECT_EQ(session_ids(all_sessions()), std::vector<std::string>({"s1", "s3"}));
    EXPECT_EQ(session_ids(page_through_all(*store, std::nullopt, 1)), std::vector<std::string>({"s1", "s3"}));
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

class SessionStoreFileTest : public ::testing::Test {
protected:
    std::filesystem::path m_database_path{};
    std::filesystem::path m_other_database_path{};

    void SetUp() override {
        const auto* const test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        const auto prefix =
            std::filesystem::temp_directory_path() / ("persistent_session_storage_" + std::string(test_info->name()));
        m_database_path = prefix.string() + ".db";
        m_other_database_path = prefix.string() + "_other.db";
        remove_database_files();
    }

    void TearDown() override {
        remove_database_files();
    }

    void remove_database_files() {
        for (const auto& path : {m_database_path, m_other_database_path}) {
            std::filesystem::remove(path);
            std::filesystem::remove(path.string() + "-wal");
            std::filesystem::remove(path.string() + "-shm");
        }
    }

    std::unique_ptr<SessionStore> make_store(const std::filesystem::path& database_path) {
        return std::make_unique<SessionStore>(std::make_unique<Connection>(database_path),
                                              std::filesystem::path{MIGRATION_FILES_PATH},
                                              SESSION_STORAGE_MIGRATION_FILE_VERSION, DEFAULT_MAX_SESSIONS);
    }
};

TEST_F(SessionStoreFileTest, records_survive_the_store_instance) {
    {
        auto writing_store = make_store(m_database_path);
        ASSERT_TRUE(writing_store->open());
        ASSERT_TRUE(writing_store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
        ASSERT_TRUE(writing_store->store_transaction_started(make_transaction_start("s1", "2026-08-21T10:30:00Z")));
        ASSERT_TRUE(writing_store->store_transaction_finished(make_finish("s1", "2026-08-21T11:00:00Z")));
        ASSERT_TRUE(writing_store->store_session_finished("s1", "2026-08-21T11:05:00Z"));
    }

    auto reading_store = make_store(m_database_path);
    ASSERT_TRUE(reading_store->open());

    const auto sessions = page_through_all(*reading_store);
    ASSERT_EQ(sessions.size(), 1);
    EXPECT_EQ(sessions.at(0).session_id, "s1");
    EXPECT_EQ(sessions.at(0).state, SessionState::Finished);
    EXPECT_EQ(sessions.at(0).timestamp_stop, "2026-08-21T11:05:00Z");
    ASSERT_TRUE(sessions.at(0).transaction.has_value());
    EXPECT_EQ(sessions.at(0).transaction->timestamp_stop, "2026-08-21T11:00:00Z");
}

TEST_F(SessionStoreFileTest, a_session_without_a_transaction_survives_the_store_instance) {
    {
        auto writing_store = make_store(m_database_path);
        ASSERT_TRUE(writing_store->open());
        ASSERT_TRUE(writing_store->store_session_started(make_session("s1", 1, "2026-08-21T10:00:00Z")));
        ASSERT_TRUE(writing_store->store_session_finished("s1", "2026-08-21T10:05:00Z"));
    }

    auto reading_store = make_store(m_database_path);
    ASSERT_TRUE(reading_store->open());

    const auto sessions = page_through_all(*reading_store);
    ASSERT_EQ(sessions.size(), 1);
    EXPECT_EQ(sessions.at(0).state, SessionState::Finished);
    EXPECT_FALSE(sessions.at(0).transaction.has_value());
}

TEST_F(SessionStoreFileTest, a_token_of_another_database_starts_from_the_beginning) {
    auto first_store = make_store(m_database_path);
    ASSERT_TRUE(first_store->open());
    ASSERT_TRUE(first_store->store_session_started(make_session("a1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(first_store->store_session_started(make_session("a2", 2, "2026-08-21T11:00:00Z")));

    auto second_store = make_store(m_other_database_path);
    ASSERT_TRUE(second_store->open());
    ASSERT_TRUE(second_store->store_session_started(make_session("b1", 1, "2026-08-21T10:00:00Z")));
    ASSERT_TRUE(second_store->store_session_started(make_session("b2", 2, "2026-08-21T11:00:00Z")));

    const auto foreign_page = first_store->get_sessions(make_request(1));
    ASSERT_TRUE(foreign_page.continuation_token.has_value());

    const auto page = second_store->get_sessions(make_request(1, foreign_page.continuation_token));
    EXPECT_EQ(session_ids(page.sessions), std::vector<std::string>({"b1"}));
}

} // namespace
