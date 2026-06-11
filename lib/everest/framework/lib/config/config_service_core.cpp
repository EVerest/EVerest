// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <utils/config/config_service_core.hpp>

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

    if (target_slot_id == active_slot_id_) {
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

std::vector<SetConfigParameterResult>
ConfigServiceCore::set_config_parameters(int slot_id, const std::vector<ConfigParameterUpdate>& updates) {
    std::vector<SetConfigParameterResult> results;
    results.reserve(updates.size());

    const int resolved_slot_id = (slot_id == ConfigServiceInterface::ACTIVE_SLOT) ? active_slot_id_ : slot_id;
    const bool modifies_active_slot = resolved_slot_id == active_slot_id_;

    ConfigurationUpdate event;
    event.timestamp = now_rfc3339();
    event.slot_id = resolved_slot_id;

    if (resolved_slot_id == active_slot_id_) {
        for (const auto& update : updates) {
            const auto mod_it = module_configs_.find(update.identifier.module_id);
            if (mod_it == module_configs_.end()) {
                results.push_back(SetConfigParameterResult::DoesNotExist);
                continue;
            }

            const auto impl_id = update.identifier.module_implementation_id.value_or("!module");
            auto params_it = mod_it->second.configuration_parameters.find(impl_id);
            if (params_it == mod_it->second.configuration_parameters.end()) {
                results.push_back(SetConfigParameterResult::DoesNotExist);
                continue;
            }

            std::optional<everest::config::ConfigurationParameterCharacteristics> characteristics;
            for (const auto& param : params_it->second) {
                if (param.name == update.identifier.configuration_parameter_name) {
                    characteristics = param.characteristics;
                    break;
                }
            }
            if (!characteristics.has_value()) {
                results.push_back(SetConfigParameterResult::DoesNotExist);
                continue;
            }

            // Persist to storage first.
            const auto write_status =
                active_storage_->write_configuration_parameter(update.identifier, *characteristics, update.value);
            if (write_status != everest::config::GetSetResponseStatus::OK) {
                results.push_back(SetConfigParameterResult::Rejected);
                continue;
            }

            SetConfigParameterResult result;
            if (update.immediately_applied) {
                // Module confirmed it immediately applied the value; mutate the in-memory state
                // so module_configs_ reflect the new runtime value.
                const auto parsed_value = everest::config::parse_config_value(characteristics->datatype, update.value);
                for (auto& param : params_it->second) {
                    if (param.name == update.identifier.configuration_parameter_name) {
                        param.value = parsed_value;
                        break;
                    }
                }
                result = SetConfigParameterResult::Applied;
            } else {
                // Value stored for next boot; module_configs_ intentionally maintains current state
                result = SetConfigParameterResult::WillApplyOnRestart;
            }
            results.push_back(result);
            event.updates.push_back({update.identifier, update.value, result});
        }
    } else {
        // Non-active slot: write directly to storage
        auto storage = make_storage(resolved_slot_id);
        const auto existing = storage->get_module_configs();

        for (const auto& update : updates) {
            // Look up characteristics from the stored config
            if (existing.status != everest::config::GenericResponseStatus::OK) {
                results.push_back(SetConfigParameterResult::Rejected);
                continue;
            }
            const auto mod_it = existing.module_configs.find(update.identifier.module_id);
            if (mod_it == existing.module_configs.end()) {
                results.push_back(SetConfigParameterResult::DoesNotExist);
                continue;
            }
            const auto impl_id = update.identifier.module_implementation_id.value_or("!module");
            const auto params_it = mod_it->second.configuration_parameters.find(impl_id);
            if (params_it == mod_it->second.configuration_parameters.end()) {
                results.push_back(SetConfigParameterResult::DoesNotExist);
                continue;
            }
            std::optional<everest::config::ConfigurationParameterCharacteristics> characteristics;
            for (const auto& param : params_it->second) {
                if (param.name == update.identifier.configuration_parameter_name) {
                    characteristics = param.characteristics;
                    break;
                }
            }
            if (!characteristics.has_value()) {
                results.push_back(SetConfigParameterResult::DoesNotExist);
                continue;
            }

            const auto write_status =
                storage->write_configuration_parameter(update.identifier, *characteristics, update.value);
            if (write_status == everest::config::GetSetResponseStatus::OK) {
                // For non-active slots results are not applied immediately, so we return WillApplyOnRestart
                results.push_back(SetConfigParameterResult::WillApplyOnRestart);
                event.updates.push_back(
                    {update.identifier, update.value, SetConfigParameterResult::WillApplyOnRestart});
            } else {
                results.push_back(SetConfigParameterResult::Rejected);
            }
        }
    }

    if (!event.updates.empty()) {
        publish_config_update(event);
    }

    return results;
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
