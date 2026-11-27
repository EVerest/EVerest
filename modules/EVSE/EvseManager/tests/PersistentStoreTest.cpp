// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <PersistentStore.hpp>
#include <utils/date.hpp>

#include "StoreAdapterStub.hpp"

namespace {
using namespace module;

struct PersistentStoreTest : public testing::Test {
    stub::StoreAdapterStub adapter;
    std::vector<std::unique_ptr<kvsIntf>> kvs;
    std::unique_ptr<PersistentStore> store;

    void SetUp() override {
        kvs.push_back(std::make_unique<kvsIntf>(&adapter, Requirement{"store", 0}, "EVSE", std::nullopt));
        store = std::make_unique<PersistentStore>(kvs, "EVSE", "boot-2", PersistentStore::Writes::Immediate);
    }

    static StoredTransaction transaction() {
        StoredTransaction stored;
        stored.session_uuid = "session-1";
        types::authorization::ProvidedIdToken token;
        token.id_token.value = "TOKEN";
        token.id_token.type = types::authorization::IdTokenType::ISO14443;
        token.authorization_type = types::authorization::AuthorizationType::RFID;
        stored.id_token = token;
        stored.boot_id = "boot-1";
        stored.clean_shutdown = true;
        return stored;
    }
};

// kvs calls answered over MQTT must not block the state machine that changes the record
TEST_F(PersistentStoreTest, BackgroundWritesDoNotBlockTheCaller) {
    stub::BlockingStoreAdapterStub blocking;
    std::vector<std::unique_ptr<kvsIntf>> blocking_kvs;
    blocking_kvs.push_back(std::make_unique<kvsIntf>(&blocking, Requirement{"store", 0}, "EVSE", std::nullopt));
    PersistentStore background(blocking_kvs, "EVSE", "boot-2");

    background.store_transaction(transaction());
    background.touch_last_alive();
    background.mark_clean_shutdown();
    EXPECT_TRUE(blocking.values.empty());

    blocking.release();
    background.flush();
    ASSERT_EQ(blocking.values.count("EVSE_session"), 1);
    EXPECT_TRUE(blocking.values.at("EVSE_session").at("clean_shutdown").get<bool>());
}

TEST_F(PersistentStoreTest, BackgroundClearIsWrittenInOrder) {
    stub::BlockingStoreAdapterStub blocking;
    blocking.release();
    std::vector<std::unique_ptr<kvsIntf>> blocking_kvs;
    blocking_kvs.push_back(std::make_unique<kvsIntf>(&blocking, Requirement{"store", 0}, "EVSE", std::nullopt));
    PersistentStore background(blocking_kvs, "EVSE", "boot-2");

    background.store_transaction(transaction());
    background.clear_session();
    background.flush();
    EXPECT_EQ(blocking.values.at("EVSE_session"), "");
}

TEST_F(PersistentStoreTest, NothingStored) {
    EXPECT_FALSE(store->get_transaction().has_value());
}

TEST_F(PersistentStoreTest, StoreStampsBootIdAndLastAlive) {
    const auto before = date::utc_clock::now();
    store->store_transaction(transaction());

    ASSERT_TRUE(adapter.values.count("EVSE_session"));
    EXPECT_TRUE(adapter.values["EVSE_session"].is_object());

    // a fresh store reads it back from the kvs, not from its cache
    PersistentStore reloaded(kvs, "EVSE", "boot-3", PersistentStore::Writes::Immediate);
    const auto stored = reloaded.get_transaction();
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->session_uuid, "session-1");
    ASSERT_TRUE(stored->id_token.has_value());
    EXPECT_EQ(stored->id_token->id_token.value, "TOKEN");
    EXPECT_EQ(stored->boot_id, "boot-2");
    EXPECT_FALSE(stored->clean_shutdown);
    ASSERT_TRUE(stored->last_alive.has_value());
    EXPECT_GE(Everest::Date::from_rfc3339(stored->last_alive.value()), before - std::chrono::seconds(1));
}

TEST_F(PersistentStoreTest, LegacySessionString) {
    adapter.values["EVSE_session"] = "legacy-session";
    const auto stored = store->get_transaction();
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->session_uuid, "legacy-session");
    EXPECT_FALSE(stored->id_token.has_value());
    EXPECT_FALSE(stored->boot_id.has_value());
}

TEST_F(PersistentStoreTest, ClearedSession) {
    store->store_transaction(transaction());
    store->clear_session();
    EXPECT_FALSE(store->get_transaction().has_value());
    PersistentStore reloaded(kvs, "EVSE", "boot-3", PersistentStore::Writes::Immediate);
    EXPECT_FALSE(reloaded.get_transaction().has_value());
}

TEST_F(PersistentStoreTest, EmptyLegacyValueMeansNoSession) {
    adapter.values["EVSE_session"] = "";
    EXPECT_FALSE(store->get_transaction().has_value());
}

TEST_F(PersistentStoreTest, MarkCleanShutdown) {
    store->store_transaction(transaction());
    store->mark_clean_shutdown();
    PersistentStore reloaded(kvs, "EVSE", "boot-3", PersistentStore::Writes::Immediate);
    const auto stored = reloaded.get_transaction();
    ASSERT_TRUE(stored.has_value());
    EXPECT_TRUE(stored->clean_shutdown);
    EXPECT_EQ(stored->session_uuid, "session-1");
}

