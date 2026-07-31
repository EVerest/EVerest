// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <utils/config/config_service_core.hpp>

#include <optional>

#include <date/tz.h>
#include <everest/logging.hpp>
#include <fmt/format.h>

#include <everest/utils/yaml_loader.hpp>
#include <utils/config.hpp>
#include <utils/config/storage_sqlite.hpp>
#include <utils/config/types.hpp>
#include <utils/date.hpp>

namespace ec = everest::config;
namespace Everest::config {

// ============================================================================
// Threading model: single-writer actor
// ============================================================================
//
// All mutable state (m_module_configs, m_active_slot_id, m_module_status,
// m_active_storage, the handler lists, m_set_parameter_callback, ...) is owned by
// a single "actor" thread running process_queue(), which executes queued tasks
// one at a time. Because every mutation happens on that one thread, the
// internal_* methods need no locks.
//
//   * Public methods just hand a lambda to post_to_actor() and wait for it.
//     post_to_actor() detects calls made from the actor thread itself and runs
//     them inline instead of queueing (which would deadlock: the actor would
//     block on a task only it could run).
//   * internal_* methods hold the real logic, always run on the actor thread,
//     and call other internal_* methods directly for clarity.
//   * Registered update handlers (active-slot/config updates) and the runtime
//     set-parameter callback are invoked on the actor thread as part of the
//     mutating task and block all further processing while they run. Thanks to
//     the inline re-entrancy above they may call public methods, but they must
//     not block on anything that itself waits for the config service.
//
// Lock-free reads: get_active_module_configurations() bypasses the actor and
// std::atomic_load()s the current immutable snapshot. The actor publishes a new
// snapshot via std::atomic_store() on every change, so readers never block the
// actor nor see a half-written config.
//
// Runtime module callbacks are processed sequentially.
//
// Shutdown: ~ConfigServiceCore() clears m_worker_thread_running, stops
// m_command_queue and joins the worker thread.
// ============================================================================

namespace {
std::string now_rfc3339() {
    return Everest::Date::to_rfc3339(date::utc_clock::now());
}

std::optional<std::pair<ec::ConfigurationParameter*, ec::Access*>>
get_parameter(ec::ConfigurationParameterIdentifier const& id, ec::ModuleConfigurations& configurations) {
    auto mod_it = configurations.find(id.module_id);
    if (mod_it == configurations.end()) {
        return std::nullopt;
    }

    auto impl_id = id.module_implementation_id.value_or("!module");
    auto params_it = mod_it->second.configuration_parameters.find(impl_id);
    if (params_it == mod_it->second.configuration_parameters.end()) {
        return std::nullopt;
    }
    auto it = std::find_if(params_it->second.begin(), params_it->second.end(),
                           [&](const auto& p) { return p.name == id.configuration_parameter_name; });
    if (it == params_it->second.end()) {
        return std::nullopt;
    }
    return std::optional{std::pair{&*it, &mod_it->second.access}};
}

// Look up the parameter targeted by \p id. On success returns the parameter and its module's
// access rules; on failure fills \p per_result.status_info with a human-readable reason and
// returns nullopt (leaving the status enum untouched).
std::optional<std::pair<ec::ConfigurationParameter*, ec::Access*>>
find_parameter_or_explain(const ec::ConfigurationParameterIdentifier& id, ec::ModuleConfigurations& configurations,
                          SetConfigPerParameterResult& per_result) {
    auto lookup = get_parameter(id, configurations);
    if (lookup.has_value()) {
        return lookup;
    }
    if (configurations.find(id.module_id) == configurations.end()) {
        per_result.status_info = fmt::format("Unknown target module: {}", id.module_id);
    } else {
        per_result.status_info =
            fmt::format("Unknown parameter: {} in module: {}", id.configuration_parameter_name, id.module_id);
    }
    return std::nullopt;
}

// Persist a parameter update to the slot's storage. This runs *before* the module is consulted
// (persist-first): on success the value applies on the next restart regardless of the runtime
// outcome. The value is stored as-is - no datatype or range validation happens at this layer.
// Returns false on a storage error, in which case the change is NOT persisted and will not
// apply on restart.
bool persist_parameter_change(ec::SqliteStorage& storage, const ec::ConfigurationParameterIdentifier& identifier,
                              const ec::ConfigurationParameterCharacteristics& characteristics,
                              const std::string& value) {
    const auto write_status = storage.write_configuration_parameter(identifier, characteristics, value);
    if (write_status != ec::GetSetResponseStatus::OK) {
        EVLOG_error << "ConfigServiceCore: Couldn't persist the configuration parameter change to storage; "
                       "the change will not apply on restart.";
        return false;
    }
    return true;
}
} // namespace

ConfigServiceCore::ConfigServiceCore(const ConfigParseSettings& parse_settings,
                                     std::shared_ptr<everest::db::sqlite::ConnectionInterface> db_connection) :
    m_parse_settings(parse_settings),
    m_slot_manager(db_connection),
    m_db(std::move(db_connection)),
    m_next_boot_slot_id(m_slot_manager.get_next_boot_slot_id()) {
    m_active_configs_ptr = std::make_shared<const ec::ModuleConfigurations>();

    m_worker_thread_running = true;
    m_worker_thread = std::thread(&ConfigServiceCore::process_queue, this);

    try {
        post_to_actor([this]() { internal_reinitialize_from_db(true); });
    } catch (...) {
        // The initial reload can throw (e.g. on a corrupt or unreadable database). A throwing
        // constructor never runs the destructor, so the worker thread must be stopped here;
        // otherwise stack unwinding would destroy a joinable std::thread and abort the whole
        // process via std::terminate instead of letting the caller handle the error.
        stop_worker();
        throw;
    }
}

ConfigServiceCore::~ConfigServiceCore() {
    stop_worker();
}

void ConfigServiceCore::stop_worker() {
    m_worker_thread_running = false;
    m_command_queue.stop();
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }
}

