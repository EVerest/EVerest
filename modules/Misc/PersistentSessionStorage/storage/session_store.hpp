// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file Persistent SQLite backed store for session records

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <everest/database/sqlite/connection.hpp>
#include <everest/util/async/monitor.hpp>

#include <generated/types/authorization.hpp>
#include <generated/types/evse_manager.hpp>
#include <generated/types/ocpp.hpp>
#include <generated/types/session_cost.hpp>
#include <generated/types/session_storage.hpp>
#include <generated/types/units_signed.hpp>

namespace module::storage {

/// \brief Everything that is known when a session starts
struct SessionStart {
    std::string session_id{};
    std::int32_t evse_id{0};
    std::string evse_id_string{};
    std::int32_t connector_id{1};
    std::string timestamp_start{};
    types::evse_manager::StartSessionReason start_reason{};
};

/// \brief Everything that becomes known when the transaction of a session starts
struct TransactionStart {
    std::string session_id{};
    std::string timestamp_start{};
    float energy_Wh_import_start{0.0F};
    /// \brief Absent only if the digest of the presented id token could not be calculated
    std::optional<std::string> id_token_hash{};
    std::optional<types::authorization::IdTokenType> id_token_type{};
    std::optional<types::authorization::AuthorizationType> authorization_type{};
    std::optional<types::units_signed::SignedMeterValue> signed_meter_value_start{};
};

/// \brief Everything that becomes known when the transaction of a session finishes
struct TransactionFinish {
    std::string session_id{};
    std::string timestamp_stop{};
    float energy_Wh_import_stop{0.0F};
    std::optional<types::evse_manager::StopTransactionReason> stop_reason{};
    std::optional<types::units_signed::SignedMeterValue> signed_meter_value_stop{};
    /// \brief Late arriving signed start meter value, backfills the transaction's
    ///        signed_meter_value_start without overwriting an existing value
    std::optional<types::units_signed::SignedMeterValue> signed_meter_value_start{};
    /// \brief Also finish the session, used when the EvseManager will not report a
    ///        SessionFinished of its own, i.e. for a session recovered after a power loss
    bool closes_session{false};
};

/// \brief Everything an OCPP transaction event contributes to the record of its session
struct OcppTransactionEvent {
    std::string session_id{};
    types::ocpp::TransactionEvent transaction_event{};
    /// \brief Absent until the CSMS assigned one, which for OCPP 1.6 is after the start
    std::optional<std::string> transaction_id{};
    std::string timestamp{};
};

/// \brief Read/write access to the stored session records
class SessionStoreInterface {
public:
    virtual ~SessionStoreInterface() = default;

    /// \brief Inserts the record of \p session, marks older Open records of the same EVSE
    ///        Stale and prunes the oldest records beyond the configured maximum. Atomic.
    /// \param[in] session - the session that started
    /// \returns false if the record was rejected (duplicate session id) or the write failed
    virtual bool store_session_started(const SessionStart& session) = 0;

    /// \brief Adds the transaction of \p transaction to the Open record of its session
    /// \param[in] transaction - the transaction that started
    /// \returns false if no Open record without a transaction matched (unknown session,
    ///          session not Open, or a transaction was already started)
    virtual bool store_transaction_started(const TransactionStart& transaction) = 0;

    /// \brief Completes the running transaction of the record identified by \p finish,
    ///        and the session itself if \p finish closes it
    /// \param[in] finish - the data that became known when the transaction finished
    /// \returns false if no Open record with a running transaction matched
    virtual bool store_transaction_finished(const TransactionFinish& finish) = 0;

    /// \brief Finishes the Open record of \p session_id
    /// \param[in] session_id - the session that finished
    /// \param[in] timestamp_stop - the time the session finished
    /// \returns false if no Open record matched (unknown, cleared or already finished)
    virtual bool store_session_finished(const std::string& session_id, const std::string& timestamp_stop) = 0;

    /// \brief Attaches the CSMS assigned transaction id and, for a Started or Ended
    ///        event, the OCPP transaction timestamp to the record of \p event's session.
    ///        An event without a transaction id leaves an already stored id in place.
    /// \param[in] event - the reported OCPP transaction event
    /// \returns false if no record matched
    virtual bool store_ocpp_transaction_event(const OcppTransactionEvent& event) = 0;

    /// \brief Replaces the cost information of the record of \p session_id, latest wins
    /// \param[in] session_id - the session the record belongs to
    /// \param[in] cost - the reported session cost, id_tag already stripped
    /// \returns false if no record matched
    virtual bool update_cost(const std::string& session_id, const types::session_cost::SessionCost& cost) = 0;

    /// \brief One page of stored records matching the request, in the order they were
    ///        stored, oldest first. The reply carries a continuation token while more
    ///        records may follow. An invalid or outdated token in the request starts
    ///        the iteration from the beginning.
    virtual types::session_storage::SessionList
    get_sessions(const types::session_storage::GetSessionsRequest& request) = 0;

    /// \brief The record matching \p identifier
    /// \returns the record, or nullopt if nothing matches
    virtual std::optional<types::session_storage::Session>
    get_session(const types::session_storage::SessionIdentifier& identifier) = 0;

    /// \brief Deletes every stored record
    /// \returns the number of records deleted
    virtual int clear_sessions() = 0;
};

/// \brief SQLite implementation of SessionStoreInterface.
///        All database exceptions are contained within this class: they are logged and
///        reported via the return values, no method throws.
class SessionStore : public SessionStoreInterface {
public:
    SessionStore() = delete;
    /// \param[in] connection - the database connection to use, not yet opened
    /// \param[in] migration_files_path - directory containing the SQL migration files
    /// \param[in] target_schema_version - schema version the migrations are applied up to
    /// \param[in] max_sessions - maximum number of records to keep, oldest deleted first
    SessionStore(std::unique_ptr<everest::db::sqlite::ConnectionInterface> connection,
                 std::filesystem::path migration_files_path, std::uint32_t target_schema_version, int max_sessions);

    /// \brief Applies the migration files, opens the connection and reads the database
    ///        epoch that scopes the continuation tokens
    /// \returns false if the migration or opening the connection failed
    bool open();

    bool store_session_started(const SessionStart& session) override;
    bool store_transaction_started(const TransactionStart& transaction) override;
    bool store_transaction_finished(const TransactionFinish& finish) override;
    bool store_session_finished(const std::string& session_id, const std::string& timestamp_stop) override;
    bool store_ocpp_transaction_event(const OcppTransactionEvent& event) override;
    bool update_cost(const std::string& session_id, const types::session_cost::SessionCost& cost) override;
    types::session_storage::SessionList
    get_sessions(const types::session_storage::GetSessionsRequest& request) override;
    std::optional<types::session_storage::Session>
    get_session(const types::session_storage::SessionIdentifier& identifier) override;
    int clear_sessions() override;

private:
    /// \brief The database connection together with the continuation token epoch it holds
    struct Database {
        std::unique_ptr<everest::db::sqlite::ConnectionInterface> connection{};
        std::string epoch{};
    };

    const std::filesystem::path m_migration_files_path{};
    const std::uint32_t m_target_schema_version{0};
    const int m_max_sessions{0};
    everest::lib::util::monitor<Database> m_database{};
};

} // namespace module::storage