TEST_F(PersistentStoreTest, MarkCleanShutdownAfterLoad) {
    store->store_transaction(transaction());
    PersistentStore reloaded(kvs, "EVSE", "boot-3", PersistentStore::Writes::Immediate);
    ASSERT_TRUE(reloaded.get_transaction().has_value());
    reloaded.mark_clean_shutdown();
    EXPECT_TRUE(adapter.values["EVSE_session"].at("clean_shutdown").get<bool>());
}

TEST_F(PersistentStoreTest, MarkCleanShutdownWithoutTransactionWritesNothing) {
    store->mark_clean_shutdown();
    store->touch_last_alive();
    EXPECT_TRUE(adapter.stored_keys.empty());
}

TEST_F(PersistentStoreTest, TouchLastAlive) {
    store->store_transaction(transaction());
    adapter.values["EVSE_session"]["last_alive"] = "2020-01-01T00:00:00.000Z";
    PersistentStore reloaded(kvs, "EVSE", "boot-2", PersistentStore::Writes::Immediate);
    ASSERT_TRUE(reloaded.get_transaction().has_value());
    reloaded.touch_last_alive();
    EXPECT_NE(adapter.values["EVSE_session"].at("last_alive").get<std::string>(), "2020-01-01T00:00:00.000Z");
}

TEST_F(PersistentStoreTest, StartMeterValue) {
    store->store_transaction(transaction());
    types::powermeter::Powermeter meter;
    meter.timestamp = "2026-11-24T10:00:00.000Z";
    meter.energy_Wh_import.total = 1234.0F;
    store->set_start_meter_value(meter);

    PersistentStore reloaded(kvs, "EVSE", "boot-3", PersistentStore::Writes::Immediate);
    const auto stored = reloaded.get_transaction();
    ASSERT_TRUE(stored.has_value());
    ASSERT_TRUE(stored->start_meter_value.has_value());
    EXPECT_FLOAT_EQ(stored->start_meter_value->energy_Wh_import.total, 1234.0F);
}

TEST_F(PersistentStoreTest, StartMeterValueWithoutTransactionWritesNothing) {
    types::powermeter::Powermeter meter;
    meter.timestamp = "2026-11-24T10:00:00.000Z";
    store->set_start_meter_value(meter);
    EXPECT_TRUE(adapter.stored_keys.empty());
}

TEST_F(PersistentStoreTest, MarkResumeAfterReset) {
    store->store_transaction(transaction());
    adapter.values["EVSE_session"]["last_alive"] = "2020-01-01T00:00:00.000Z";
    PersistentStore reloaded(kvs, "EVSE", "boot-2", PersistentStore::Writes::Immediate);
    ASSERT_TRUE(reloaded.get_transaction().has_value());
    EXPECT_TRUE(reloaded.mark_resume_after_reset(true));

    const auto& value = adapter.values["EVSE_session"];
    EXPECT_TRUE(value.at("resume_after_reset").get<bool>());
    EXPECT_TRUE(value.at("energy_transfer_paused").get<bool>());
    EXPECT_NE(value.at("last_alive").get<std::string>(), "2020-01-01T00:00:00.000Z");
}

TEST_F(PersistentStoreTest, StoreTransactionClearsResumeAfterReset) {
    auto stored = transaction();
    stored.resume_after_reset = true;
    stored.energy_transfer_paused = true;
    store->store_transaction(stored);
    EXPECT_FALSE(adapter.values["EVSE_session"].value("resume_after_reset", false));
    EXPECT_FALSE(adapter.values["EVSE_session"].value("energy_transfer_paused", false));
}

TEST_F(PersistentStoreTest, ClearResumeAfterReset) {
    store->store_transaction(transaction());
    ASSERT_TRUE(store->mark_resume_after_reset(true));
    store->clear_resume_after_reset();

    const auto& value = adapter.values["EVSE_session"];
    EXPECT_FALSE(value.value("resume_after_reset", false));
    EXPECT_FALSE(value.value("energy_transfer_paused", false));
    ASSERT_TRUE(store->get_transaction().has_value());
    EXPECT_FALSE(store->get_transaction()->resume_after_reset);
}

TEST_F(PersistentStoreTest, ClearResumeAfterResetWithoutTransactionStoresNothing) {
    store->clear_resume_after_reset();
    EXPECT_TRUE(adapter.stored_keys.empty());
}

TEST_F(PersistentStoreTest, MarkResumeAfterResetWithoutTransactionFails) {
    EXPECT_FALSE(store->mark_resume_after_reset(false));
    EXPECT_TRUE(adapter.stored_keys.empty());
}

TEST_F(PersistentStoreTest, WithoutKvsNothingIsStored) {
    std::vector<std::unique_ptr<kvsIntf>> no_kvs;
    PersistentStore inactive(no_kvs, "EVSE", "boot-2");
    inactive.store_transaction(transaction());
    inactive.mark_clean_shutdown();
    inactive.touch_last_alive();
    EXPECT_FALSE(inactive.mark_resume_after_reset(false));
    EXPECT_FALSE(inactive.get_transaction().has_value());
}

} // namespace
