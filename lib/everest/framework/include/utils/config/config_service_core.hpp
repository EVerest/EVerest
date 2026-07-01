// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <future>
#include <thread>
#include <type_traits>

#include <everest/util/queue/thread_safe_queue.hpp>
#include <everest/util/async/thread_pool_scaling.hpp>

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
    /// \param spawn_threads  Whether to use background threads for async/actor tasks (default true).
    /// \param max_worker_threads Maximum number of concurrent network callbacks allowed (default 10).
    ConfigServiceCore(const ConfigParseSettings& parse_settings,
                      std::shared_ptr<everest::db::sqlite::ConnectionInterface> db_connection,
                      bool spawn_threads = true,
                      unsigned int max_worker_threads = 10);
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
    bool spawn_threads_;
    std::shared_ptr<const everest::config::ModuleConfigurations> active_configs_ptr_;
    everest::config::ModuleConfigurations module_configs_;
    ConfigParseSettings parse_settings_;
    everest::config::SqliteConfigSlotManager slot_manager_;
    /// \brief Keepalive for the shared connection
    std::shared_ptr<everest::db::sqlite::ConnectionInterface> db_;
    int active_slot_id_{-1};
    ActiveSlotStatus module_status_{ActiveSlotStatus::Stopped};

    std::vector<std::function<void(const ActiveSlotUpdate&)>> active_slot_handlers_;
    std::vector<std::function<void(const ConfigurationUpdate&)>> config_update_handlers_;

    everest::lib::util::thread_safe_queue<std::function<void()>> command_queue_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    void process_queue();

    template <typename Func>
    auto post_to_actor(Func&& f) {
        using ReturnType = std::invoke_result_t<Func>;
        if (!spawn_threads_) {
            if constexpr (std::is_void_v<ReturnType>) {
                f();
                return;
            } else {
                return f();
            }
        }

        auto promise = std::make_shared<std::promise<ReturnType>>();
        auto future = promise->get_future();
        command_queue_.push([promise, f = std::forward<Func>(f)]() mutable {
            if constexpr (std::is_void_v<ReturnType>) {
                f();
                promise->set_value();
            } else {
                promise->set_value(f());
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
    LoadFromYamlResult internal_load_from_yaml(const std::string& raw_yaml, std::optional<std::string> description, std::optional<int> slot_id);
    bool internal_set_description(int slot_id, const std::string& description);
    GetConfigurationResult internal_get_configuration(int slot_id);
    SetConfigParameterResult internal_set_config_parameters(int slot_id, const std::vector<ConfigParameterUpdate>& updates, const Origin& origin);
    GetConfigParametersResult internal_get_config_parameters(int slot_id, const std::vector<everest::config::ConfigurationParameterIdentifier>& parameters);
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
    std::unique_ptr<everest::config::SqliteStorage> active_storage_;

    SetParamCallback set_parameter_callback_;

    std::vector<std::future<SetParameterResponse>> orphaned_futures_;

    everest::lib::util::thread_pool_scaling<> async_worker_pool_;
};

} // namespace Everest::config