void ConfigServiceCore::process_queue() {
    while (m_worker_thread_running) {
        try {
            auto task = m_command_queue.wait_and_pop();
            if (task) {
                (*task)();
            }
        } catch (const std::exception& e) {
            EVLOG_error << "Caught exception in process_queue(): " << e.what();
        } catch (...) {
            EVLOG_error << "Caught non-std::exception in process_queue().";
        }
    }
}

void ConfigServiceCore::reinitialize_from_db(bool force_reload) {
    post_to_actor([this, force_reload]() { internal_reinitialize_from_db(force_reload); });
}

void ConfigServiceCore::internal_reinitialize_from_db(bool force_reload) {
    if (m_module_status != ActiveSlotStatus::Stopped) {
        return;
    }
    m_next_boot_slot_id = m_slot_manager.get_next_boot_slot_id();
    const int new_active_slot_id = m_next_boot_slot_id;
    bool slot_changed = (new_active_slot_id != m_active_slot_id);
    if (slot_changed or force_reload) {
        slot_changed = true;
        m_active_slot_id = new_active_slot_id;
        m_active_storage = make_storage(m_active_slot_id);
    }
    // always reload module configuration in order to include possible WillApplyOnRestart changes
    reload_from_storage();
    if (slot_changed) {
        publish_active_slot_update();
    }
}

std::unique_ptr<ec::SqliteStorage> ConfigServiceCore::make_storage(int slot_id) {
    return std::make_unique<ec::SqliteStorage>(m_db, slot_id);
}

void ConfigServiceCore::publish_active_slot_update() {
    const ActiveSlotUpdate update{now_rfc3339(), m_active_slot_id, m_next_boot_slot_id, m_module_status};
    auto handlers_copy = m_active_slot_handlers;

    for (const auto& handler : handlers_copy) {
        handler(update);
    }
}

void ConfigServiceCore::publish_config_update(const ConfigurationUpdate& update) {
    auto handlers_copy = m_config_update_handlers;

    for (const auto& handler : handlers_copy) {
        handler(update);
    }
}

// --- Active-slot in-memory access ---

std::shared_ptr<const ec::ModuleConfigurations> ConfigServiceCore::get_active_module_configurations() const {
    return std::atomic_load(&m_active_configs_ptr);
}

void ConfigServiceCore::reload_from_storage() {
    if (not m_slot_manager.exists(m_active_slot_id)) {
        return;
    }
    const auto resp = m_active_storage->get_module_configs();
    if (resp.status != ec::GenericResponseStatus::OK) {
        return;
    }
    try {
        // Validate against manifests, interfaces and requirements; enriches configs with manifest metadata.
        m_module_configs = Everest::validate_preloaded_module_configs(m_parse_settings, resp.module_configs);
        std::atomic_store(&m_active_configs_ptr, std::make_shared<const ec::ModuleConfigurations>(m_module_configs));
    } catch (const std::exception& e) {
        EVLOG_error << "Configuration loaded from database for slot " << m_active_slot_id
                    << " failed validation: " << e.what() << " -> Keeping the previous in-memory configuration.";
    }
}

