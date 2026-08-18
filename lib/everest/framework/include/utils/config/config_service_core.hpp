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
    /// \param persistence_mirror Optional secondary storage that active-slot parameter writes are
    ///                       persisted to in addition to the slot storage. Used when the manager
    ///                       runs on an in-memory database: the mirror (the user-config YAML) is
    ///                       then the only persistence that survives a restart, so a failed mirror
    ///                       write rejects the update.
    ConfigServiceCore(const ConfigParseSettings& parse_settings,
                      std::shared_ptr<everest::db::sqlite::ConnectionInterface> db_connection,
                      std::unique_ptr<everest::config::StorageInterface> persistence_mirror = nullptr);
    ~ConfigServiceCore() override;

    // --- Re-initialize configuration ---
    // \brief Reloads the active_slot_id from the db and reloads the modules accordingly
    /// \param force_reload Forces a reload even when the active slot did not change
    void reinitialize_from_db(bool force_reload = false);

    // --- Active-slot in-memory access (zero-copy) ---
    std::shared_ptr<const everest::config::ModuleConfigurations> get_active_module_configurations() const override;

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
    void set_modules_stopped() override;
    void set_modules_running() override;
    void set_modules_starting() override;
    void set_modules_stopping() override;
    void notice_cfg_validation_failed() override;
    void notice_module_restart_triggered() override;

    /// \brief Report that the modules came to rest with no start attempt in flight.
    ///
    /// Settles any transitional status to Stopped. A previously recorded FailedToStart is kept: it is
    /// a resting status too (see modules_are_down()) and carries the "the last start attempt failed"
    /// diagnostic until the next start attempt overwrites it via set_modules_starting(). Idempotent -
    /// no event is published when the status is already a resting one, so the usual
    /// ShutdownFinalizing -> Idle sequence still emits exactly one Stopped update.
    ///
    /// Not part of ConfigServiceInterface: like reinitialize_from_db(), this is lifecycle plumbing
    /// for the owner of the module processes (the manager).
    void set_modules_at_rest();

protected:
    // --- Slot-scoped configuration ---
    GetConfigurationResult get_configuration_v(int slot_id, bool force_read_from_db) override;
    GetConfigParametersResult
    get_config_parameters_v(int slot_id,
                            const std::vector<everest::config::ConfigurationParameterIdentifier>& parameters,
                            bool force_read_from_db) override;

private:
    everest::config::ModuleConfigurations module_configs_;
    ConfigParseSettings parse_settings_;
    everest::config::SqliteConfigSlotManager slot_manager_;
    /// \brief Keepalive for the shared connection
    std::shared_ptr<everest::db::sqlite::ConnectionInterface> db_;
    std::shared_ptr<const everest::config::ModuleConfigurations> active_configs_ptr_;
    int active_slot_id_{everest::config::SqliteStorage::DEFAULT_CONFIG_ID};
    int next_boot_slot_id_{everest::config::SqliteStorage::DEFAULT_CONFIG_ID};
    ActiveSlotStatus module_status_{ActiveSlotStatus::Stopped};

    std::vector<std::function<void(const ActiveSlotUpdate&)>> active_slot_handlers_;
    std::vector<std::function<void(const ConfigurationUpdate&)>> config_update_handlers_;

    // Actor infrastructure: worker_thread_ drains command_queue_ one task at a
    // time, so all mutable state is touched by a single thread. See the
    // "Threading model" comment at the top of config_service_core.cpp.
    everest::lib::util::thread_safe_queue<std::function<void()>> command_queue_;
    std::thread worker_thread_;
    std::atomic<bool> worker_thread_running_{false};

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
        if (std::this_thread::get_id() == worker_thread_.get_id()) {
            return f();
        }
        auto promise = std::make_shared<std::promise<ReturnType>>();
        auto future = promise->get_future();
        command_queue_.push([promise, f = std::forward<Func>(f)]() mutable {
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
    void internal_set_modules_at_rest();

    void reload_from_storage();

    std::unique_ptr<everest::config::SqliteStorage> make_storage(int slot_id);
    void publish_active_slot_update();
    void publish_config_update(const ConfigurationUpdate& update);

    /// \brief Storage handle for the currently active slot, used to persist runtime config writes.
    std::unique_ptr<everest::config::SqliteStorage> active_storage_;

    /// \brief Optional secondary persistence for active-slot parameter writes (e.g. the user-config
    /// YAML when the manager runs on an in-memory database). Never read from.
    std::unique_ptr<everest::config::StorageInterface> persistence_mirror_;

    SetParamCallback set_parameter_callback_;
};

} // namespace Everest::config
