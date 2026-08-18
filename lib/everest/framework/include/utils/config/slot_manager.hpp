// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utils/config/storage_types.hpp>
#include <vector>

namespace everest::db::sqlite {
class ConnectionInterface;
} // namespace everest::db::sqlite

namespace everest::config {

/// \brief Result of duplicate_slot(); slot_id holds the id of the newly created slot on success
struct DuplicateSlotResult {
    bool success = false;
    std::optional<int> slot_id;
};

/// \brief Metadata of a single configuration slot as stored in the CONFIG/CONFIG_META tables
struct SlotInfo {
    int id;
    /// \brief RFC 3339 timestamp of the last write to this slot; empty if no metadata was written yet
    std::string last_updated;
    std::optional<std::string> config_file_path;
    std::optional<std::string> description;
};

using StoredSlotInfo = SlotInfo;

/// \brief Manages configuration slots of the config database with SQLite.
///
/// A slot is one complete, independently addressable configuration: a row in the CONFIG table
/// plus its metadata in CONFIG_META (config dump, originating config file path, description) and
/// all per-slot data keyed by CONFIG_ID (modules, fulfillments, tier mappings, configuration
/// parameters and access rights). This class handles the slots as a whole - creating, listing,
/// duplicating and deleting them, and selecting which one is used on the next boot. The contents
/// of an individual slot are accessed through a SqliteStorage instance scoped to that slot id.
///
/// Slots are identified by an integer id; DEFAULT_SLOT_ID is the slot a migrated or freshly created
/// database carries its initial configuration in. Deleting a slot removes its dependent rows via
/// the foreign key constraints of the schema.
///
/// This class is not thread-safe; concurrent access must be synchronized by the caller.
class SqliteConfigSlotManager {
public:
    /// \brief The slot ID used when no explicit ID is given, and the fallback for the next boot slot
    static constexpr int DEFAULT_SLOT_ID = 0;

    /// \brief Opens its own Connection and applies migrations.
    SqliteConfigSlotManager(const std::filesystem::path& db_path, const std::filesystem::path& migrations_path);

    /// \brief Shares an already-migrated Connection.
    /// Calls open_connection() on the shared connection; the destructor calls close_connection().
    /// \param connection Shared database connection (already migrated)
    explicit SqliteConfigSlotManager(std::shared_ptr<everest::db::sqlite::ConnectionInterface> connection);

    ~SqliteConfigSlotManager();

    /// \brief Returns true if a slot with \p slot_id has metadata stored in the CONFIG_META table
    bool exists(int slot_id);
    /// \brief Returns the next available slot ID (MAX(ID) + 1, or 0 if no slots exist).
    int next_slot_id();
    /// \brief Writes a new configuration slot
    /// \param slot_id Id of the new slot; must not exist yet
    /// \param config_dump JSON dump of the config file that was used to create the configuration
    /// \param config_file_path Path to the config file that was used to create the configuration
    /// \param description Arbitrary text
    GenericResponseStatus write_config_slot(int slot_id, const std::string& config_dump,
                                            const std::optional<std::filesystem::path>& config_file_path,
                                            const std::optional<std::string>& description);
    /// \brief Updates an existing configuration slot
    /// Optionals without a value will set the corresponding value to NULL in the DB
    /// \param slot_id Id of the slot; must not exist yet
    /// \param config_dump JSON dump of the config file that was used to create the configuration
    /// \param config_file_path Path to the config file that was used to create the configuration
    /// \param description Arbitrary text
    GenericResponseStatus update_config_slot(int slot_id, const std::string& config_dump,
                                             const std::optional<std::filesystem::path>& config_file_path,
                                             const std::optional<std::string>& description);
    /// \brief Updates an existing configuration slot's description
    /// Optionals without a value will set the corresponding value to NULL in the DB
    /// \param slot_id Id of the slot; must not exist yet
    /// \param description Arbitrary text
    GenericResponseStatus update_description(int slot_id, const std::optional<std::string>& description);

    /// \brief Returns the metadata of all existing slots, ordered by slot ID ascending
    std::vector<SlotInfo> list_slots();

    /// \brief Deletes \p slot_id and all data belonging to it
    /// \returns OK also when the slot does not exist, since deleting it is a no-op then
    /// \returns Failed if \p slot_id is the explicitly selected next boot slot; the foreign key of
    ///          the BOOT_CONFIG table rejects the delete so the selection cannot end up dangling.
    ///          Mark a different slot via set_next_boot_slot_id() first.
    GenericResponseStatus delete_slot(int slot_id);

    /// \brief Duplicates all data belonging to \p source_slot_id into a new slot.
    /// \returns DuplicateSlotResult with the new slot_id on success.
    DuplicateSlotResult duplicate_slot(int source_slot_id, std::optional<std::string> description = std::nullopt);

    /// \brief Returns the slot ID that will be used on the next boot (from the BOOT_CONFIG table).
    /// Returns DEFAULT_SLOT_ID while no slot has been selected explicitly yet.
    int get_next_boot_slot_id();

    /// \brief Persists \p slot_id as the next boot slot in the BOOT_CONFIG table.
    /// Until this is called the table holds no row at all and DEFAULT_SLOT_ID is implied.
    /// \returns Failed if \p slot_id does not exist in the CONFIG table.
    GenericResponseStatus set_next_boot_slot_id(int slot_id);

private:
    std::shared_ptr<everest::db::sqlite::ConnectionInterface> db;

    // a conservative maximum slot_id value, compatible with sqlite but more than big enough for practical usage
    const int max_slot_id{(2 << 16) - 1};
};

} // namespace everest::config
