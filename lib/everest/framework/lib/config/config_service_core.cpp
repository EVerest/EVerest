// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <utils/config/config_service_core.hpp>

#include <optional>

#include <date/tz.h>
#include <everest/logging.hpp>

#include <utils/config.hpp>
#include <utils/config/storage_sqlite.hpp>
#include <utils/config/types.hpp>
#include <utils/date.hpp>
#include <utils/yaml_loader.hpp>

namespace Everest::config {

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
    return it != config_access.modules.end() && it->second.allow_set_read_only;
}
} // namespace

ConfigServiceCore::ConfigServiceCore(everest::config::ModuleConfigurations initial_module_configs,
                                     const ConfigParseSettings& parse_settings,
                                     std::shared_ptr<everest::db::sqlite::ConnectionInterface> db_connection,
                                     std::optional<int> active_slot_id, std::function<StopModulesResult()> stop_fn,
                                     std::function<RestartModulesResult()> restart_fn) :
    module_configs_(std::move(initial_module_configs)),
    parse_settings_(parse_settings),
    slot_manager_(db_connection),
    db_(std::move(db_connection)),
    stop_fn_(std::move(stop_fn)),
    restart_fn_(std::move(restart_fn)) {
    int active_slot_id_from_db = slot_manager_.get_next_boot_slot_id();
    // use the next_boot_slot_id from the db as active_slot_id if none was provided
    if (active_slot_id.has_value()) {
        // Booting with provided active_slot_id
        active_slot_id_ = active_slot_id.value();
    } else {
        // No active_slot_id provided, using stored id from db
        active_slot_id_ = active_slot_id_from_db;
    }
    if (slot_manager_.is_valid(active_slot_id_)) {
        // TODO(CB): This is arguable - this is usually the reset-from-yaml scenario
        // should the config be stored in the currently active slot, or always the default slot?
        if (active_slot_id_ == active_slot_id_from_db) {
            slot_manager_.set_next_boot_slot_id(active_slot_id_);
        }
        active_storage_ = make_storage(active_slot_id_);
    } else {
        // TODO(CB): What do we do now? (in the end it should end up in a confiuration_API-only mode, IF this API is
        // active, otherwise the manager should quit)
    }
}

std::unique_ptr<everest::config::SqliteStorage> ConfigServiceCore::make_storage(int slot_id) {
    return std::make_unique<everest::config::SqliteStorage>(db_, slot_id);
}

void ConfigServiceCore::publish_active_slot_update(const ActiveSlotUpdate& update) {
    for (const auto& handler : active_slot_handlers_) {
        handler(update);
    }
}

void ConfigServiceCore::publish_config_update(const ConfigurationUpdate& update) {
    for (const auto& handler : config_update_handlers_) {
        handler(update);
    }
}

// --- Active-slot in-memory access ---

const everest::config::ModuleConfigurations& ConfigServiceCore::get_active_module_configurations() const {
    return module_configs_;
}

const everest::config::ModuleConfigurations& ConfigServiceCore::reload_from_storage() {
    const auto resp = active_storage_->get_module_configs();
    if (resp.status == everest::config::GenericResponseStatus::OK) {
        module_configs_ = resp.module_configs;
    }
    return module_configs_;
}

// --- Slot management ---

std::vector<SlotInfo> ConfigServiceCore::list_all_slots() {
    return slot_manager_.list_slots();
}

int ConfigServiceCore::get_active_slot_id() {
    return active_slot_id_;
}

SetActiveSlotStatus ConfigServiceCore::mark_active_slot(int slot_id) {
    if (slot_id == active_slot_id_) {
        return SetActiveSlotStatus::AlreadyActive;
    }
    const auto status = slot_manager_.set_next_boot_slot_id(slot_id);
    if (status != everest::config::GenericResponseStatus::OK) {
        return SetActiveSlotStatus::DoesNotExist;
    }
    publish_active_slot_update({now_rfc3339(), slot_id, ActiveSlotStatus::RestartTriggered});
    return SetActiveSlotStatus::Success;
}