// --- Slot management ---

std::vector<SlotInfo> ConfigServiceCore::list_all_slots() {
    return post_to_actor([this]() { return internal_list_all_slots(); });
}
std::vector<SlotInfo> ConfigServiceCore::internal_list_all_slots() {
    return m_slot_manager.list_slots();
}

int ConfigServiceCore::get_active_slot_id() {
    return post_to_actor([this]() { return internal_get_active_slot_id(); });
}
int ConfigServiceCore::internal_get_active_slot_id() {
    return m_active_slot_id;
}

int ConfigServiceCore::get_next_boot_slot_id() {
    return post_to_actor([this]() { return internal_get_next_boot_slot_id(); });
}
int ConfigServiceCore::internal_get_next_boot_slot_id() {
    return m_next_boot_slot_id;
}

SetActiveSlotStatus ConfigServiceCore::mark_active_slot(int slot_id) {
    return post_to_actor([this, slot_id]() { return internal_mark_active_slot(slot_id); });
}
SetActiveSlotStatus ConfigServiceCore::internal_mark_active_slot(int slot_id) {
    if (slot_id == m_next_boot_slot_id) {
        return SetActiveSlotStatus::NoChangeRequired;
    }
    if (not m_slot_manager.exists(slot_id)) {
        EVLOG_warning << "Failed to mark slot " << slot_id << " as active: slot does not exist.";
        return SetActiveSlotStatus::DoesNotExist;
    }
    const auto status = m_slot_manager.set_next_boot_slot_id(slot_id);
    if (status != ec::GenericResponseStatus::OK) {
        EVLOG_error << "Failed to mark slot " << slot_id << " as active in database.";
        return SetActiveSlotStatus::Failed;
    }
    m_next_boot_slot_id = slot_id;
    publish_active_slot_update();
    EVLOG_info << "Successfully marked slot " << slot_id << " as active for next boot.";
    return SetActiveSlotStatus::Success;
}

DeleteSlotStatus ConfigServiceCore::delete_slot(int slot_id) {
    return post_to_actor([this, slot_id]() { return internal_delete_slot(slot_id); });
}
DeleteSlotStatus ConfigServiceCore::internal_delete_slot(int slot_id) {
    if (slot_id == m_active_slot_id and m_active_slot_id != m_next_boot_slot_id and
        m_module_status == ActiveSlotStatus::Stopped) {
        m_active_slot_id = m_next_boot_slot_id;
        internal_reinitialize_from_db(true);
        EVLOG_warning << "Switched the active slot to the next boot slot (" << m_active_slot_id
                      << ") and deleted the old one.";
    }
    if (slot_id == m_active_slot_id or slot_id == m_next_boot_slot_id) {
        EVLOG_warning << "Failed to delete slot " << slot_id << ": cannot delete the active or next boot slot.";
        return DeleteSlotStatus::CannotDeleteActiveSlot;
    }
    if (not m_slot_manager.exists(slot_id)) {
        EVLOG_warning << "Failed to delete slot " << slot_id << ": slot does not exist.";
        return DeleteSlotStatus::DoesNotExist;
    }
    const auto status = m_slot_manager.delete_slot(slot_id);
    if (status != ec::GenericResponseStatus::OK) {
        EVLOG_error << "Failed to delete slot " << slot_id << " from database.";
        return DeleteSlotStatus::Failed;
    }
    EVLOG_info << "Successfully deleted slot " << slot_id << ".";
    return DeleteSlotStatus::Success;
}

DuplicateSlotResult ConfigServiceCore::duplicate_slot(int slot_id, std::optional<std::string> description) {
    return post_to_actor([this, slot_id, description]() { return internal_duplicate_slot(slot_id, description); });
}
DuplicateSlotResult ConfigServiceCore::internal_duplicate_slot(int slot_id, std::optional<std::string> description) {
    auto result = m_slot_manager.duplicate_slot(slot_id, description);
    if (result.success) {
        EVLOG_info << "Successfully duplicated slot " << slot_id << " to new slot " << result.slot_id.value_or(-1)
                   << " with description: " << description.value_or("<none>");
    } else {
        EVLOG_error << "Failed to duplicate slot " << slot_id << ".";
    }
    return result;
}

