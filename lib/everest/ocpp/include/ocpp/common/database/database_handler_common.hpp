// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <everest/database/exceptions.hpp>
#include <everest/database/sqlite/connection.hpp>
#include <ocpp/common/types.hpp>

namespace ocpp::common {

struct DBTransactionMessage {
    json json_message;
    std::string message_type;
    std::int32_t message_attempts;
    DateTime timestamp;
    std::string unique_id;
};

class DatabaseHandlerCommon {
protected:
    std::unique_ptr<everest::db::sqlite::ConnectionInterface> database;
    const fs::path sql_migration_files_path;
    const std::uint32_t target_schema_version;

    /// \brief Perform the initialization needed to use the database. Will be called by open_connection()
    virtual void init_sql() = 0;

public:
    /// \brief Common database handler class
    /// Class handles some common database functionality like inserting and removing transaction messages.
    ///
    /// \param database Interface for the database connection
    /// \param sql_migration_files_path Filesystem path to migration file folder
    /// \param target_schema_version The required schema version of the database
    explicit DatabaseHandlerCommon(std::unique_ptr<everest::db::sqlite::ConnectionInterface> database,
                                   const fs::path& sql_migration_files_path,
                                   std::uint32_t target_schema_version) noexcept;

    virtual ~DatabaseHandlerCommon() = default;

    /// \brief Opens connection to database file and performs the initialization by calling init_sql()
    void open_connection();

    /// \brief Closes the database connection.
    void close_connection();

    /// \brief Get messages from messages queue table specified by \p queue_type
    /// \param queue_type , defaults to QueueType::Transaction
    /// \param connection Only return messages belonging to this OCPP connection. std::nullopt is the
    ///        primary connection, which is also what rows written before connections were tagged are.
    ///        A charging station with several connections must not replay another connection's
    ///        messages, so each queue asks only for its own.
    /// \return The transaction messages.
    virtual std::vector<DBTransactionMessage>
    get_message_queue_messages(const QueueType queue_type = QueueType::Transaction,
                               const std::optional<std::string>& connection = std::nullopt);

    /// \brief Insert a new message into messages queue table specified by \p queue_type
    /// \param message  The message to be stored.
    /// \param queue_type , defaults to QueueType::Transaction
    /// \param connection The OCPP connection the message belongs to; std::nullopt is the primary.
    virtual void insert_message_queue_message(const DBTransactionMessage& message,
                                              const QueueType queue_type = QueueType::Transaction,
                                              const std::optional<std::string>& connection = std::nullopt);

    /// \brief Remove a message from the messages queue table specified by \p queue_type
    /// \param unique_id    The unique id of the transaction message
    /// \param queue_type , defaults to QueueType::Transaction
    /// \param connection Only remove the message if it belongs to this OCPP connection, so that one
    ///        connection can never delete another's message.
    /// \return True on success.
    virtual void remove_message_queue_message(const std::string& unique_id,
                                              const QueueType queue_type = QueueType::Transaction,
                                              const std::optional<std::string>& connection = std::nullopt);

    /// \brief Deletes all entries from message queue table specified by \p queue_type
    /// \param queue_type , defaults to QueueType::Transaction
    /// \param connection Only clear the entries of this OCPP connection; std::nullopt is the primary.
    virtual void clear_message_queue(const QueueType queue_type = QueueType::Transaction,
                                     const std::optional<std::string>& connection = std::nullopt);
};

} // namespace ocpp::common
