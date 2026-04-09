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

struct StoredSlotInfo {
    int id;
    std::string last_updated;
    bool is_valid;
    std::optional<std::string> config_file_path;
};

class SqliteConfigSlotManager {
public:
    static constexpr int DEFAULT_SLOT_ID = 0;

    SqliteConfigSlotManager(const std::filesystem::path& db_path, const std::filesystem::path& migrations_path);

    bool is_valid(int slot_id = DEFAULT_SLOT_ID);
    /// \brief Returns the next available slot ID (MAX(ID) + 1, or 0 if no slots exist).
    int next_slot_id();
    GenericResponseStatus write_config_slot(int slot_id);
    std::vector<StoredSlotInfo> list_slots();
    GenericResponseStatus delete_slot(int slot_id);

private:
    std::unique_ptr<everest::db::sqlite::ConnectionInterface> db;
};

} // namespace everest::config