LoadFromYamlResult ConfigServiceCore::load_from_yaml(const std::string& raw_yaml,
                                                     std::optional<std::string> description,
                                                     std::optional<int> slot_id) {
    return post_to_actor(
        [this, raw_yaml, description, slot_id]() { return internal_load_from_yaml(raw_yaml, description, slot_id); });
}
LoadFromYamlResult ConfigServiceCore::internal_load_from_yaml(const std::string& raw_yaml,
                                                              const std::optional<std::string>& description,
                                                              std::optional<int> slot_id) {
    int target_slot_id = slot_id.value_or(m_slot_manager.next_slot_id());

    if (target_slot_id == m_active_slot_id and m_module_status != ActiveSlotStatus::Stopped) {
        const char* err_msg = "Cannot load YAML into the active slot when modules are running";
        EVLOG_warning << "Failed to load from YAML into slot " << target_slot_id << ": " << err_msg;
        return {false, std::nullopt, err_msg};
    }
    const bool into_new_slot = not m_slot_manager.exists(target_slot_id);
    try {
        const auto json_config = Everest::load_yaml_from_string(raw_yaml);

        // Validate against manifests, interfaces and requirements; enriches configs with manifest metadata.
        const auto module_configs = Everest::validate_module_configs(m_parse_settings, json_config);

        // If the slot doesn't exist, create it and write the config
        if (into_new_slot) {
            if (m_slot_manager.write_config_slot(target_slot_id, nlohmann::json(module_configs).dump(), std::nullopt,
                                                 description) != ec::GenericResponseStatus::OK) {
                const char* err_msg = "Failed to create new config slot";
                EVLOG_error << "Failed to load from YAML into slot " << target_slot_id << ": " << err_msg;
                return {false, std::nullopt, err_msg};
            }

            auto storage = make_storage(target_slot_id);

            if (storage->write_module_configs(module_configs) != ec::GenericResponseStatus::OK) {
                m_slot_manager.delete_slot(target_slot_id);
                const char* err_msg = "Failed to write module configs to new slot";
                EVLOG_error << "Failed to load from YAML into slot " << target_slot_id << ": " << err_msg;
                return {false, std::nullopt, err_msg};
            }
        } else {
            if (m_slot_manager.update_config_slot(target_slot_id, nlohmann::json(module_configs).dump(), std::nullopt,
                                                  description) != ec::GenericResponseStatus::OK) {
                const char* err_msg = "Failed to replace config slot";
                EVLOG_error << "Failed to load from YAML into slot " << target_slot_id << ": " << err_msg;
                return {false, std::nullopt, err_msg};
            }
            // overwrite its config with the new one
            auto storage = make_storage(target_slot_id);

            if (storage->replace_module_configs(module_configs) != ec::GenericResponseStatus::OK) {
                // Do nothing - if writing to an existing slot failed, we don't want to delete it;
                const char* err_msg = "Failed to write module configs to existing slot";
                EVLOG_error << "Failed to load from YAML into slot " << target_slot_id << ": " << err_msg;
                return {false, std::nullopt, err_msg};
            }

            if (target_slot_id == m_active_slot_id) {
                internal_reinitialize_from_db(true);
            }
        }

        EVLOG_info << "Successfully loaded from YAML into slot " << target_slot_id << ".";
        return {true, target_slot_id, ""};
    } catch (const std::exception& e) {
        std::string err_msg = fmt::format("Validation failed: {}", e.what());
        EVLOG_error << "Failed to load from YAML into slot " << target_slot_id << ": " << err_msg;
        return {false, std::nullopt, err_msg};
    }
}

bool ConfigServiceCore::set_description(int slot_id, const std::string& description) {
    return post_to_actor([this, slot_id, description]() { return internal_set_description(slot_id, description); });
}
bool ConfigServiceCore::internal_set_description(int slot_id, const std::string& description) {
    if (m_slot_manager.exists(slot_id)) {
        if (m_slot_manager.update_description(slot_id, {description}) == ec::GenericResponseStatus::OK) {
            EVLOG_info << "Successfully set description for slot " << slot_id << ".";
            return true;
        } else {
            EVLOG_error << "Failed to set description for slot " << slot_id << " in database.";
            return false;
        }
    } else {
        EVLOG_warning << "Failed to set description for slot " << slot_id << ": slot does not exist.";
        return false;
    }
}