DeleteSlotStatus ConfigServiceCore::delete_slot(int slot_id) {
    if (slot_id == active_slot_id_) {
        return DeleteSlotStatus::CannotDeleteActiveSlot;
    }
    // Verify slot exists first
    const auto slots = slot_manager_.list_slots();
    const bool exists = std::any_of(slots.begin(), slots.end(), [slot_id](const auto& s) { return s.id == slot_id; });
    if (!exists) {
        return DeleteSlotStatus::DoesNotExist;
    }
    const auto status = slot_manager_.delete_slot(slot_id);
    if (status != everest::config::GenericResponseStatus::OK) {
        return DeleteSlotStatus::DoesNotExist;
    }
    return DeleteSlotStatus::Success;
}

DuplicateSlotResult ConfigServiceCore::duplicate_slot(int slot_id, std::optional<std::string> description) {
    return slot_manager_.duplicate_slot(slot_id, description);
}

LoadFromYamlResult ConfigServiceCore::load_from_yaml(const std::string& raw_yaml,
                                                     std::optional<std::string> description,
                                                     std::optional<int> slot_id) {
    bool into_new_slot = not slot_id.has_value();
    int target_slot_id = into_new_slot ? slot_manager_.next_slot_id() : slot_id.value();

    if (target_slot_id == active_slot_id_) {
        return {false, std::nullopt, "Cannot load YAML into the active slot"};
    }
    if (!into_new_slot && !slot_manager_.exists(target_slot_id)) {
        return {false, std::nullopt, "The given slot ID does not exist"};
    }
    try {
        const auto json_config = Everest::load_yaml_from_string(raw_yaml);
        if (!json_config.contains("active_modules")) {
            return {false, std::nullopt, "YAML does not contain an 'active_modules' section"};
        }

        // Validate against manifests, interfaces and requirements; enriches configs with manifest metadata.
        const auto module_configs = Everest::validate_module_configs(parse_settings_, json_config);

        // If the slot doesn't exist, create it and write the config
        if (into_new_slot) {
            if (slot_manager_.write_config_slot(target_slot_id) != everest::config::GenericResponseStatus::OK) {
                return {false, std::nullopt, "Failed to create new config slot"};
            }

            auto storage = make_storage(target_slot_id);

            if (storage->write_module_configs(module_configs) != everest::config::GenericResponseStatus::OK) {
                slot_manager_.delete_slot(target_slot_id);
                return {false, std::nullopt, "Failed to write module configs to new slot"};
            }
            storage->mark_valid(true, nlohmann::json(module_configs).dump(), std::nullopt, description);
        } else {
            // If the slot exists, overwrite its config with the new one
            auto storage = make_storage(target_slot_id);

            if (storage->replace_module_configs(module_configs) != everest::config::GenericResponseStatus::OK) {
                // Do nothing - if writing to an existing slot failed, we don't want to delete it;
                return {false, std::nullopt, "Failed to write module configs to existing slot"};
            }
            storage->mark_valid(true, nlohmann::json(module_configs).dump(), std::nullopt, description);
        }

        return {true, target_slot_id, ""};
    } catch (const std::exception& e) {
        return {false, std::nullopt, std::string("Validation failed: ") + e.what()};
    }
}

// --- Slot-scoped configuration ---

