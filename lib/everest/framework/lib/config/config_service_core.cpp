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
#include <everest/utils/yaml_loader.hpp>

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

ConfigServiceCore::ConfigServiceCore(const ConfigParseSettings& parse_settings,
                                     std::shared_ptr<everest::db::sqlite::ConnectionInterface> db_connection) :
    parse_settings_(parse_settings), slot_manager_(db_connection), db_(std::move(db_connection)) {
    active_slot_id_ = everest::config::SqliteStorage::DEFAULT_CONFIG_ID;

    reinitialize_from_db(true);
}

void ConfigServiceCore::reinitialize_from_db(bool force_reload) {
    if (module_status_ != ActiveSlotStatus::Stopped) {
        return;
    }
    int new_active_slot_id = slot_manager_.get_next_boot_slot_id();
    if ((new_active_slot_id != active_slot_id_) or force_reload) {
        active_slot_id_ = new_active_slot_id;
        active_storage_ = make_storage(active_slot_id_);
    }
    // always reload module configuration in order to include possible WillApplyOnRestart changes
    reload_from_storage();
    if (new_active_slot_id != active_slot_id_) {
        publish_active_slot_update();
    }
}

std::unique_ptr<everest::config::SqliteStorage> ConfigServiceCore::make_storage(int slot_id) {
    return std::make_unique<everest::config::SqliteStorage>(db_, slot_id);
}

void ConfigServiceCore::publish_active_slot_update() {
    const ActiveSlotUpdate update{now_rfc3339(), active_slot_id_, slot_manager_.get_next_boot_slot_id(),
                                  module_status_};
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

void ConfigServiceCore::reload_from_storage() {
    if (slot_manager_.exists(active_slot_id_)) {
        const auto resp = active_storage_->get_module_configs();
        if (resp.status == everest::config::GenericResponseStatus::OK) {
            module_configs_ = resp.module_configs;
        }
    }
}

// --- Slot management ---

std::vector<SlotInfo> ConfigServiceCore::list_all_slots() {
    return slot_manager_.list_slots();
}

int ConfigServiceCore::get_active_slot_id() {
    return active_slot_id_;
}

int ConfigServiceCore::get_next_boot_slot_id() {
    return slot_manager_.get_next_boot_slot_id();
}

SetActiveSlotStatus ConfigServiceCore::mark_active_slot(int slot_id) {
    int next_boot_slot_id = slot_manager_.get_next_boot_slot_id();
    if (slot_id == next_boot_slot_id) {
        return SetActiveSlotStatus::NoChangeRequired;
    }
    const auto status = slot_manager_.set_next_boot_slot_id(slot_id);
    if (status != everest::config::GenericResponseStatus::OK) {
        return SetActiveSlotStatus::DoesNotExist;
    }
    publish_active_slot_update();
    return SetActiveSlotStatus::Success;
}

DeleteSlotStatus ConfigServiceCore::delete_slot(int slot_id) {
    if (slot_id == active_slot_id_ or slot_id == slot_manager_.get_next_boot_slot_id()) {
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
    int target_slot_id = slot_id.value_or(slot_manager_.next_slot_id());

    if (target_slot_id == active_slot_id_ and module_status_ != ActiveSlotStatus::Stopped) {
        return {false, std::nullopt, "Cannot load YAML into the active slot"};
    }
    bool into_new_slot = not slot_manager_.exists(target_slot_id);
    try {
        const auto json_config = Everest::load_yaml_from_string(raw_yaml);
        if (!json_config.contains("active_modules")) {
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
            // If the slot exists, overwrite its config with the new one
            auto storage = make_storage(target_slot_id);

            if (storage->replace_module_configs(module_configs) != everest::config::GenericResponseStatus::OK) {
                slot_manager_.delete_slot(target_slot_id);
                // Do nothing - if writing to an existing slot failed, we don't want to delete it;
                return {false, std::nullopt, "Failed to write module configs to existing slot"};
            }
        }

        return {true, target_slot_id, ""};
    } catch (const std::exception& e) {
        return {false, std::nullopt, std::string("Validation failed: ") + e.what()};
    }
}

bool ConfigServiceCore::set_description(int slot_id, const std::string& description) {
    if (slot_manager_.exists(slot_id)) {
        auto slots = slot_manager_.list_slots();

        slot_manager_.update_description(slot_id, {description});
        return true;
    } else {
        return false;
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
                        fmt::format("Unknown parameter: {} in module: {}", update.identifier.configuration_parameter_name, update.identifier.module_id);
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
                if (inactive_configuration.module_configs.find(update.identifier.module_id) == inactive_configuration.module_configs.end()) {
                    result.parameter_results.value()[i].status_info =
                        fmt::format("Unknown target module: {}", update.identifier.module_id);
                } else {
                    result.parameter_results.value()[i].status_info =
                        fmt::format("Unknown parameter: {} in module: {}", update.identifier.configuration_parameter_name, update.identifier.module_id);
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

    if (!event.updates.empty()) {
        publish_config_update(event);
    }

    return result;
}

GetConfigParametersResult ConfigServiceCore::get_config_parameters(
    int slot_id, const std::vector<everest::config::ConfigurationParameterIdentifier>& parameters) {
    GetConfigurationResult get_cfg_result = get_configuration(slot_id);

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
    active_slot_handlers_.push_back(std::move(handler));
}

void ConfigServiceCore::register_config_update_handler(std::function<void(const ConfigurationUpdate&)> handler) {
    config_update_handlers_.push_back(std::move(handler));
}

void ConfigServiceCore::register_set_runtime_parameter_handler(const SetParamCallback& callback) {
    set_parameter_callback_ = callback;
}

// --- Module state ---
void ConfigServiceCore::set_modules_running() {
    module_status_ = ActiveSlotStatus::Running;
    publish_active_slot_update();
}

void ConfigServiceCore::set_modules_stopped() {
    module_status_ = ActiveSlotStatus::Stopped;
    publish_active_slot_update();
}

void ConfigServiceCore::set_modules_starting() {
    module_status_ = ActiveSlotStatus::Starting;
    publish_active_slot_update();
}

void ConfigServiceCore::set_modules_stopping() {
    module_status_ = ActiveSlotStatus::Stopping;
    publish_active_slot_update();
}

void ConfigServiceCore::notice_cfg_validation_failed() {
    module_status_ = ActiveSlotStatus::FailedToStart;
    publish_active_slot_update();
}

void ConfigServiceCore::notice_module_restart_triggered() {
    module_status_ = ActiveSlotStatus::RestartTriggered;
    publish_active_slot_update();
}

} // namespace Everest::config