// --- Slot-scoped configuration ---

GetConfigurationResult ConfigServiceCore::get_configuration_v(int slot_id, bool force_read_from_db) {
    return post_to_actor(
        [this, slot_id, force_read_from_db]() { return internal_get_configuration(slot_id, force_read_from_db); });
}
GetConfigurationResult ConfigServiceCore::internal_get_configuration(int slot_id, bool force_read_from_db) {
    const int resolved_slot_id = (slot_id == ConfigServiceInterface::ACTIVE_SLOT) ? m_active_slot_id : slot_id;
    if (resolved_slot_id == m_active_slot_id and not force_read_from_db) {
        return {GetConfigurationStatus::Success, m_module_configs};
    }

    if (not m_slot_manager.exists(resolved_slot_id)) {
        return {GetConfigurationStatus::SlotDoesNotExist, {}};
    }

    auto storage = make_storage(resolved_slot_id);
    const auto response = storage->get_module_configs();
    if (response.status != ec::GenericResponseStatus::OK) {
        return {GetConfigurationStatus::Failed, {}};
    }
    return {GetConfigurationStatus::Success, response.module_configs};
}

SetConfigParameterResult ConfigServiceCore::set_config_parameters(int slot_id,
                                                                  const std::vector<ConfigParameterUpdate>& updates,
                                                                  const Origin& origin) {
    return post_to_actor(
        [this, slot_id, updates, origin]() { return internal_set_config_parameters(slot_id, updates, origin); });
}
SetConfigParameterResult
ConfigServiceCore::internal_set_config_parameters(int slot_id, const std::vector<ConfigParameterUpdate>& updates,
                                                  const Origin& origin) {
    SetConfigParameterResult result;
    result.status = SetConfigParameterStatus::Ok;

    const int resolved_slot_id = (slot_id == ConfigServiceInterface::ACTIVE_SLOT) ? m_active_slot_id : slot_id;
    const bool modifies_active_slot = resolved_slot_id == m_active_slot_id;
    const bool modules_in_transient =
        m_module_status != ActiveSlotStatus::Running and m_module_status != ActiveSlotStatus::Stopped;

    result.status = SetConfigParameterStatus::Ok;
    result.parameter_results.emplace(updates.size(),
                                     SetConfigPerParameterResult{SetConfigParameterResultEnum::DoesNotExist, ""});

    ConfigurationUpdate event;
    event.timestamp = now_rfc3339();
    event.slot_id = resolved_slot_id;
    event.origin = origin;

    if (modifies_active_slot and modules_in_transient) {
        // Modules are mid-transition; the caller should retry once they settle.
        result.status = SetConfigParameterStatus::ModulesInTransientState;
        std::fill(result.parameter_results->begin(), result.parameter_results->end(),
                  SetConfigPerParameterResult{SetConfigParameterResultEnum::RetryLater, ""});
    } else if (modifies_active_slot) {
        apply_active_slot_updates(updates, result, event);
    } else {
        apply_inactive_slot_updates(resolved_slot_id, updates, result, event);
    }

    if (not event.updates.empty()) {
        if (modifies_active_slot) {
            std::atomic_store(&m_active_configs_ptr,
                              std::make_shared<const ec::ModuleConfigurations>(m_module_configs));
        }
        publish_config_update(event);
        EVLOG_info << "Successfully set " << event.updates.size() << " of " << updates.size()
                   << " config parameter(s) for slot " << resolved_slot_id << ".";
    } else if (!updates.empty()) {
        EVLOG_error << "Failed to set any config parameters for slot " << resolved_slot_id
                    << "; all updates were rejected or invalid.";
    }

    return result;
}

