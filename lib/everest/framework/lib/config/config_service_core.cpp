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

namespace Everest::config {

// ============================================================================
// Threading model: single-writer actor
// ============================================================================
//
// All mutable state (module_configs_, active_slot_id_, module_status_,
// active_storage_, the handler lists, set_parameter_callback_, ...) is owned by
// a single "actor" thread running process_queue(), which executes queued tasks
// one at a time. Because every mutation happens on that one thread, the
// internal_* methods need no locks.
//
//   * Public methods just hand a lambda to post_to_actor() and wait for it.
//   * internal_* methods hold the real logic, always run on the actor thread,
//     and therefore call other internal_* methods directly -- never the public
//     wrappers, which would deadlock (the actor would block on a task only it
//     could run).
//   * post_to_actor() (in the header) runs inline when spawn_threads_ == false
//     (deterministic single-threaded mode used by tests), otherwise enqueues
//     and blocks the caller on the result.
//
// Lock-free reads: get_active_module_configurations() bypasses the actor and
// std::atomic_load()s the current immutable snapshot. The actor publishes a new
// snapshot via std::atomic_store() on every change, so readers never block the
// actor nor see a half-written config.
//
// Runtime module callbacks: set_config_parameters dispatches the (potentially
// slow) module callback to async_worker_pool_ and then waits on the futures
// with a timeout -- so the actor CAN block here for up to that timeout. In
// practice a healthy module replies well within it; the bounded wait is a
// deliberate trade-off, and any future that does time out is parked in
// orphaned_futures_ (reaped on the next call) so the actor still returns.
//
// Event delivery: publish_*_update() copy the handler list and, in threaded
// mode, run handlers on a detached thread, so handlers may call back into the
// service without deadlocking the actor.
//
// Shutdown: ~ConfigServiceCore() clears running_, stops command_queue_ and
// joins the worker thread.
// ============================================================================

namespace {
std::string now_rfc3339() {
    return Everest::Date::to_rfc3339(date::utc_clock::now());
}

std::optional<std::pair<everest::config::ConfigurationParameter*, everest::config::Access*>>
get_parameter(everest::config::ConfigurationParameterIdentifier const& id,
              everest::config::ModuleConfigurations& configurations) {
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

bool is_set_read_only_allowed(const everest::config::Access& access, const std::string& target_module_id) {
    if (not access.config.has_value()) {
        return false;
    }
    const auto& config_access = access.config.value();
    if (config_access.allow_set_read_only) {
        return true;
    }
    const auto it = config_access.modules.find(target_module_id);
    return it != config_access.modules.end() and it->second.allow_set_read_only;
}
} // namespace

ConfigServiceCore::ConfigServiceCore(const ConfigParseSettings& parse_settings,
                                     std::shared_ptr<everest::db::sqlite::ConnectionInterface> db_connection) :
    parse_settings_(parse_settings), slot_manager_(db_connection), db_(std::move(db_connection)) {
    active_slot_id_ = everest::config::SqliteStorage::DEFAULT_CONFIG_ID;
    active_configs_ptr_ = std::make_shared<const everest::config::ModuleConfigurations>();

    running_ = true;
    worker_thread_ = std::thread(&ConfigServiceCore::process_queue, this);

    post_to_actor([this]() { internal_reinitialize_from_db(true); });
}

ConfigServiceCore::~ConfigServiceCore() {
    running_ = false;
    command_queue_.stop();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void ConfigServiceCore::process_queue() {
    while (running_) {
        auto task = command_queue_.wait_and_pop();
        if (task) {
            (*task)();
        }
    }
}

void ConfigServiceCore::reinitialize_from_db(bool force_reload) {
    post_to_actor([this, force_reload]() { internal_reinitialize_from_db(force_reload); });
}

void ConfigServiceCore::internal_reinitialize_from_db(bool force_reload) {
    if (module_status_ != ActiveSlotStatus::Stopped) {
        return;
    }
    const int new_active_slot_id = slot_manager_.get_next_boot_slot_id();
    bool slot_changed = (new_active_slot_id != active_slot_id_);
    if (slot_changed or force_reload) {
        slot_changed = true;
        active_slot_id_ = new_active_slot_id;
        active_storage_ = make_storage(active_slot_id_);
    }
    // always reload module configuration in order to include possible WillApplyOnRestart changes
    reload_from_storage();
    if (slot_changed) {
        publish_active_slot_update();
    }
}

std::unique_ptr<everest::config::SqliteStorage> ConfigServiceCore::make_storage(int slot_id) {
    return std::make_unique<everest::config::SqliteStorage>(db_, slot_id);
}

void ConfigServiceCore::publish_active_slot_update() {
    const ActiveSlotUpdate update{now_rfc3339(), active_slot_id_, slot_manager_.get_next_boot_slot_id(),
                                  module_status_};
    auto handlers_copy = active_slot_handlers_;
    std::thread([handlers_copy = std::move(handlers_copy), update]() {
        for (const auto& handler : handlers_copy) {
            handler(update);
        }
    }).detach();
}

void ConfigServiceCore::publish_config_update(const ConfigurationUpdate& update) {
    auto handlers_copy = config_update_handlers_;
    std::thread([handlers_copy = std::move(handlers_copy), update]() {
        for (const auto& handler : handlers_copy) {
            handler(update);
        }
    }).detach();
}

// --- Active-slot in-memory access ---

std::shared_ptr<const everest::config::ModuleConfigurations>
ConfigServiceCore::get_active_module_configurations() const {
    return std::atomic_load(&active_configs_ptr_);
}

void ConfigServiceCore::reload_from_storage() {
    if (slot_manager_.exists(active_slot_id_)) {
        const auto resp = active_storage_->get_module_configs();
        if (resp.status == everest::config::GenericResponseStatus::OK) {
            module_configs_ = resp.module_configs;
            std::atomic_store(&active_configs_ptr_,
                              std::make_shared<const everest::config::ModuleConfigurations>(module_configs_));
        }
    }
}

// --- Slot management ---

std::vector<SlotInfo> ConfigServiceCore::list_all_slots() {
    return post_to_actor([this]() { return internal_list_all_slots(); });
}
std::vector<SlotInfo> ConfigServiceCore::internal_list_all_slots() {
    return slot_manager_.list_slots();
}

int ConfigServiceCore::get_active_slot_id() {
    return post_to_actor([this]() { return internal_get_active_slot_id(); });
}
int ConfigServiceCore::internal_get_active_slot_id() {
    return active_slot_id_;
}

int ConfigServiceCore::get_next_boot_slot_id() {
    return post_to_actor([this]() { return internal_get_next_boot_slot_id(); });
}
int ConfigServiceCore::internal_get_next_boot_slot_id() {
    return slot_manager_.get_next_boot_slot_id();
}

SetActiveSlotStatus ConfigServiceCore::mark_active_slot(int slot_id) {
    return post_to_actor([this, slot_id]() { return internal_mark_active_slot(slot_id); });
}
SetActiveSlotStatus ConfigServiceCore::internal_mark_active_slot(int slot_id) {
    const int next_boot_slot_id = slot_manager_.get_next_boot_slot_id();
    if (slot_id == next_boot_slot_id) {
        return SetActiveSlotStatus::NoChangeRequired;
    }
    if (not slot_manager_.exists(slot_id)) {
        return SetActiveSlotStatus::DoesNotExist;
    }
    const auto status = slot_manager_.set_next_boot_slot_id(slot_id);
    if (status != everest::config::GenericResponseStatus::OK) {
        return SetActiveSlotStatus::Failed;
    }
    publish_active_slot_update();
    return SetActiveSlotStatus::Success;
}

DeleteSlotStatus ConfigServiceCore::delete_slot(int slot_id) {
    return post_to_actor([this, slot_id]() { return internal_delete_slot(slot_id); });
}
DeleteSlotStatus ConfigServiceCore::internal_delete_slot(int slot_id) {
    if (slot_id == active_slot_id_ or slot_id == slot_manager_.get_next_boot_slot_id()) {
        return DeleteSlotStatus::CannotDeleteActiveSlot;
    }
    // Verify slot exists first
    const auto slots = slot_manager_.list_slots();
    const bool exists = std::any_of(slots.begin(), slots.end(), [slot_id](const auto& s) { return s.id == slot_id; });
    if (not exists) {
        return DeleteSlotStatus::DoesNotExist;
    }
    const auto status = slot_manager_.delete_slot(slot_id);
    if (status != everest::config::GenericResponseStatus::OK) {
        return DeleteSlotStatus::Failed;
    }
    return DeleteSlotStatus::Success;
}

DuplicateSlotResult ConfigServiceCore::duplicate_slot(int slot_id, std::optional<std::string> description) {
    return post_to_actor([this, slot_id, description]() { return internal_duplicate_slot(slot_id, description); });
}
DuplicateSlotResult ConfigServiceCore::internal_duplicate_slot(int slot_id, std::optional<std::string> description) {
    return slot_manager_.duplicate_slot(slot_id, std::move(description));
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
    int target_slot_id = slot_id.value_or(slot_manager_.next_slot_id());

    if (target_slot_id == active_slot_id_ and module_status_ != ActiveSlotStatus::Stopped) {
        return {false, std::nullopt, "Cannot load YAML into the active slot when modules are running"};
    }
    const bool into_new_slot = not slot_manager_.exists(target_slot_id);
    try {
        const auto json_config = Everest::load_yaml_from_string(raw_yaml);
        if (not json_config.contains("active_modules")) {
            return {false, std::nullopt, "YAML does not contain an 'active_modules' section"};
        }

        // Validate against manifests, interfaces and requirements; enriches configs with manifest metadata.
        const auto module_configs = Everest::validate_module_configs(parse_settings_, json_config);

        // If the slot doesn't exist, create it and write the config
        if (into_new_slot) {
            if (slot_manager_.write_config_slot(target_slot_id, nlohmann::json(module_configs).dump(), std::nullopt,
                                                description) != everest::config::GenericResponseStatus::OK) {
                return {false, std::nullopt, "Failed to create new config slot"};
            }

            auto storage = make_storage(target_slot_id);

            if (storage->write_module_configs(module_configs) != everest::config::GenericResponseStatus::OK) {
                slot_manager_.delete_slot(target_slot_id);
                return {false, std::nullopt, "Failed to write module configs to new slot"};
            }
        } else {
            if (slot_manager_.update_config_slot(target_slot_id, nlohmann::json(module_configs).dump(), std::nullopt,
                                                 description) != everest::config::GenericResponseStatus::OK) {
                return {false, std::nullopt, "Failed to replace config slot"};
            }
            // overwrite its config with the new one
            auto storage = make_storage(target_slot_id);

            if (storage->replace_module_configs(module_configs) != everest::config::GenericResponseStatus::OK) {
                // Do nothing - if writing to an existing slot failed, we don't want to delete it;
                return {false, std::nullopt, "Failed to write module configs to existing slot"};
            }
        }

        return {true, target_slot_id, ""};
    } catch (const std::exception& e) {
        return {false, std::nullopt, fmt::format("Validation failed: {}", e.what())};
    }
}

bool ConfigServiceCore::set_description(int slot_id, const std::string& description) {
    return post_to_actor([this, slot_id, description]() { return internal_set_description(slot_id, description); });
}
bool ConfigServiceCore::internal_set_description(int slot_id, const std::string& description) {
    if (slot_manager_.exists(slot_id)) {
        slot_manager_.update_description(slot_id, {description});
        return true;
    } else {
        return false;
    }
}

// --- Slot-scoped configuration ---

GetConfigurationResult ConfigServiceCore::get_configuration(int slot_id) {
    return post_to_actor([this, slot_id]() { return internal_get_configuration(slot_id); });
}
GetConfigurationResult ConfigServiceCore::internal_get_configuration(int slot_id) {
    const int resolved_slot_id = (slot_id == ConfigServiceInterface::ACTIVE_SLOT) ? active_slot_id_ : slot_id;
    if (resolved_slot_id == active_slot_id_) {
        return {GetConfigurationStatus::Success, module_configs_};
    }

    // Check slot exists
    const auto slots = slot_manager_.list_slots();
    const bool exists =
        std::any_of(slots.begin(), slots.end(), [resolved_slot_id](const auto& s) { return s.id == resolved_slot_id; });
    if (not exists) {
        return {GetConfigurationStatus::SlotDoesNotExist, {}};
    }

    auto storage = make_storage(resolved_slot_id);
    const auto response = storage->get_module_configs();
    if (response.status != everest::config::GenericResponseStatus::OK) {
        return {GetConfigurationStatus::SlotDoesNotExist, {}};
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
    const bool modules_are_running = module_status_ == ActiveSlotStatus::Running;
    const bool modules_in_transient =
        (module_status_ != ActiveSlotStatus::Running and module_status_ != ActiveSlotStatus::Stopped);

    const int resolved_slot_id = (slot_id == ConfigServiceInterface::ACTIVE_SLOT) ? active_slot_id_ : slot_id;
    const bool modifies_active_slot = resolved_slot_id == active_slot_id_;

    const std::string& origin_module_id = origin.module_id.value_or("<external>");

    result.parameter_results.emplace(
        updates.size(), Everest::config::SetConfigPerParameterResult{SetConfigParameterResultEnum::DoesNotExist, ""});

    ConfigurationUpdate event;
    event.timestamp = now_rfc3339();
    event.slot_id = resolved_slot_id;

    if (modifies_active_slot and modules_in_transient) {
        result.status = SetConfigParameterStatus::ModulesInTransientState;
        result.parameter_results.emplace(
            updates.size(), Everest::config::SetConfigPerParameterResult{SetConfigParameterResultEnum::RetryLater, ""});
    } else if (modifies_active_slot and not modules_in_transient) {
        for (size_t i = 0; i < updates.size(); ++i) {
            const auto& update = updates[i];
            SetConfigParameterResultEnum& result_enum = result.parameter_results.value()[i].status;
            std::string& status_info = result.parameter_results.value()[i].status_info;

            // does the parameter exist?
            auto parameter_lookup_result = get_parameter(update.identifier, module_configs_);
            if (not parameter_lookup_result.has_value()) {
                if (module_configs_.find(update.identifier.module_id) == module_configs_.end()) {
                    result.parameter_results.value()[i].status_info =
                        fmt::format("Unknown target module: {}", update.identifier.module_id);
                } else {
                    result.parameter_results.value()[i].status_info =
                        fmt::format("Unknown parameter: {} in module: {}",
                                    update.identifier.configuration_parameter_name, update.identifier.module_id);
                }
                continue;
            }
            auto [parameter, parameter_access] = parameter_lookup_result.value();

            // if applicable: set in module and mutate in memory at the same time
            if (parameter->characteristics.mutability == everest::config::Mutability::ReadWrite and
                modules_are_running) {
                // If the callback is not defined (== cannot ask the modules), changes are rejected
                if (not set_parameter_callback_) {
                    result_enum = SetConfigParameterResultEnum::Rejected;
                    EVLOG_error << "ConfigServiceCore: No callback registered for setting the configuration parameter "
                                   "change in the module -> Rejecting.";
                    break;
                }
                auto set_result = set_parameter_callback_(update.identifier, update.value);

                switch (set_result) {
                case SetParameterResponse::SetCallFailed:
                    [[fallthrough]];
                case SetParameterResponse::ModuleReplied_Rejected:
                    result_enum = SetConfigParameterResultEnum::Rejected;
                    status_info = "Rejected by module";
                    continue;
                case SetParameterResponse::ModuleReplied_Applied: {
                    result_enum = SetConfigParameterResultEnum::Applied;
                    // Module confirmed it immediately applied the value; mutate the in-memory state
                    const auto parsed_value =
                        everest::config::parse_config_value(parameter->characteristics.datatype, update.value);
                    parameter->value = parsed_value;
                    break;
                }
                case SetParameterResponse::ModuleReplied_RequiresRestart:
                    result_enum = SetConfigParameterResultEnum::WillApplyOnRestart;
                    break;
                }
            } else if (parameter->characteristics.mutability == everest::config::Mutability::ReadOnly and
                       modules_are_running) {
                if (is_set_read_only_allowed(*parameter_access, update.identifier.module_id)) {
                    result_enum = SetConfigParameterResultEnum::WillApplyOnRestart;
                } else {
                    status_info = "Is a ReadOnly parameter";
                    result_enum = SetConfigParameterResultEnum::Rejected;
                }
            } else {
                // ReadOnly and WriteOnly don't go to the module at runtime
                // ReadWrite also if no modules are running:
                result_enum = SetConfigParameterResultEnum::WillApplyOnRestart;
                // mutate the in-memory state
                const auto parsed_value =
                    everest::config::parse_config_value(parameter->characteristics.datatype, update.value);
                parameter->value = parsed_value;
            }

            if (result_enum == SetConfigParameterResultEnum::Applied or
                result_enum == SetConfigParameterResultEnum::WillApplyOnRestart) {
                event.updates.push_back({update.identifier, update.value, result_enum});
            }

            if (result_enum == SetConfigParameterResultEnum::Applied or
                result_enum == SetConfigParameterResultEnum::WillApplyOnRestart) {
                const auto write_status = active_storage_->write_configuration_parameter(
                    update.identifier, parameter->characteristics, update.value);
                if (write_status != everest::config::GetSetResponseStatus::OK) {
                    EVLOG_error << "ConfigServiceCore: Couldn't persist a configuration parameter change which was "
                                   "accepted by the module.";
                }
            }
        }
    } else {
        const auto slots = slot_manager_.list_slots();
        const bool exists = std::any_of(slots.begin(), slots.end(),
                                        [resolved_slot_id](const auto& s) { return s.id == resolved_slot_id; });
        if (not exists) {
            std::fill(result.parameter_results.value().begin(), result.parameter_results.value().end(),
                      SetConfigPerParameterResult{SetConfigParameterResultEnum::DoesNotExist, "Slot does not exist"});
            return result;
        }

        auto storage = make_storage(resolved_slot_id);
        auto inactive_configuration = storage->get_module_configs();
        if (inactive_configuration.status != everest::config::GenericResponseStatus::OK) {
            std::fill(result.parameter_results.value().begin(), result.parameter_results.value().end(),
                      SetConfigPerParameterResult{SetConfigParameterResultEnum::Rejected, ""});
            return result;
        }

        for (size_t i = 0; i < updates.size(); ++i) {
            const auto& update = updates[i];
            SetConfigParameterResultEnum& result_enum = result.parameter_results.value()[i].status;

            // does the parameter exist?
            auto parameter_lookup_result = get_parameter(update.identifier, inactive_configuration.module_configs);
            if (not parameter_lookup_result.has_value()) {
                if (inactive_configuration.module_configs.find(update.identifier.module_id) ==
                    inactive_configuration.module_configs.end()) {
                    result.parameter_results.value()[i].status_info =
                        fmt::format("Unknown target module: {}", update.identifier.module_id);
                } else {
                    result.parameter_results.value()[i].status_info =
                        fmt::format("Unknown parameter: {} in module: {}",
                                    update.identifier.configuration_parameter_name, update.identifier.module_id);
                }
                continue;
            }
            auto [parameter, parameter_access] = parameter_lookup_result.value();

            const auto write_status =
                storage->write_configuration_parameter(update.identifier, parameter->characteristics, update.value);
            if (write_status == everest::config::GetSetResponseStatus::OK) {
                result_enum = SetConfigParameterResultEnum::WillApplyOnRestart;
                event.updates.push_back(
                    {update.identifier, update.value, SetConfigParameterResultEnum::WillApplyOnRestart});
            } else {
                result_enum = SetConfigParameterResultEnum::Rejected;
                continue;
            }
        }
    }

    if (not event.updates.empty()) {
        if (modifies_active_slot) {
            std::atomic_store(&active_configs_ptr_,
                              std::make_shared<const everest::config::ModuleConfigurations>(module_configs_));
        }
        publish_config_update(event);
    }

    return result;
}

GetConfigParametersResult ConfigServiceCore::get_config_parameters(
    int slot_id, const std::vector<everest::config::ConfigurationParameterIdentifier>& parameters) {
    return post_to_actor([this, slot_id, parameters]() { return internal_get_config_parameters(slot_id, parameters); });
}
GetConfigParametersResult ConfigServiceCore::internal_get_config_parameters(
    int slot_id, const std::vector<everest::config::ConfigurationParameterIdentifier>& parameters) {
    GetConfigurationResult get_cfg_result = internal_get_configuration(slot_id);

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
        [this, handler = std::move(handler)]() mutable { active_slot_handlers_.push_back(std::move(handler)); });
}

void ConfigServiceCore::register_config_update_handler(std::function<void(const ConfigurationUpdate&)> handler) {
    post_to_actor(
        [this, handler = std::move(handler)]() mutable { config_update_handlers_.push_back(std::move(handler)); });
}

void ConfigServiceCore::register_set_runtime_parameter_handler(const SetParamCallback& callback) {
    post_to_actor([this, callback]() { set_parameter_callback_ = callback; });
}

// --- Module state ---
void ConfigServiceCore::set_modules_running() {
    post_to_actor([this]() { internal_set_modules_running(); });
}
void ConfigServiceCore::internal_set_modules_running() {
    module_status_ = ActiveSlotStatus::Running;
    publish_active_slot_update();
}

void ConfigServiceCore::set_modules_stopped() {
    post_to_actor([this]() { internal_set_modules_stopped(); });
}
void ConfigServiceCore::internal_set_modules_stopped() {
    module_status_ = ActiveSlotStatus::Stopped;
    publish_active_slot_update();
}

void ConfigServiceCore::set_modules_starting() {
    post_to_actor([this]() { internal_set_modules_starting(); });
}
void ConfigServiceCore::internal_set_modules_starting() {
    module_status_ = ActiveSlotStatus::Starting;
    publish_active_slot_update();
}

void ConfigServiceCore::set_modules_stopping() {
    post_to_actor([this]() { internal_set_modules_stopping(); });
}
void ConfigServiceCore::internal_set_modules_stopping() {
    module_status_ = ActiveSlotStatus::Stopping;
    publish_active_slot_update();
}

void ConfigServiceCore::notice_cfg_validation_failed() {
    post_to_actor([this]() { internal_notice_cfg_validation_failed(); });
}
void ConfigServiceCore::internal_notice_cfg_validation_failed() {
    module_status_ = ActiveSlotStatus::FailedToStart;
    publish_active_slot_update();
}

void ConfigServiceCore::notice_module_restart_triggered() {
    post_to_actor([this]() { internal_notice_module_restart_triggered(); });
}
void ConfigServiceCore::internal_notice_module_restart_triggered() {
    module_status_ = ActiveSlotStatus::RestartTriggered;
    publish_active_slot_update();
}

} // namespace Everest::config
