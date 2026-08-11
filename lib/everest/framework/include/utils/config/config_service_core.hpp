// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <everest/util/queue/thread_safe_queue.hpp>

#include <utils/config/settings.hpp>
#include <utils/config/slot_manager.hpp>
#include <utils/config/storage_sqlite.hpp>
#include <utils/config/types.hpp>
#include <utils/config_service_interface.hpp>

namespace Everest::config {

/// \brief Helper type, which should not be part of the config_service_interface
///
/// The provided set_parameter_callback uses it to return success/failure
enum class SetParameterResponse {
    SetCallFailed,
    ModuleReplied_Applied,
    ModuleReplied_RequiresRestart,
    ModuleReplied_Rejected,
};

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
    /// \brief Stops and joins the actor worker thread.
    ~ConfigServiceCore() override;

    // --- Re-initialize configuration ---
    // \brief Reloads the active_slot_id from the db and reloads the modules accordingly
    /// \param force_reload Forces a reload even when the active slot did not change
    void reinitialize_from_db(bool force_reload = false);

    // --- Active-slot in-memory access (zero-copy) ---
    /// \brief Returns the current lock-free in-memory snapshot of the active slot's module configurations.
    std::shared_ptr<const everest::config::ModuleConfigurations> get_active_module_configurations() const override;

    // --- Slot management ---
    /// \brief Lists metadata for all configuration slots.
    std::vector<SlotInfo> list_all_slots() override;
    /// \brief Returns the id of the currently active slot.
    int get_active_slot_id() override;
    /// \brief Returns the id of the slot that will be activated on the next boot.
    int get_next_boot_slot_id() override;
    /// \brief Marks the given slot as the one to activate on the next boot.
    SetActiveSlotStatus mark_active_slot(int slot_id) override;
    /// \brief Deletes the given slot (never the active or next-boot slot).
    DeleteSlotStatus delete_slot(int slot_id) override;
    /// \brief Duplicates the given slot into a new slot, optionally with a description.
    DuplicateSlotResult duplicate_slot(int slot_id, std::optional<std::string> description) override;
    /// \brief Validates and loads raw YAML into the given (or a newly created) slot.
    LoadFromYamlResult load_from_yaml(const std::string& raw_yaml, std::optional<std::string> description,
                                      std::optional<int> slot_id) override;
    /// \brief Sets the description of the given slot.
    bool set_description(int slot_id, const std::string& description) override;

    // --- Slot-scoped configuration ---
    /// \brief Applies configuration parameter updates to the given slot.
    SetConfigParameterResult set_config_parameters(int slot_id, const std::vector<ConfigParameterUpdate>& updates,
                                                   const Origin& origin) override;

    // --- Push-event subscriptions ---
    // Handlers are invoked on the internal actor thread as part of the mutating operation and
    // block all further config-service processing while they run: keep them short. Calling back
    // into public ConfigServiceCore methods from a handler is safe (executed inline), but do not
    // block on anything that itself waits for the config service.
    void register_active_slot_update_handler(std::function<void(const ActiveSlotUpdate&)> handler) override;
    void register_config_update_handler(std::function<void(const ConfigurationUpdate&)> handler) override;

    // \brief Provide the means, to change module config parameters at runtime.
    // The callback is invoked on the internal actor thread once per updated parameter; its full
    // duration (e.g. a synchronous MQTT round-trip to the target module, including timeouts)
    // blocks every other config-service operation.
    using SetParamCallback = std::function<SetParameterResponse(
        const everest::config::ConfigurationParameterIdentifier&, const std::string&)>;
    void register_set_runtime_parameter_handler(const SetParamCallback& callback);

    // --- Module state ---
    /// \brief Records that the active-slot modules are stopped.
    void set_modules_stopped() override;
    /// \brief Records that the active-slot modules are running.
    void set_modules_running() override;
    /// \brief Records that the active-slot modules are starting.
    void set_modules_starting() override;
    /// \brief Records that the active-slot modules are stopping.
    void set_modules_stopping() override;
    /// \brief Records that module configuration validation failed to start the modules.
    void notice_cfg_validation_failed() override;
    /// \brief Records that a module restart was triggered.
    void notice_module_restart_triggered() override;

protected:
    // --- Slot-scoped configuration ---
    GetConfigurationResult get_configuration_v(int slot_id, bool force_read_from_db) override;
    GetConfigParametersResult
    get_config_parameters_v(int slot_id,
                            const std::vector<everest::config::ConfigurationParameterIdentifier>& parameters,
                            bool force_read_from_db) override;

private:
    everest::config::ModuleConfigurations m_module_configs;
    ConfigParseSettings m_parse_settings;
    everest::config::SqliteConfigSlotManager m_slot_manager;
    /// \brief Keepalive for the shared connection
    std::shared_ptr<everest::db::sqlite::ConnectionInterface> m_db;
    std::shared_ptr<const everest::config::ModuleConfigurations> m_active_configs_ptr;
    int m_active_slot_id{everest::config::SqliteStorage::DEFAULT_CONFIG_ID};
    int m_next_boot_slot_id{everest::config::SqliteStorage::DEFAULT_CONFIG_ID};
    ActiveSlotStatus m_module_status{ActiveSlotStatus::Stopped};

