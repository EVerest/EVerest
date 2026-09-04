// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generated/types/authorization.hpp>
#include <generated/types/evse_manager.hpp>
#include <generated/types/ocpp.hpp>
#include <generated/types/powermeter.hpp>
#include <generated/types/session_cost.hpp>
#include <generated/types/session_storage.hpp>
#include <generated/types/units.hpp>
#include <generated/types/units_signed.hpp>

#include <storage/record_conversions.hpp>
#include <storage/session_recorder.hpp>
#include <storage/session_store.hpp>

namespace {

using module::storage::EvseSessionRecorder;
using module::storage::OcppTransactionEvent;
using module::storage::RecorderConfig;
using module::storage::SessionStart;
using module::storage::SessionStoreInterface;
using module::storage::TransactionFinish;
using module::storage::TransactionStart;
using types::authorization::AuthorizationType;
using types::authorization::IdTokenType;
using types::authorization::ProvidedIdToken;
using types::evse_manager::SessionEvent;
using types::evse_manager::SessionEventEnum;
using types::evse_manager::StartSessionReason;
using types::evse_manager::StopTransactionReason;
using types::session_storage::Session;
using types::session_storage::SessionIdentifier;
using types::units_signed::SignedMeterValue;

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

constexpr auto TIMESTAMP_START = "2026-08-21T10:00:00Z";
constexpr auto TIMESTAMP_STOP = "2026-08-21T12:00:00Z";
constexpr auto SESSION_ID = "session-1";
constexpr std::int32_t EVSE_ID = 1;
constexpr auto EVSE_ID_STRING = "DE*PNX*E1";

class MockSessionStore : public SessionStoreInterface {
public:
    MOCK_METHOD(bool, store_session_started, (const SessionStart& session), (override));
    MOCK_METHOD(bool, store_transaction_started, (const TransactionStart& transaction), (override));
    MOCK_METHOD(bool, store_transaction_finished, (const TransactionFinish& finish), (override));
    MOCK_METHOD(bool, store_session_finished, (const std::string& session_id, const std::string& timestamp_stop),
                (override));
    MOCK_METHOD(bool, store_ocpp_transaction_event, (const OcppTransactionEvent& event), (override));
    MOCK_METHOD(bool, update_cost, (const std::string& session_id, const types::session_cost::SessionCost& cost),
                (override));
    MOCK_METHOD(types::session_storage::SessionList, get_sessions,
                (const types::session_storage::GetSessionsRequest& request), (override));
    MOCK_METHOD(std::optional<Session>, get_session, (const SessionIdentifier& identifier), (override));
    MOCK_METHOD(int, clear_sessions, (), (override));
};

types::units::Energy make_energy(float total, bool with_phases) {
    types::units::Energy energy{};
    energy.total = total;
    if (with_phases) {
        energy.L1 = total / 2.0F;
        energy.L2 = total / 4.0F;
        energy.L3 = total / 8.0F;
    }
    return energy;
}

types::powermeter::Powermeter make_powermeter(float total, bool with_phases = true) {
    types::powermeter::Powermeter meter_value{};
    meter_value.timestamp = TIMESTAMP_START;
    meter_value.energy_Wh_import = make_energy(total, with_phases);
    meter_value.meter_id = "meter-1";
    meter_value.energy_Wh_export = make_energy(total / 10.0F, with_phases);
    return meter_value;
}

SignedMeterValue make_signed_meter_value(const std::string& data) {
    SignedMeterValue signed_meter_value{};
    signed_meter_value.signed_meter_data = data;
    signed_meter_value.signing_method = "ECDSA-secp256r1-SHA256";
    signed_meter_value.encoding_method = "OCMF";
    signed_meter_value.public_key = "public-key";
    signed_meter_value.timestamp = TIMESTAMP_START;
    return signed_meter_value;
}

ProvidedIdToken make_id_token(const std::string& value, IdTokenType id_token_type,
                              AuthorizationType authorization_type) {
    ProvidedIdToken id_tag{};
    id_tag.id_token.value = value;
    id_tag.id_token.type = id_token_type;
    id_tag.authorization_type = authorization_type;
    return id_tag;
}

SessionEvent make_session_event(SessionEventEnum event, const std::string& uuid,
                                const std::string& timestamp = TIMESTAMP_START) {
    SessionEvent session_event{};
    session_event.uuid = uuid;
    session_event.timestamp = timestamp;
    session_event.event = event;
    return session_event;
}

SessionEvent make_session_started_event(const std::string& uuid,
                                        StartSessionReason reason = StartSessionReason::EVConnected,
                                        const std::string& timestamp = TIMESTAMP_START) {
    auto event = make_session_event(SessionEventEnum::SessionStarted, uuid, timestamp);
    types::evse_manager::SessionStarted session_started{};
    session_started.reason = reason;
    session_started.meter_value = make_powermeter(100.0F);
    event.session_started = session_started;
    return event;
}

SessionEvent make_transaction_started_event(const std::string& uuid, const std::string& timestamp = TIMESTAMP_START,
                                            const ProvidedIdToken& id_tag = make_id_token("TOKEN-1",
                                                                                          IdTokenType::ISO14443,
                                                                                          AuthorizationType::RFID),
                                            const types::powermeter::Powermeter& meter_value = make_powermeter(1000.0F),
                                            const std::optional<SignedMeterValue>& signed_meter_value = std::nullopt) {
    auto event = make_session_event(SessionEventEnum::TransactionStarted, uuid, timestamp);
    types::evse_manager::TransactionStarted transaction_started{};
    transaction_started.id_tag = id_tag;
    transaction_started.meter_value = meter_value;
    transaction_started.signed_meter_value = signed_meter_value;
    event.transaction_started = transaction_started;
    return event;
}

SessionEvent make_transaction_finished_event(
    const std::string& uuid, const std::string& timestamp = TIMESTAMP_STOP,
    const types::powermeter::Powermeter& meter_value = make_powermeter(2000.0F),
    const std::optional<StopTransactionReason>& reason = StopTransactionReason::EVDisconnected,
    const std::optional<SignedMeterValue>& signed_meter_value = std::nullopt,
    const std::optional<SignedMeterValue>& start_signed_meter_value = std::nullopt) {
    auto event = make_session_event(SessionEventEnum::TransactionFinished, uuid, timestamp);
    types::evse_manager::TransactionFinished transaction_finished{};
    transaction_finished.meter_value = meter_value;
    transaction_finished.reason = reason;
    transaction_finished.signed_meter_value = signed_meter_value;
    transaction_finished.start_signed_meter_value = start_signed_meter_value;
    event.transaction_finished = transaction_finished;
    return event;
}

types::session_cost::SessionCost make_session_cost(const std::string& session_id, int cost_value) {
    types::money::Currency currency{};
    currency.code = types::money::CurrencyCode::EUR;
    currency.decimals = 2;

    types::session_cost::SessionCostChunk chunk{};
    chunk.timestamp_from = TIMESTAMP_START;
    chunk.timestamp_to = TIMESTAMP_STOP;
    chunk.cost = types::money::MoneyAmount{cost_value};
    chunk.category = types::session_cost::CostCategory::Energy;

    types::session_cost::SessionCost cost{};
    cost.session_id = session_id;
    cost.currency = currency;
    cost.status = types::session_cost::SessionStatus::Finished;
    cost.cost_chunks = std::vector<types::session_cost::SessionCostChunk>{chunk};
    cost.qr_code = "https://example.invalid/invoice";
    return cost;
}

types::ocpp::OcppTransactionEvent
make_ocpp_transaction_event(const std::string& session_id, const std::optional<std::string>& transaction_id,
                            types::ocpp::TransactionEvent transaction_event = types::ocpp::TransactionEvent::Started,
                            const std::string& timestamp = TIMESTAMP_START) {
    types::ocpp::OcppTransactionEvent event{};
    event.transaction_event = transaction_event;
    event.session_id = session_id;
    event.transaction_id = transaction_id;
    event.timestamp = timestamp;
    return event;
}

/// \brief Recorder with the default configuration, i.e. signed meter values not stored
class EvseSessionRecorderTest : public testing::Test {
protected:
    void SetUp() override {
        recorder.set_evse(EVSE_ID, EVSE_ID_STRING);
    }