void ConfigServiceCore::apply_active_slot_updates(const std::vector<ConfigParameterUpdate>& updates,
                                                  SetConfigParameterResult& result, ConfigurationUpdate& event) {
    const bool modules_are_running = m_module_status == ActiveSlotStatus::Running;

    for (size_t i = 0; i < updates.size(); ++i) {
        const auto& update = updates[i];
        auto& per_result = result.parameter_results.value()[i];
        SetConfigParameterResultEnum& result_enum = per_result.status;

        auto lookup = find_parameter_or_explain(update.identifier, m_module_configs, per_result);
        if (not lookup.has_value()) {
            // result_enum already set to DoesNotExist
            continue;
        }
        auto [in_memory_parameter, parameter_access] = lookup.value();
        const auto mutability = in_memory_parameter->characteristics.mutability;

        // Validate the value against the parameter's datatype before anything is persisted; a value
        // that would fail to parse on the next boot must never reach the database. No range
        // (min/max) validation happens at this layer.
        if (const auto validation_error =
                ec::validate_config_value(in_memory_parameter->characteristics, update.value)) {
            result_enum = SetConfigParameterResultEnum::Rejected;
            per_result.status_info = validation_error.value();
            continue;
        }

        // The parameter exists and the value is well-formed -> store the update in the database so
        // it can be applied on the next restart. Persist-first: this happens before the module is
        // consulted.
        if (not persist_parameter_change(*m_active_storage, update.identifier, in_memory_parameter->characteristics,
                                         update.value)) {
            result_enum = SetConfigParameterResultEnum::Rejected;
            per_result.status_info = "Failed to persist change";
            continue;
        }
        result_enum = SetConfigParameterResultEnum::WillApplyOnRestart;

        // Now test if the module applies the update at runtime
        if (mutability == ec::Mutability::ReadWrite and modules_are_running) {
            if (not m_set_parameter_callback) {
                // The value is already persisted; without a runtime-change callback it simply
                // takes effect on the next restart.
                per_result.status_info = "No runtime-change callback registered; value persisted and "
                                         "applies on next restart";
                EVLOG_warning << "ConfigServiceCore: No callback registered for setting the configuration parameter "
                                 "change in the module -> change applies on next restart.";
            } else {
                switch (m_set_parameter_callback(update.identifier, update.value)) {
                case SetParameterResponse::SetCallFailed:
                    per_result.status_info = "Module not responding";
                    break;
                case SetParameterResponse::ModuleReplied_Rejected:
                    per_result.status_info = "Runtime change rejected by module";
                    break;
                case SetParameterResponse::ModuleReplied_Applied: {
                    result_enum = SetConfigParameterResultEnum::Applied;
                    // Module confirmed it immediately applied the value; mutate the in-memory state
                    const auto parsed_value =
                        ec::parse_config_value(in_memory_parameter->characteristics.datatype, update.value);
                    in_memory_parameter->value = parsed_value;
                    break;
                }
                case SetParameterResponse::ModuleReplied_RequiresRestart:
                    break;
                default:
                    EVLOG_error << "ConfigServiceCore: Unexpected SetParameterResponse from runtime-change callback; "
                                   "the value stays persisted and applies on next restart.";
                    break;
                }
            }
        }
        // Now that we know if the change was applied at runtime or only after the next reboot, extend the update notice
        // record
        event.updates.push_back({update.identifier, update.value, result_enum});
    }
}

void ConfigServiceCore::apply_inactive_slot_updates(int slot_id, const std::vector<ConfigParameterUpdate>& updates,
                                                    SetConfigParameterResult& result, ConfigurationUpdate& event) {
    if (not m_slot_manager.exists(slot_id)) {
        std::fill(result.parameter_results->begin(), result.parameter_results->end(),
                  SetConfigPerParameterResult{SetConfigParameterResultEnum::DoesNotExist, "Slot does not exist"});
        return;
    }

    auto storage = make_storage(slot_id);
    auto inactive_configuration = storage->get_module_configs();
    if (inactive_configuration.status != ec::GenericResponseStatus::OK) {
        std::fill(result.parameter_results->begin(), result.parameter_results->end(),
                  SetConfigPerParameterResult{SetConfigParameterResultEnum::Rejected, ""});
        return;
    }

    for (size_t i = 0; i < updates.size(); ++i) {
        const auto& update = updates[i];
        auto& per_result = result.parameter_results.value()[i];

        auto lookup = find_parameter_or_explain(update.identifier, inactive_configuration.module_configs, per_result);
        if (not lookup.has_value()) {
            continue;
        }
        auto [parameter, parameter_access] = lookup.value();

        // Validate the value against the parameter's datatype before persisting; a value that would
        // fail to parse when booting from this slot must never reach the database. No range
        // (min/max) validation happens at this layer.
        if (const auto validation_error = ec::validate_config_value(parameter->characteristics, update.value)) {
            per_result.status = SetConfigParameterResultEnum::Rejected;
            per_result.status_info = validation_error.value();
            continue;
        }

        const auto write_status =
            storage->write_configuration_parameter(update.identifier, parameter->characteristics, update.value);
        if (write_status == ec::GetSetResponseStatus::OK) {
            per_result.status = SetConfigParameterResultEnum::WillApplyOnRestart;
            event.updates.push_back(
                {update.identifier, update.value, SetConfigParameterResultEnum::WillApplyOnRestart});
        } else {
            per_result.status = SetConfigParameterResultEnum::Rejected;
        }
    }
}

