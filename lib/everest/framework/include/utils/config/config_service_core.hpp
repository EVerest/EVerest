// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <utils/config/settings.hpp>
#include <utils/config/slot_manager.hpp>
#include <utils/config/storage_sqlite.hpp>
#include <utils/config/types.hpp>
#include <utils/config_service_interface.hpp>

namespace Everest::config {

/// \brief Core framework implementation of ConfigServiceInterface.
///
/// Owns a SqliteConfigSlotManager and uses a SqliteStorage factory for per-slot storage access.
/// Owns the active-slot ModuleConfigurations as the single in-memory runtime authority.
class ConfigServiceCore : public ConfigServiceInterface {
public:
    /// \param parse_settings Parse settings used to validate incoming YAML configs (paths to schemas, modules, etc.).
    /// \param db_connection  Shared, already-migrated SQLite connection (from open_config_database()).
    /// \param stop_fn        Callback to stop running modules (optional stub).
    /// \param restart_fn     Callback to restart modules (optional stub).
    ConfigServiceCore(const ConfigParseSettings& parse_settings,
                      std::shared_ptr<everest::db::sqlite::ConnectionInterface> db_connection);

    // --- Re-initialize configuration ---
    // \brief Reloads the active_slot_id from the db and reloads the modules accordingly
    /// \param force_reload Forces a reload even when the active slot did not change
    void reinitialize_from_db(bool force_reload = false);

    // --- Active-slot in-memory access (zero-copy) ---
    const everest::config::ModuleConfigurations& get_active_module_configurations() const override;

    // --- Slot management ---
    std::vector<SlotInfo> list_all_slots() override;
    int get_active_slot_id() override;
    int get_next_boot_slot_id() override;
    SetActiveSlotStatus mark_active_slot(int slot_id) override;
    DeleteSlotStatus delete_slot(int slot_id) override;
    DuplicateSlotResult duplicate_slot(int slot_id, std::optional<std::string> description) override;
    LoadFromYamlResult load_from_yaml(const std::string& raw_yaml, std::optional<std::string> description,
                                      std::optional<int> slot_id) override;
    bool set_description(int slot_id, const std::string& description) override;

    // --- Slot-scoped configuration ---
    GetConfigurationResult get_configuration(int slot_id) override;
    SetConfigParameterResult set_config_parameters(int slot_id, const std::vector<ConfigParameterUpdate>& updates,
                                                   const Origin& origin) override;
    GetConfigParametersResult
    get_config_parameters(int slot_id,
                          const std::vector<everest::config::ConfigurationParameterIdentifier>& parameters) override;

    // --- Push-event subscriptions ---
    void register_active_slot_update_handler(std::function<void(const ActiveSlotUpdate&)> handler) override;
    void register_config_update_handler(std::function<void(const ConfigurationUpdate&)> handler) override;

    // \brief Provide the means, to change module config parameters at runtime
    using SetParamCallback = std::function<SetParameterResponse(
        const everest::config::ConfigurationParameterIdentifier&, const std::string&)>;
    void register_set_runtime_parameter_handler(const SetParamCallback& callback);

    // --- Module state ---
    void set_modules_stopped() override;
    void set_modules_running() override;
    void set_modules_starting() override;
    void set_modules_stopping() override;
    void notice_cfg_validation_failed() override;
    void notice_module_restart_triggered() override;

private:
    everest::config::ModuleConfigurations module_configs_;
    ConfigParseSettings parse_settings_;
    everest::config::SqliteConfigSlotManager slot_manager_;
    /// \brief Keepalive for the shared connection
    std::shared_ptr<everest::db::sqlite::ConnectionInterface> db_;
    int active_slot_id_{-1};
    ActiveSlotStatus module_status_{ActiveSlotStatus::Stopped};

    std::vector<std::function<void(const ActiveSlotUpdate&)>> active_slot_handlers_;
    std::vector<std::function<void(const ConfigurationUpdate&)>> config_update_handlers_;

    void reload_from_storage();

    std::unique_ptr<everest::config::SqliteStorage> make_storage(int slot_id);
    void publish_active_slot_update();
    void publish_config_update(const ConfigurationUpdate& update);

    /// \brief Storage handle for the currently active slot, used to persist runtime config writes.
    std::unique_ptr<everest::config::SqliteStorage> active_storage_;
};

} // namespace Everest::config