    NiceMock<MockSessionStore> store{};
    EvseSessionRecorder recorder{store, RecorderConfig{}};
};

/// \brief Recorder configured to store signed meter values
class EvseSessionRecorderSignedTest : public testing::Test {
protected:
    void SetUp() override {
        recorder.set_evse(EVSE_ID, EVSE_ID_STRING);
    }

    NiceMock<MockSessionStore> store{};
    EvseSessionRecorder recorder{store, RecorderConfig{true}};
};

/// \brief Recorder whose store rejects every unexpected call
class EvseSessionRecorderStrictTest : public testing::Test {
protected:
    void SetUp() override {
        recorder.set_evse(EVSE_ID, EVSE_ID_STRING);
    }

    StrictMock<MockSessionStore> store{};
    EvseSessionRecorder recorder{store, RecorderConfig{}};
};

/// \brief Store shared by the stateless forwarding functions
class SessionForwardingTest : public testing::Test {
protected:
    NiceMock<MockSessionStore> store{};
};

// --- session record building -------------------------------------------------

TEST_F(EvseSessionRecorderTest, session_started_stores_one_record) {
    SessionStart captured{};
    EXPECT_CALL(store, store_session_started(_)).WillOnce([&captured](const SessionStart& session) {
        captured = session;
        return true;
    });

    recorder.on_session_event(make_session_started_event(SESSION_ID));

    EXPECT_EQ(captured.session_id, SESSION_ID);
    EXPECT_EQ(captured.evse_id, EVSE_ID);
    EXPECT_EQ(captured.evse_id_string, EVSE_ID_STRING);
    EXPECT_EQ(captured.timestamp_start, TIMESTAMP_START);
}

TEST_F(EvseSessionRecorderTest, start_reason_is_taken_from_the_event_payload) {
    std::vector<SessionStart> captured{};
    EXPECT_CALL(store, store_session_started(_)).Times(2).WillRepeatedly([&captured](const SessionStart& session) {
        captured.push_back(session);
        return true;
    });

    recorder.on_session_event(make_session_started_event("s1", StartSessionReason::EVConnected));
    recorder.on_session_event(make_session_started_event("s2", StartSessionReason::Authorized));

    ASSERT_EQ(captured.size(), 2);
    EXPECT_EQ(captured.at(0).start_reason, StartSessionReason::EVConnected);
    EXPECT_EQ(captured.at(1).start_reason, StartSessionReason::Authorized);
}

TEST_F(EvseSessionRecorderTest, connector_id_from_the_event_is_used) {
    SessionStart captured{};
    EXPECT_CALL(store, store_session_started(_)).WillOnce([&captured](const SessionStart& session) {
        captured = session;
        return true;
    });

    auto event = make_session_started_event(SESSION_ID);
    event.connector_id = 2;
    recorder.on_session_event(event);

    EXPECT_EQ(captured.connector_id, 2);
}

TEST_F(EvseSessionRecorderTest, connector_id_defaults_to_one) {
    SessionStart captured{};
    EXPECT_CALL(store, store_session_started(_)).WillOnce([&captured](const SessionStart& session) {
        captured = session;
        return true;
    });

    auto event = make_session_started_event(SESSION_ID);
    event.connector_id = std::nullopt;
    recorder.on_session_event(event);

    EXPECT_EQ(captured.connector_id, 1);
}

TEST_F(EvseSessionRecorderTest, evse_information_is_taken_from_set_evse) {
    SessionStart captured{};
    EXPECT_CALL(store, store_session_started(_)).WillOnce([&captured](const SessionStart& session) {
        captured = session;
        return true;
    });

    recorder.set_evse(7, "DE*PNX*E7");
    recorder.on_session_event(make_session_started_event(SESSION_ID));

    EXPECT_EQ(captured.evse_id, 7);
    EXPECT_EQ(captured.evse_id_string, "DE*PNX*E7");
}

TEST_F(EvseSessionRecorderStrictTest, session_started_without_payload_is_dropped) {
    auto event = make_session_event(SessionEventEnum::SessionStarted, SESSION_ID);
    event.session_started = std::nullopt;

    recorder.on_session_event(event);
}

// --- transaction record building ---------------------------------------------

TEST_F(EvseSessionRecorderTest, transaction_started_stores_the_transaction_of_its_session) {
    TransactionStart captured{};
    EXPECT_CALL(store, store_transaction_started(_)).WillOnce([&captured](const TransactionStart& transaction) {
        captured = transaction;
        return true;
    });

    recorder.on_session_event(make_session_started_event(SESSION_ID));
    recorder.on_session_event(make_transaction_started_event(SESSION_ID, "2026-08-21T10:30:00Z"));

    EXPECT_EQ(captured.session_id, SESSION_ID);
    EXPECT_EQ(captured.timestamp_start, "2026-08-21T10:30:00Z");
}

TEST_F(EvseSessionRecorderTest, transaction_started_is_forwarded_without_a_preceding_session_started) {
    TransactionStart captured{};
    EXPECT_CALL(store, store_transaction_started(_)).WillOnce([&captured](const TransactionStart& transaction) {
        captured = transaction;
        return true;
    });

    recorder.on_session_event(make_transaction_started_event(SESSION_ID));

    EXPECT_EQ(captured.session_id, SESSION_ID);
}

TEST_F(EvseSessionRecorderTest, id_token_fields_are_taken_from_the_event) {
    TransactionStart captured{};
    EXPECT_CALL(store, store_transaction_started(_)).WillOnce([&captured](const TransactionStart& transaction) {
        captured = transaction;
        return true;
    });

    const auto id_tag = make_id_token("DEADBEEF", IdTokenType::eMAID, AuthorizationType::PlugAndCharge);
    recorder.on_session_event(make_transaction_started_event(SESSION_ID, TIMESTAMP_START, id_tag));

    ASSERT_TRUE(captured.id_token_hash.has_value());
    EXPECT_EQ(captured.id_token_hash.value(), module::storage::generate_id_token_hash(id_tag.id_token));
    EXPECT_EQ(captured.id_token_hash->size(), 64);
    EXPECT_THAT(captured.id_token_hash.value(), testing::MatchesRegex("^[0-9a-f]{64}$"));
    ASSERT_TRUE(captured.id_token_type.has_value());
    EXPECT_EQ(captured.id_token_type.value(), IdTokenType::eMAID);
    ASSERT_TRUE(captured.authorization_type.has_value());
    EXPECT_EQ(captured.authorization_type.value(), AuthorizationType::PlugAndCharge);
}

TEST_F(EvseSessionRecorderStrictTest, transaction_started_without_payload_is_dropped) {
    auto event = make_session_event(SessionEventEnum::TransactionStarted, SESSION_ID);
    event.transaction_started = std::nullopt;

    recorder.on_session_event(event);
}

// --- energy forwarding -------------------------------------------------------

TEST_F(EvseSessionRecorderTest, start_energy_is_the_imported_total) {
    TransactionStart captured{};
    EXPECT_CALL(store, store_transaction_started(_)).WillOnce([&captured](const TransactionStart& transaction) {
        captured = transaction;
        return true;
    });

    const auto meter_value = make_powermeter(1234.5F);
    recorder.on_session_event(make_transaction_started_event(
        SESSION_ID, TIMESTAMP_START, make_id_token("TOKEN-1", IdTokenType::ISO14443, AuthorizationType::RFID),
        meter_value));

    EXPECT_FLOAT_EQ(captured.energy_Wh_import_start, meter_value.energy_Wh_import.total);
}

TEST_F(EvseSessionRecorderTest, stop_energy_is_the_imported_total) {
    TransactionFinish captured{};
    EXPECT_CALL(store, store_transaction_finished(_)).WillOnce([&captured](const TransactionFinish& finish) {
        captured = finish;
        return true;
    });

    const auto meter_value = make_powermeter(9876.5F);
    recorder.on_session_event(make_transaction_finished_event(SESSION_ID, TIMESTAMP_STOP, meter_value));

    EXPECT_FLOAT_EQ(captured.energy_Wh_import_stop, meter_value.energy_Wh_import.total);
}

// --- signed meter value gating ----------------------------------------------

TEST_F(EvseSessionRecorderTest, start_signed_meter_value_is_dropped_when_disabled) {
    TransactionStart captured{};
    EXPECT_CALL(store, store_transaction_started(_)).WillOnce([&captured](const TransactionStart& transaction) {
        captured = transaction;
        return true;
    });

    recorder.on_session_event(make_transaction_started_event(
        SESSION_ID, TIMESTAMP_START, make_id_token("TOKEN-1", IdTokenType::ISO14443, AuthorizationType::RFID),
        make_powermeter(1000.0F), make_signed_meter_value("start-data")));

    EXPECT_FALSE(captured.signed_meter_value_start.has_value());
}

TEST_F(EvseSessionRecorderTest, finish_signed_meter_values_are_dropped_when_disabled) {
    TransactionFinish captured{};
    EXPECT_CALL(store, store_transaction_finished(_)).WillOnce([&captured](const TransactionFinish& finish) {
        captured = finish;
        return true;
    });

    recorder.on_session_event(make_transaction_finished_event(
        SESSION_ID, TIMESTAMP_STOP, make_powermeter(2000.0F), StopTransactionReason::Local,
        make_signed_meter_value("stop-data"), make_signed_meter_value("start-data")));

    EXPECT_FALSE(captured.signed_meter_value_stop.has_value());
    EXPECT_FALSE(captured.signed_meter_value_start.has_value());
}

TEST_F(EvseSessionRecorderSignedTest, start_signed_meter_value_is_stored_when_enabled) {
    TransactionStart captured{};
    EXPECT_CALL(store, store_transaction_started(_)).WillOnce([&captured](const TransactionStart& transaction) {
        captured = transaction;
        return true;
    });

    const auto signed_meter_value = make_signed_meter_value("start-data");
    recorder.on_session_event(make_transaction_started_event(
        SESSION_ID, TIMESTAMP_START, make_id_token("TOKEN-1", IdTokenType::ISO14443, AuthorizationType::RFID),
        make_powermeter(1000.0F), signed_meter_value));

    ASSERT_TRUE(captured.signed_meter_value_start.has_value());
    EXPECT_EQ(captured.signed_meter_value_start.value(), signed_meter_value);
}

TEST_F(EvseSessionRecorderSignedTest, finish_signed_meter_values_are_stored_when_enabled) {
    TransactionFinish captured{};
    EXPECT_CALL(store, store_transaction_finished(_)).WillOnce([&captured](const TransactionFinish& finish) {
        captured = finish;
        return true;
    });

    const auto stop_value = make_signed_meter_value("stop-data");
    const auto start_value = make_signed_meter_value("start-data");
    recorder.on_session_event(make_transaction_finished_event(SESSION_ID, TIMESTAMP_STOP, make_powermeter(2000.0F),
                                                              StopTransactionReason::Local, stop_value, start_value));

    ASSERT_TRUE(captured.signed_meter_value_stop.has_value());
    EXPECT_EQ(captured.signed_meter_value_stop.value(), stop_value);
    ASSERT_TRUE(captured.signed_meter_value_start.has_value());
    EXPECT_EQ(captured.signed_meter_value_start.value(), start_value);
}

// --- transaction finish ------------------------------------------------------

TEST_F(EvseSessionRecorderTest, transaction_finished_with_uuid_is_forwarded) {
    TransactionFinish captured{};
    EXPECT_CALL(store, store_transaction_finished(_)).WillOnce([&captured](const TransactionFinish& finish) {
        captured = finish;
        return true;
    });

    recorder.on_session_event(make_transaction_finished_event(SESSION_ID));

    EXPECT_EQ(captured.session_id, SESSION_ID);
    EXPECT_EQ(captured.timestamp_stop, TIMESTAMP_STOP);
    ASSERT_TRUE(captured.stop_reason.has_value());
    EXPECT_EQ(captured.stop_reason.value(), StopTransactionReason::EVDisconnected);
    EXPECT_FALSE(captured.closes_session);
}

TEST_F(EvseSessionRecorderTest, stop_reason_absent_when_the_event_has_none) {
    TransactionFinish captured{};
    EXPECT_CALL(store, store_transaction_finished(_)).WillOnce([&captured](const TransactionFinish& finish) {
        captured = finish;
        return true;
    });

    recorder.on_session_event(
        make_transaction_finished_event(SESSION_ID, TIMESTAMP_STOP, make_powermeter(2000.0F), std::nullopt));

    EXPECT_FALSE(captured.stop_reason.has_value());
}

TEST_F(EvseSessionRecorderTest, power_loss_stop_reason_is_forwarded) {
    TransactionFinish captured{};
    EXPECT_CALL(store, store_transaction_finished(_)).WillOnce([&captured](const TransactionFinish& finish) {
        captured = finish;
        return true;
    });

    recorder.on_session_event(make_transaction_finished_event(SESSION_ID, TIMESTAMP_STOP, make_powermeter(2000.0F),
                                                              StopTransactionReason::PowerLoss));

    ASSERT_TRUE(captured.stop_reason.has_value());
    EXPECT_EQ(captured.stop_reason.value(), StopTransactionReason::PowerLoss);
}

TEST_F(EvseSessionRecorderStrictTest, transaction_finished_without_payload_is_dropped) {
    auto event = make_session_event(SessionEventEnum::TransactionFinished, SESSION_ID);
    event.transaction_finished = std::nullopt;

    recorder.on_session_event(event);
}

// --- power loss recovery -----------------------------------------------------

TEST_F(EvseSessionRecorderTest, empty_uuid_finish_resolves_to_the_resumed_session_and_closes_it) {
    TransactionFinish captured{};
    EXPECT_CALL(store, store_transaction_finished(_)).WillOnce([&captured](const TransactionFinish& finish) {
        captured = finish;
        return true;
    });

    recorder.on_session_event(make_session_event(SessionEventEnum::SessionResumed, SESSION_ID));
    recorder.on_session_event(make_transaction_finished_event(""));

    EXPECT_EQ(captured.session_id, SESSION_ID);
    EXPECT_TRUE(captured.closes_session);
    EXPECT_EQ(captured.timestamp_stop, TIMESTAMP_STOP);
}

TEST_F(EvseSessionRecorderStrictTest, empty_uuid_finish_without_resumed_session_is_dropped) {
    recorder.on_session_event(make_transaction_finished_event(""));
}

TEST_F(EvseSessionRecorderStrictTest, resumed_session_id_is_consumed_by_the_first_empty_uuid_finish) {
    EXPECT_CALL(store, store_transaction_finished(_)).Times(1).WillRepeatedly(Return(true));

    recorder.on_session_event(make_session_event(SessionEventEnum::SessionResumed, SESSION_ID));
    recorder.on_session_event(make_transaction_finished_event(""));
    recorder.on_session_event(make_transaction_finished_event(""));
}

TEST_F(EvseSessionRecorderStrictTest, resumed_session_of_another_evse_does_not_satisfy_an_empty_uuid_finish) {
    EvseSessionRecorder other_recorder{store, RecorderConfig{}};
    other_recorder.set_evse(2, "DE*PNX*E2");

    recorder.on_session_event(make_session_event(SessionEventEnum::SessionResumed, SESSION_ID));
    other_recorder.on_session_event(make_transaction_finished_event(""));
}

TEST_F(EvseSessionRecorderStrictTest, session_resumed_alone_does_not_touch_the_store) {
    recorder.on_session_event(make_session_event(SessionEventEnum::SessionResumed, SESSION_ID));
}

TEST_F(EvseSessionRecorderTest, finish_with_uuid_does_not_consume_the_resumed_session) {
    std::vector<TransactionFinish> captured{};
    EXPECT_CALL(store, store_transaction_finished(_))
        .Times(2)
        .WillRepeatedly([&captured](const TransactionFinish& finish) {
            captured.push_back(finish);
            return true;
        });

    recorder.on_session_event(make_session_event(SessionEventEnum::SessionResumed, SESSION_ID));
    recorder.on_session_event(make_transaction_finished_event("other-session"));
    recorder.on_session_event(make_transaction_finished_event(""));

    ASSERT_EQ(captured.size(), 2);
    EXPECT_EQ(captured.at(0).session_id, "other-session");
    EXPECT_FALSE(captured.at(0).closes_session);
    EXPECT_EQ(captured.at(1).session_id, SESSION_ID);
    EXPECT_TRUE(captured.at(1).closes_session);
}

// --- session finish ----------------------------------------------------------

TEST_F(EvseSessionRecorderTest, session_finished_is_forwarded) {
    EXPECT_CALL(store, store_session_finished(SESSION_ID, "2026-08-21T13:00:00Z")).WillOnce(Return(true));

    recorder.on_session_event(make_session_started_event(SESSION_ID));
    recorder.on_session_event(
        make_session_event(SessionEventEnum::SessionFinished, SESSION_ID, "2026-08-21T13:00:00Z"));
}

TEST_F(EvseSessionRecorderTest, session_finished_is_forwarded_without_a_preceding_session_started) {
    EXPECT_CALL(store, store_session_finished(SESSION_ID, TIMESTAMP_STOP)).WillOnce(Return(true));

    recorder.on_session_event(make_session_event(SessionEventEnum::SessionFinished, SESSION_ID, TIMESTAMP_STOP));
}

TEST_F(EvseSessionRecorderTest, session_finished_of_another_session_is_forwarded_as_is) {
    EXPECT_CALL(store, store_session_finished("other-session", TIMESTAMP_STOP)).WillOnce(Return(true));

    recorder.on_session_event(make_session_started_event(SESSION_ID));
    recorder.on_session_event(make_session_event(SessionEventEnum::SessionFinished, "other-session", TIMESTAMP_STOP));
}

// --- per EVSE isolation ------------------------------------------------------

TEST(EvseSessionRecorderMultipleEvseTest, each_recorder_records_its_own_evse) {
    NiceMock<MockSessionStore> store{};
    std::vector<SessionStart> captured{};
    EXPECT_CALL(store, store_session_started(_)).Times(2).WillRepeatedly([&captured](const SessionStart& session) {
        captured.push_back(session);
        return true;
    });

    EvseSessionRecorder first{store, RecorderConfig{}};
    first.set_evse(1, "DE*PNX*E1");
    EvseSessionRecorder second{store, RecorderConfig{}};
    second.set_evse(2, "DE*PNX*E2");

    first.on_session_event(make_session_started_event("s1"));
    second.on_session_event(make_session_started_event("s2"));

    ASSERT_EQ(captured.size(), 2);
    EXPECT_EQ(captured.at(0).session_id, "s1");
    EXPECT_EQ(captured.at(0).evse_id, 1);
    EXPECT_EQ(captured.at(0).evse_id_string, "DE*PNX*E1");
    EXPECT_EQ(captured.at(1).session_id, "s2");
    EXPECT_EQ(captured.at(1).evse_id, 2);
    EXPECT_EQ(captured.at(1).evse_id_string, "DE*PNX*E2");
}

// --- defensive behaviour and noise rejection ---------------------------------

TEST(EvseSessionRecorderUnsetEvseTest, session_started_before_set_evse_is_dropped) {
    StrictMock<MockSessionStore> store{};
    EvseSessionRecorder recorder{store, RecorderConfig{}};

    recorder.on_session_event(make_session_started_event(SESSION_ID));
}

TEST(EvseSessionRecorderUnsetEvseTest, session_finished_before_set_evse_is_dropped) {
    StrictMock<MockSessionStore> store{};
    EvseSessionRecorder recorder{store, RecorderConfig{}};

    recorder.on_session_event(make_session_event(SessionEventEnum::SessionFinished, SESSION_ID));
}

TEST_F(EvseSessionRecorderStrictTest, unrelated_session_events_do_not_touch_the_store) {
    const std::vector<SessionEventEnum> events{
        SessionEventEnum::Authorized,       SessionEventEnum::Deauthorized,     SessionEventEnum::Enabled,
        SessionEventEnum::Disabled,         SessionEventEnum::AuthRequired,     SessionEventEnum::PrepareCharging,
        SessionEventEnum::ChargingStarted,  SessionEventEnum::ChargingPausedEV, SessionEventEnum::ChargingPausedEVSE,
        SessionEventEnum::StoppingCharging, SessionEventEnum::ChargingFinished, SessionEventEnum::ReservationStart,
        SessionEventEnum::ReservationEnd,   SessionEventEnum::PluginTimeout,    SessionEventEnum::SwitchingPhases,
    };

    for (const auto event : events) {
        recorder.on_session_event(make_session_event(event, SESSION_ID));
    }
}

// --- OCPP transaction events -------------------------------------------------

TEST_F(SessionForwardingTest, ocpp_started_event_is_forwarded) {
    OcppTransactionEvent captured{};
    EXPECT_CALL(store, store_ocpp_transaction_event(_)).WillOnce([&captured](const OcppTransactionEvent& event) {
        captured = event;
        return true;
    });

    module::storage::forward_ocpp_transaction_event(
        store, make_ocpp_transaction_event(SESSION_ID, "4711", types::ocpp::TransactionEvent::Started));

    EXPECT_EQ(captured.session_id, SESSION_ID);
    EXPECT_EQ(captured.transaction_event, types::ocpp::TransactionEvent::Started);
    EXPECT_EQ(captured.transaction_id, "4711");
    EXPECT_EQ(captured.timestamp, TIMESTAMP_START);
}

TEST_F(SessionForwardingTest, ocpp_ended_event_is_forwarded) {
    OcppTransactionEvent captured{};
    EXPECT_CALL(store, store_ocpp_transaction_event(_)).WillOnce([&captured](const OcppTransactionEvent& event) {
        captured = event;
        return true;
    });

    module::storage::forward_ocpp_transaction_event(
        store, make_ocpp_transaction_event(SESSION_ID, "4711", types::ocpp::TransactionEvent::Ended, TIMESTAMP_STOP));

    EXPECT_EQ(captured.transaction_event, types::ocpp::TransactionEvent::Ended);
    EXPECT_EQ(captured.transaction_id, "4711");
    EXPECT_EQ(captured.timestamp, TIMESTAMP_STOP);
}

TEST_F(SessionForwardingTest, ocpp_updated_event_is_forwarded) {
    OcppTransactionEvent captured{};
    EXPECT_CALL(store, store_ocpp_transaction_event(_)).WillOnce([&captured](const OcppTransactionEvent& event) {
        captured = event;
        return true;
    });

    module::storage::forward_ocpp_transaction_event(
        store, make_ocpp_transaction_event(SESSION_ID, "4711", types::ocpp::TransactionEvent::Updated));

    EXPECT_EQ(captured.transaction_event, types::ocpp::TransactionEvent::Updated);
    EXPECT_EQ(captured.transaction_id, "4711");
}

TEST_F(SessionForwardingTest, ocpp_event_without_transaction_id_is_forwarded) {
    OcppTransactionEvent captured{};
    EXPECT_CALL(store, store_ocpp_transaction_event(_)).WillOnce([&captured](const OcppTransactionEvent& event) {
        captured = event;
        return true;
    });

    module::storage::forward_ocpp_transaction_event(store, make_ocpp_transaction_event(SESSION_ID, std::nullopt));

    EXPECT_EQ(captured.session_id, SESSION_ID);
    EXPECT_FALSE(captured.transaction_id.has_value());
    EXPECT_EQ(captured.timestamp, TIMESTAMP_START);
}

TEST_F(SessionForwardingTest, repeated_ocpp_transaction_event_is_forwarded_again) {
    EXPECT_CALL(store, store_ocpp_transaction_event(_)).Times(2).WillRepeatedly(Return(true));

    module::storage::forward_ocpp_transaction_event(store, make_ocpp_transaction_event(SESSION_ID, "4711"));
    module::storage::forward_ocpp_transaction_event(store, make_ocpp_transaction_event(SESSION_ID, "4711"));
}

TEST_F(SessionForwardingTest, ocpp_event_evse_member_is_ignored) {
    EXPECT_CALL(store, store_ocpp_transaction_event(_)).WillOnce(Return(true));

    auto event = make_ocpp_transaction_event(SESSION_ID, "4711");
    types::ocpp::EVSE evse{};
    evse.id = 42;
    evse.connector_id = 9;
    event.evse = evse;
    module::storage::forward_ocpp_transaction_event(store, event);
}

// --- session cost ------------------------------------------------------------

TEST_F(SessionForwardingTest, session_cost_is_forwarded_without_the_id_tag) {
    types::session_cost::SessionCost captured{};
    EXPECT_CALL(store, update_cost(SESSION_ID, _))
        .WillOnce([&captured](const std::string&, const types::session_cost::SessionCost& cost) {
            captured = cost;
            return true;
        });

    auto cost = make_session_cost(SESSION_ID, 4321);
    cost.id_tag = make_id_token("TOKEN-1", IdTokenType::ISO14443, AuthorizationType::RFID);
    module::storage::forward_session_cost(store, cost);

    EXPECT_FALSE(captured.id_tag.has_value());
    EXPECT_EQ(captured.session_id, SESSION_ID);
    EXPECT_EQ(captured.status, types::session_cost::SessionStatus::Finished);
    EXPECT_EQ(captured.currency, cost.currency);
    EXPECT_EQ(captured.cost_chunks, cost.cost_chunks);
    EXPECT_EQ(captured.qr_code, cost.qr_code);
}

TEST_F(SessionForwardingTest, repeated_session_cost_is_forwarded_again) {
    EXPECT_CALL(store, update_cost(SESSION_ID, _)).Times(2).WillRepeatedly(Return(true));

    module::storage::forward_session_cost(store, make_session_cost(SESSION_ID, 1000));
    module::storage::forward_session_cost(store, make_session_cost(SESSION_ID, 2000));
}

} // namespace
