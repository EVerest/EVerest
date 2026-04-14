// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <utils/config/settings.hpp>
#include <utils/config/slot_manager.hpp>
#include <utils/config/storage_sqlite.hpp>
#include <utils/config_service_interface.hpp>

namespace Everest {
class ManagerConfig;
} // namespace Everest

namespace Everest::config {

/// \brief Core framework implementation of ConfigServiceInterface.
///
/// Owns a SqliteConfigSlotManager and uses a SqliteStorage factory for
/// per-slot storage access. Delegates active-slot configuration operations
/// to the shared ManagerConfig instance.
class ConfigServiceCore : public ConfigServiceInterface {
public:
    /// \param active_config  Shared pointer to the active slot's ManagerConfig.
    /// \param parse_settings Parse settings used to validate incoming YAML configs (paths to schemas, modules, etc.).
    /// \param db_path        Full path to the SQLite database file.
    /// \param migrations_dir Directory containing SQL migration files.
    /// \param active_slot_id Slot ID that is currently booted.
    /// \param stop_fn        Callback to stop running modules (optional stub).
    /// \param restart_fn     Callback to restart modules (optional stub).
    ConfigServiceCore(std::shared_ptr<ManagerConfig> active_config, const ConfigParseSettings& parse_settings,
                      std::filesystem::path db_path, std::filesystem::path migrations_dir, int active_slot_id,
                      std::function<StopModulesResult()> stop_fn = {},
                      std::function<RestartModulesResult()> restart_fn = {});

    // --- Slot management ---
    std::vector<SlotInfo> list_all_slots() override;
    int get_active_slot_id() override;
    SetActiveSlotStatus mark_active_slot(int slot_id) override;
    DeleteSlotStatus delete_slot(int slot_id) override;
    DuplicateSlotResult duplicate_slot(int slot_id, std::optional<std::string> description) override;
    LoadFromYamlResult load_from_yaml(const std::string& raw_yaml) override;

    // --- Slot-scoped configuration ---
    GetConfigurationResult get_configuration(int slot_id) override;
    std::vector<SetConfigParameterResult>
    set_config_parameters(int slot_id, const std::vector<ConfigParameterUpdate>& updates) override;

    // --- Module lifecycle ---
    StopModulesResult stop_modules() override;
    RestartModulesResult restart_modules() override;

    // --- Push-event subscriptions ---
    void register_active_slot_update_handler(std::function<void(const ActiveSlotUpdate&)> handler) override;
    void register_config_update_handler(std::function<void(const ConfigurationUpdate&)> handler) override;

private:
    std::shared_ptr<ManagerConfig> active_config_;
    ConfigParseSettings parse_settings_;
    everest::config::SqliteConfigSlotManager slot_manager_;
    std::filesystem::path db_path_;
    std::filesystem::path migrations_dir_;
    int active_slot_id_;
    std::function<StopModulesResult()> stop_fn_;
    std::function<RestartModulesResult()> restart_fn_;

    std::vector<std::function<void(const ActiveSlotUpdate&)>> active_slot_handlers_;
    std::vector<std::function<void(const ConfigurationUpdate&)>> config_update_handlers_;

    std::unique_ptr<everest::config::SqliteStorage> make_storage(int slot_id);
    void publish_active_slot_update(const ActiveSlotUpdate& update);
    void publish_config_update(const ConfigurationUpdate& update);

    /// \brief Storage handle for the currently active slot, used to persist runtime config writes.
    std::unique_ptr<everest::config::SqliteStorage> active_storage_;
};

} // namespace Everest::config