    std::vector<std::function<void(const ActiveSlotUpdate&)>> m_active_slot_handlers;
    std::vector<std::function<void(const ConfigurationUpdate&)>> m_config_update_handlers;

    // Actor infrastructure: m_worker_thread drains m_command_queue one task at a
    // time, so all mutable state is touched by a single thread. See the
    // "Threading model" comment at the top of config_service_core.cpp.
    everest::lib::util::thread_safe_queue<std::function<void()>> m_command_queue;
    std::thread m_worker_thread;
    std::atomic<bool> m_worker_thread_running{false};

    void process_queue();

    /// \brief Stops and joins the worker thread. Used by the destructor and by the constructor's
    ///        failure path (where the destructor will never run).
    void stop_worker();

    template <typename Func> auto post_to_actor(Func&& f) {
        using ReturnType = std::invoke_result_t<Func>;
        // Re-entrant calls from the actor thread itself - e.g. a registered update handler
        // calling back into a public method - execute inline: the actor is busy running the
        // very task that triggered the handler, so waiting on the queue would deadlock. Inline
        // execution preserves the single-writer model, since we already are the actor thread.
        if (std::this_thread::get_id() == m_worker_thread.get_id()) {
            return f();
        }
        auto promise = std::make_shared<std::promise<ReturnType>>();
        auto future = promise->get_future();
        m_command_queue.push([promise, f = std::forward<Func>(f)]() mutable {
            try {
                if constexpr (std::is_void_v<ReturnType>) {
                    f();
                    promise->set_value();
                } else {
                    promise->set_value(f());
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
        return future.get();
    }

    // --- Internal Actor Methods ---
    void internal_reinitialize_from_db(bool force_reload = false);
    std::vector<SlotInfo> internal_list_all_slots();
    int internal_get_active_slot_id();
    int internal_get_next_boot_slot_id();
    SetActiveSlotStatus internal_mark_active_slot(int slot_id);
    DeleteSlotStatus internal_delete_slot(int slot_id);
    DuplicateSlotResult internal_duplicate_slot(int slot_id, std::optional<std::string> description);
    LoadFromYamlResult internal_load_from_yaml(const std::string& raw_yaml,
                                               const std::optional<std::string>& description,
                                               std::optional<int> slot_id);
    bool internal_set_description(int slot_id, const std::string& description);
    GetConfigurationResult internal_get_configuration(int slot_id, bool force_read_from_db);
    SetConfigParameterResult internal_set_config_parameters(int slot_id,
                                                            const std::vector<ConfigParameterUpdate>& updates,
                                                            const Origin& origin);
    /// \brief Apply updates to the active slot: run module callbacks for live ReadWrite params,
    /// persist the rest, and record accepted changes in \p event.
    void apply_active_slot_updates(const std::vector<ConfigParameterUpdate>& updates, SetConfigParameterResult& result,
                                   ConfigurationUpdate& event);
    /// \brief Apply updates to a non-active slot by writing straight to its storage.
    void apply_inactive_slot_updates(int slot_id, const std::vector<ConfigParameterUpdate>& updates,
                                     SetConfigParameterResult& result, ConfigurationUpdate& event);
    GetConfigParametersResult
    internal_get_config_parameters(int slot_id,
                                   const std::vector<everest::config::ConfigurationParameterIdentifier>& parameters,
                                   bool force_read_from_db);
    void internal_set_modules_stopped();
    void internal_set_modules_running();
    void internal_set_modules_starting();
    void internal_set_modules_stopping();
    void internal_notice_cfg_validation_failed();
    void internal_notice_module_restart_triggered();

    void reload_from_storage();

    std::unique_ptr<everest::config::SqliteStorage> make_storage(int slot_id);
    void publish_active_slot_update();
    void publish_config_update(const ConfigurationUpdate& update);

    /// \brief Storage handle for the currently active slot, used to persist runtime config writes.
    std::unique_ptr<everest::config::SqliteStorage> m_active_storage;

    SetParamCallback m_set_parameter_callback;
};

} // namespace Everest::config
