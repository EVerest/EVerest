// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <utils/config/config_service_core.hpp>

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
} // namespace

ConfigServiceCore::ConfigServiceCore(std::shared_ptr<ManagerConfig> active_config,
                                     const ConfigParseSettings& parse_settings, std::filesystem::path db_path,
                                     std::filesystem::path migrations_dir, int active_slot_id,
                                     std::function<StopModulesResult()> stop_fn,
                                     std::function<RestartModulesResult()> restart_fn) :
    active_config_(std::move(active_config)),
    parse_settings_(parse_settings),
    slot_manager_(db_path, migrations_dir),
    db_path_(db_path),
    migrations_dir_(migrations_dir),
    active_slot_id_(active_slot_id),
    stop_fn_(std::move(stop_fn)),
    restart_fn_(std::move(restart_fn)),
    active_storage_(make_storage(active_slot_id)) {
}

std::unique_ptr<everest::config::SqliteStorage> ConfigServiceCore::make_storage(int slot_id) {
    return std::make_unique<everest::config::SqliteStorage>(db_path_, migrations_dir_, slot_id);
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

// --- Slot management ---

std::vector<SlotInfo> ConfigServiceCore::list_all_slots() {
    const auto stored = slot_manager_.list_slots();
    std::vector<SlotInfo> result;
    result.reserve(stored.size());
    for (const auto& s : stored) {
        SlotInfo info;
        info.slot_id = s.id;
        info.last_updated = s.last_updated;
        info.is_valid = s.is_valid;
        info.description = s.description;
        info.config_file_path = s.config_file_path;
        result.push_back(std::move(info));
    }
    return result;
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

LoadFromYamlResult ConfigServiceCore::load_from_yaml(const std::string& raw_yaml) {
    try {
        const auto json_config = Everest::load_yaml_from_string(raw_yaml);
        if (!json_config.contains("active_modules")) {
            return {false, std::nullopt, "YAML does not contain an 'active_modules' section"};
        }

        // Validate against manifests, interfaces and requirements; enriches configs with manifest metadata.
        const auto module_configs = Everest::validate_module_configs(parse_settings_, json_config);

        const int new_slot_id = slot_manager_.next_slot_id();
        if (slot_manager_.write_config_slot(new_slot_id) != everest::config::GenericResponseStatus::OK) {
            return {false, std::nullopt, "Failed to create new config slot"};
        }

        auto storage = make_storage(new_slot_id);
        if (storage->write_module_configs(module_configs) != everest::config::GenericResponseStatus::OK) {
            slot_manager_.delete_slot(new_slot_id);
            return {false, std::nullopt, "Failed to write module configs to new slot"};
        }
        storage->mark_valid(true, nlohmann::json(module_configs).dump(), std::nullopt);

        return {true, new_slot_id, ""};
    } catch (const std::exception& e) {
        return {false, std::nullopt, std::string("Validation failed: ") + e.what()};
    }
}

// --- Slot-scoped configuration ---

GetConfigurationResult ConfigServiceCore::get_configuration(int slot_id) {
    const int resolved_slot_id = (slot_id == ConfigServiceInterface::ACTIVE_SLOT) ? active_slot_id_ : slot_id;
    if (resolved_slot_id == active_slot_id_) {
        return {GetConfigurationStatus::Success, active_config_->get_module_configurations()};
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

    ConfigurationUpdate event;
    event.timestamp = now_rfc3339();
    event.slot_id = resolved_slot_id;

    if (resolved_slot_id == active_slot_id_) {
        for (const auto& update : updates) {
            const auto& module_configurations = active_config_->get_module_configurations();
            const auto mod_it = module_configurations.find(update.identifier.module_id);
            if (mod_it == module_configurations.end()) {
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

            // Persist to storage first.
            const auto write_status =
                active_storage_->write_configuration_parameter(update.identifier, *characteristics, update.value);
            if (write_status != everest::config::GetSetResponseStatus::OK) {
                results.push_back(SetConfigParameterResult::Rejected);
                continue;
            }

            SetConfigParameterResult result;
            if (update.immediately_applied) {
                // Module confirmed it immediately applied the value. We mutate live module_configs so
                // get_config_value() reflects the new runtime state immediately.
                const auto parsed_value = everest::config::parse_config_value(characteristics->datatype, update.value);
                active_config_->update_config_value(update.identifier, parsed_value);
                result = SetConfigParameterResult::Applied;
            } else {
                // Value stored for next boot; module_configs intentionally remains at boot-time state.
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
                results.push_back(SetConfigParameterResult::Applied);
                event.updates.push_back({update.identifier, update.value, SetConfigParameterResult::Applied});
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

// --- Module lifecycle ---

StopModulesResult ConfigServiceCore::stop_modules() {
    if (stop_fn_) {
        return stop_fn_();
    }
    return StopModulesResult::NotAllowed;
}

RestartModulesResult ConfigServiceCore::restart_modules() {
    if (restart_fn_) {
        return restart_fn_();
    }
    return RestartModulesResult::NotAllowed;
}

// --- Push-event subscriptions ---

void ConfigServiceCore::register_active_slot_update_handler(std::function<void(const ActiveSlotUpdate&)> handler) {
    active_slot_handlers_.push_back(std::move(handler));
}

void ConfigServiceCore::register_config_update_handler(std::function<void(const ConfigurationUpdate&)> handler) {
    config_update_handlers_.push_back(std::move(handler));
}

} // namespace Everest::config