GetConfigurationResult ConfigServiceCore::get_configuration(int slot_id) {
    const int resolved_slot_id = (slot_id == ConfigServiceInterface::ACTIVE_SLOT) ? active_slot_id_ : slot_id;
    if (resolved_slot_id == active_slot_id_) {
        return {GetConfigurationStatus::Success, module_configs_};
    }

    // Check slot exists
    const auto slots = slot_manager_.list_slots();
    const bool exists =
        std::any_of(slots.begin(), slots.end(), [resolved_slot_id](const auto& s) { return s.id == resolved_slot_id; });
    if (!exists) {
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
    SetConfigParameterResult result;
    result.status = SetConfigParameterStatus::Ok;

    const int resolved_slot_id = (slot_id == ConfigServiceInterface::ACTIVE_SLOT) ? active_slot_id_ : slot_id;

    // TODO(CB): remove from here, if access check remains on the mqtt_config_service level
    // // initialized with std::nullopt and set to value only if applicable
    // std::optional<everest::config::Access> access;

    // // if the request is from an internal module: test if the origin exists
    // if (!origin.external and origin.module_id.has_value()) {
    //     const auto origin_it = module_configs_.find(origin.module_id.value());
    //     if (origin_it == module_configs_.end()) {
    //         result.status = SetConfigParameterStatus::Error;
    //         result.status_info = fmt::format("Unknown origin module: {}", origin.module_id.value());
    //         return result;
    //     }

    //     access = origin_it->second.access;
    // }
    const std::string& origin_module_id = origin.module_id.value_or("<external>");

    result.parameter_results.emplace(
        updates.size(), Everest::config::SetConfigPerParameterResult{SetConfigParameterResultEnum::DoesNotExist, ""});

    ConfigurationUpdate event;
    event.timestamp = now_rfc3339();
    event.slot_id = resolved_slot_id;

    if (resolved_slot_id == active_slot_id_) {
        for (size_t i = 0; i < updates.size(); ++i) {
            const auto& update = updates[i];
            SetConfigParameterResultEnum& result_enum = result.parameter_results.value()[i].status;
            std::string& status_info = result.parameter_results.value()[i].status_info;

            // TODO(CB): remove from here, if access check remains on the mqtt_config_service level
            // if (access.has_value() and
            //     not write_access_allowed(access.value(), origin_module_id, update.identifier.module_id)) {
            //     result_enum = SetConfigParameterResultEnum::AccessDenied;
            //     result.parameter_results.value()[i].status_info = fmt::format(
            //         "Access to config item denied: {} cannot access {}", origin_module_id,
            //         update.identifier.module_id);
            //     return result;
            // }

            // does the parameter exist?
            auto parameter_lookup_result = get_parameter(update.identifier, module_configs_);
            if (not parameter_lookup_result.has_value()) {
                result.parameter_results.value()[i].status_info =
                    fmt::format("Unknown target module: {}", update.identifier.module_id);
                continue;
            }
            auto [parameter, parameter_access] = parameter_lookup_result.value();

            // if applicable: set in module and mutate in memory at the same time
            if (parameter->characteristics.mutability == everest::config::Mutability::ReadWrite and
                modules_are_running) {
                // If the callback is not defined (== cannot ask the modules), changes are rejected
                if (not set_parameter_callback_) {
                    result_enum = SetConfigParameterResultEnum::Rejected;
                    EVLOG_error << "ConfigServiceCore: No callback registered for setting the configuration parameter change in the module -> Rejecting.";
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
            } else if (parameter->characteristics.mutability == everest::config::Mutability::ReadOnly) {
                if (is_set_read_only_allowed(*parameter_access, update.identifier.module_id)) {
                    result_enum = SetConfigParameterResultEnum::WillApplyOnRestart;
                } else {
                    status_info = "Is a ReadOnly parameter";
                    result_enum = SetConfigParameterResultEnum::Rejected;
                }
            } else {
                // ReadOnly and WriteOnly don't go to the module at runtime
                // TODO(CB): What does WriteOnly mean !?
                result_enum = SetConfigParameterResultEnum::WillApplyOnRestart;
                // TODO(CB): Again, why not forward to the module?
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
                    EVLOG_error << "ConfigServiceCore: Couldn't persist a configuration parameter change which was accepted by the module.";
                }
            }
        }
    } else {
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

    if (!event.updates.empty()) {
        publish_config_update(event);
    }

    return result;
}

// --- Module lifecycle ---

StopModulesResult ConfigServiceCore::stop_modules() {
    if (stop_fn_) {
        return stop_fn_();
    }
    return StopModulesResult::Rejected;
}

RestartModulesResult ConfigServiceCore::restart_modules() {
    if (restart_fn_) {
        return restart_fn_();
    }
    return RestartModulesResult::Rejected;
}

// --- Push-event subscriptions ---

void ConfigServiceCore::register_active_slot_update_handler(std::function<void(const ActiveSlotUpdate&)> handler) {
    active_slot_handlers_.push_back(std::move(handler));
}

void ConfigServiceCore::register_config_update_handler(std::function<void(const ConfigurationUpdate&)> handler) {
    config_update_handlers_.push_back(std::move(handler));
}

void ConfigServiceCore::register_set_runtime_parameter_handler(const SetParamCallback& callback) {
    set_parameter_callback_ = callback;
}

} // namespace Everest::config