GetConfigParametersResult ConfigServiceCore::get_config_parameters_v(
    int slot_id, const std::vector<ec::ConfigurationParameterIdentifier>& parameters, bool force_read_from_db) {
    return post_to_actor([this, slot_id, parameters, force_read_from_db]() {
        return internal_get_config_parameters(slot_id, parameters, force_read_from_db);
    });
}
GetConfigParametersResult ConfigServiceCore::internal_get_config_parameters(
    int slot_id, const std::vector<ec::ConfigurationParameterIdentifier>& parameters, bool force_read_from_db) {
    GetConfigurationResult get_cfg_result = internal_get_configuration(slot_id, force_read_from_db);

    GetConfigParametersResult result;
    result.status = GetConfigurationStatus::SlotDoesNotExist;

    if (get_cfg_result.status == GetConfigurationStatus::Success) {
        result.status = GetConfigurationStatus::Success;

        auto& module_cfgs = get_cfg_result.module_configurations;

        for (const auto& requested_param : parameters) {
            auto param = get_parameter(requested_param, module_cfgs);

            if (param.has_value()) {
                auto [parameter, parameter_access] = param.value();
                result.parameters.push_back(*parameter);
            } else {
                result.parameters.push_back(std::nullopt);
            }
        }
    }

    return result;
}

// --- Push-event subscriptions ---

void ConfigServiceCore::register_active_slot_update_handler(std::function<void(const ActiveSlotUpdate&)> handler) {
    post_to_actor(
        [this, handler = std::move(handler)]() mutable { m_active_slot_handlers.push_back(std::move(handler)); });
}

void ConfigServiceCore::register_config_update_handler(std::function<void(const ConfigurationUpdate&)> handler) {
    post_to_actor(
        [this, handler = std::move(handler)]() mutable { m_config_update_handlers.push_back(std::move(handler)); });
}

void ConfigServiceCore::register_set_runtime_parameter_handler(const SetParamCallback& callback) {
    post_to_actor([this, callback]() { m_set_parameter_callback = callback; });
}

// --- Module state ---
void ConfigServiceCore::set_modules_running() {
    post_to_actor([this]() { internal_set_modules_running(); });
}
void ConfigServiceCore::internal_set_modules_running() {
    m_module_status = ActiveSlotStatus::Running;
    publish_active_slot_update();
}

void ConfigServiceCore::set_modules_stopped() {
    post_to_actor([this]() { internal_set_modules_stopped(); });
}
void ConfigServiceCore::internal_set_modules_stopped() {
    m_module_status = ActiveSlotStatus::Stopped;
    publish_active_slot_update();
}

void ConfigServiceCore::set_modules_starting() {
    post_to_actor([this]() { internal_set_modules_starting(); });
}
void ConfigServiceCore::internal_set_modules_starting() {
    m_module_status = ActiveSlotStatus::Starting;
    publish_active_slot_update();
}

void ConfigServiceCore::set_modules_stopping() {
    post_to_actor([this]() { internal_set_modules_stopping(); });
}
void ConfigServiceCore::internal_set_modules_stopping() {
    m_module_status = ActiveSlotStatus::Stopping;
    publish_active_slot_update();
}

void ConfigServiceCore::notice_cfg_validation_failed() {
    post_to_actor([this]() { internal_notice_cfg_validation_failed(); });
}
void ConfigServiceCore::internal_notice_cfg_validation_failed() {
    m_module_status = ActiveSlotStatus::FailedToStart;
    publish_active_slot_update();
}

void ConfigServiceCore::notice_module_restart_triggered() {
    post_to_actor([this]() { internal_notice_module_restart_triggered(); });
}
void ConfigServiceCore::internal_notice_module_restart_triggered() {
    m_module_status = ActiveSlotStatus::RestartTriggered;
    publish_active_slot_update();
}

} // namespace Everest::config
