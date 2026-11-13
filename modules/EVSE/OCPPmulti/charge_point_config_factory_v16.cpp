// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "charge_point_config_factory_v16.hpp"

#include <everest/ocpp_module_common/device_model/device_model_conflicts.hpp>
#include <everest/ocpp_module_common/device_model/everest_device_model.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <ocpp/common/utils.hpp>
#include <ocpp/v16/charge_point_configuration.hpp>
#include <ocpp/v16/charge_point_configuration_devicemodel.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>
#include <ocpp/v2/ocpp16_component_config_patcher.hpp>
#include <ocpp/v2/ocpp16_key_mappings.hpp>

namespace module::config_factory_v16 {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

void create_empty_user_config(const fs::path& user_config_path) {
    if (fs::exists(user_config_path.parent_path())) {
        std::ofstream fs(user_config_path.c_str());
        auto user_config = json::object();
        fs << user_config << std::endl;
        fs.close();
    } else {
        EVLOG_AND_THROW(
            std::runtime_error(fmt::format("Provided UserConfigPath is invalid: {}", user_config_path.string())));
    }
}

std::optional<fs::path> resolve_device_model_resource_path(const fs::path& ocpp_share_path,
                                                           const fs::path& configured_resource_path) {
    const auto resolved =
        configured_resource_path.is_absolute() ? configured_resource_path : ocpp_share_path / configured_resource_path;
    if (fs::exists(resolved)) {
        return resolved;
    }
    return std::nullopt;
}

struct DeviceModelInitializationContext {
    fs::path database_path;
    fs::path migration_files_path;
    fs::path component_config_path;
    std::optional<fs::path> custom_mappings_path;
    std::int32_t ocpp16_network_config_slot{1};
};

DeviceModelInitializationContext resolve_device_model_initialization_context(const fs::path& ocpp_share_path,
                                                                             const Ocpp16DeviceModelParams& config) {
    DeviceModelInitializationContext context;

    const auto configured_device_model_database_path = fs::path(config.DeviceModelDatabasePath);
    if (configured_device_model_database_path.is_absolute()) {
        context.database_path = configured_device_model_database_path;
    } else if (const auto resolved_existing =
                   resolve_device_model_resource_path(ocpp_share_path, configured_device_model_database_path);
               resolved_existing.has_value()) {
        context.database_path = resolved_existing.value();
    } else {
        context.database_path = ocpp_share_path / configured_device_model_database_path;
    }

    const auto migration_files_path =
        resolve_device_model_resource_path(ocpp_share_path, fs::path(config.DeviceModelDatabaseMigrationPath));
    if (!migration_files_path.has_value()) {
        EVLOG_AND_THROW(std::runtime_error(fmt::format("Could not locate device model migration files at '{}'.",
                                                       config.DeviceModelDatabaseMigrationPath)));
    }
    context.migration_files_path = migration_files_path.value();

    const auto component_config_path =
        resolve_device_model_resource_path(ocpp_share_path, fs::path(config.DeviceModelConfigPath));
    if (!component_config_path.has_value()) {
        EVLOG_AND_THROW(std::runtime_error(
            fmt::format("Could not locate device model component config at '{}'.", config.DeviceModelConfigPath)));
    }
    context.component_config_path = component_config_path.value();

    if (!config.Ocpp16KeyMappingsPath.empty()) {
        const auto custom_mappings_path =
            resolve_device_model_resource_path(ocpp_share_path, fs::path(config.Ocpp16KeyMappingsPath));
        if (!custom_mappings_path.has_value()) {
            EVLOG_AND_THROW(std::runtime_error(
                fmt::format("Could not locate OCPP 1.6 key mapping file at '{}'.", config.Ocpp16KeyMappingsPath)));
        }
        context.custom_mappings_path = custom_mappings_path.value();
    }

    context.ocpp16_network_config_slot = static_cast<int32_t>(config.Ocpp16NetworkConfigSlot);

    return context;
}

/// \brief Overrides the OCPP 1.6 NumberOfConnectors device-model value with the actual number of connected EVSEs.
void patch_number_of_connectors(
    std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>>& component_configs,
    std::int32_t n_evse) {
    constexpr auto legacy_ctrlr_name = "OCPP16LegacyCtrlr";
    constexpr auto number_of_connectors_var = "NumberOfConnectors";

    for (auto& [component_key, variables] : component_configs) {
        if (ocpp::iequals(component_key.name, legacy_ctrlr_name)) {
            for (auto& variable : variables) {
                if (ocpp::iequals(variable.name, number_of_connectors_var)) {
                    for (auto& attribute : variable.attributes) {
                        if (attribute.variable_attribute.type == ocpp::v2::AttributeEnum::Actual) {
                            attribute.value_source = "OCPP16Config";
                            attribute.variable_attribute.value = std::to_string(n_evse);
                        }
                    }
                    variable.default_actual_value = std::to_string(n_evse);
                    return;
                }
            }
        }
    }

    EVLOG_warning << "Could not find " << legacy_ctrlr_name << "/" << number_of_connectors_var
                  << " in component configs; NumberOfConnectors may not match the number of connected EVSEs.";
}

ocpp::v2::Ocpp16KeyMappings load_custom_mappings(const DeviceModelInitializationContext& context) {
    if (!context.custom_mappings_path.has_value()) {
        return {};
    }
    return ocpp::v2::load_ocpp16_key_mappings_from_yaml(context.custom_mappings_path.value());
}

void initialize_device_model_direct(const DeviceModelInitializationContext& context, std::int32_t n_evse) {
    EVLOG_info << "Updating device model database from component configs.";
    auto component_configs = ocpp::v2::get_all_component_configs(context.component_config_path);
    ocpp::v2::ensure_ocpp16_legacy_ctrlr(component_configs);
    ocpp::v2::remove_overridden_ocpp16_legacy_variables(component_configs, load_custom_mappings(context));
    patch_number_of_connectors(component_configs, n_evse);
    ocpp::v2::InitDeviceModelDb init_device_model_db(context.database_path, context.migration_files_path);
    init_device_model_db.initialize_database(component_configs, false);
}

void initialize_device_model_with_migration(const fs::path& ocpp_share_path, const fs::path& user_config_path,
                                            const std::string& charge_point_config_json,
                                            const DeviceModelInitializationContext& context, std::int32_t n_evse) {
    if (ocpp::v2::InitDeviceModelDb(context.database_path, context.migration_files_path).is_db_initialized()) {
        EVLOG_info << "Device model database already initialized. Skipping migration, updating from component configs.";
        initialize_device_model_direct(context, n_evse);
        return;
    }

    EVLOG_info << "Starting one-time OCPP1.6 configuration migration to device model database.";

    const auto legacy_charge_point_config = std::make_unique<ocpp::v16::ChargePointConfiguration>(
        charge_point_config_json, ocpp_share_path, user_config_path);

    auto component_configs = ocpp::v2::get_all_component_configs(context.component_config_path);
    ocpp::v2::ensure_ocpp16_legacy_ctrlr(component_configs);

    if (context.custom_mappings_path.has_value()) {
        EVLOG_info << "Custom OCPP 1.6 key mappings provided, loading from " << context.custom_mappings_path.value();
    }
    const auto custom_mappings = load_custom_mappings(context);

    ocpp::v2::patch_component_config_with_ocpp16(component_configs, *legacy_charge_point_config, custom_mappings,
                                                 context.ocpp16_network_config_slot);
    ocpp::v2::remove_overridden_ocpp16_legacy_variables(component_configs, custom_mappings);

    ocpp::v2::InitDeviceModelDb init_device_model_db(context.database_path, context.migration_files_path);
    init_device_model_db.initialize_database(component_configs, false);

    EVLOG_info << "Successfully migrated OCPP1.6 configuration to device model database at " << context.database_path;
}

config_factory_result_t create_device_model_charge_point_configuration(
    const fs::path& ocpp_share_path, const DeviceModelInitializationContext& context,
    const fs::path& everest_device_model_database_path,
    ocpp_module_common::device_model::EverestDeviceModel* everest_device_model,
    ocpp_module_common::device_model::ConfigServiceAccess* config_service,
    ocpp_module_common::device_model::ExternalOwners* external_owners, const std::int32_t n_evse,
    const bool fail_on_device_model_conflicts) {
    // The database is pre-seeded by initialize_device_model_* above, so the 1-arg ctor is correct here.
    auto device_model_storage = std::make_unique<ocpp::v2::DeviceModelStorageSqlite>(context.database_path);
    ocpp_module_common::device_model::migrate_everest_device_model_database(
        everest_device_model_database_path, context.database_path, *device_model_storage);
    auto device_model = std::make_unique<ocpp::v2::DeviceModel>(std::move(device_model_storage));

    // One-time migration, mirroring the v2 path (ocpp::v2::ChargePoint::initialize)
    ocpp::v2::NetworkConfigurationComponentVariables::migrate_from_blob_if_needed(*device_model);

    auto custom_mappings = load_custom_mappings(context);

    // Register only once the configuration owns the device model, so the registrations end before it is destroyed
    auto& registry = *device_model;
    // Pass a copy so the returned mappings match the ones the configuration runs with.
    auto configuration = std::make_unique<ocpp::v16::ChargePointConfigurationDeviceModel>(
        ocpp_share_path.string(), std::move(device_model), custom_mappings);

    ocpp_module_common::device_model::DeviceModelRegistrations registrations;
    if (everest_device_model != nullptr) {
        registrations.register_everest_device_model(*everest_device_model, registry);
    }
    if (config_service != nullptr) {
        registrations.register_config_service(*config_service, registry);
    }
    if (external_owners != nullptr) {
        registrations.register_external_owners(*external_owners, registry);
    }
    // Also removes the monitors and stored values of variables registered in OCPP 2.x mode only, such as DER
    registry.remove_monitors_and_values_of_absent_variables();
    ocpp_module_common::device_model::check_device_model_conflicts(registrations.results(),
                                                                   fail_on_device_model_conflicts);
    configuration->check_integrity(n_evse);

    return {std::move(configuration), std::move(custom_mappings), std::move(registrations)};
}

/// \brief Loads the legacy OCPP 1.6 JSON config used as the one-time device-model migration source.
///
/// Requires \p configured_config_path to exist, applies \p n_evse to the connector count, and overlays the
/// user config (creating an empty one if missing).
std::string load_charge_point_config_json(const fs::path& configured_config_path, const fs::path& user_config_path,
                                          std::int32_t n_evse) {
    if (!fs::exists(configured_config_path)) {
        EVLOG_AND_THROW(Everest::EverestConfigError(
            fmt::format("ChargePointConfigPath '{}' does not exist (required as the OCPP1.6 device model migration "
                        "source)",
                        configured_config_path.string())));
    }

    EVLOG_info << "OCPP config: " << configured_config_path.string();
    EVLOG_info << "OCPP user config: " << user_config_path.string();

    std::ifstream ifs(configured_config_path.c_str());
    std::string config_file((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
    auto json_config = json::parse(config_file);
    json_config.at("Core").at("NumberOfConnectors") = n_evse;

    if (fs::exists(user_config_path)) {
        std::ifstream uifs(user_config_path.c_str());
        std::string user_config_file((std::istreambuf_iterator<char>(uifs)), (std::istreambuf_iterator<char>()));
        try {
            const auto user_config = json::parse(user_config_file);
            EVLOG_info << "Augmenting chargepoint config with user_config entries";
            json_config.merge_patch(user_config);
        } catch (const json::parse_error& e) {
            EVLOG_error << "Error while parsing user config file.";
            EVLOG_AND_THROW(e);
        }
    } else {
        EVLOG_debug << "No user-config provided. Creating user config file";
        create_empty_user_config(user_config_path);
    }
    return json_config.dump();
}

} // namespace

config_factory_result_t
create_charge_point_configuration(const fs::path& ocpp_share_path, const Ocpp16DeviceModelParams& config,
                                  std::int32_t n_evse, const fs::path& everest_device_model_database_path,
                                  ocpp_module_common::device_model::EverestDeviceModel* everest_device_model,
                                  ocpp_module_common::device_model::ConfigServiceAccess* config_service,
                                  ocpp_module_common::device_model::ExternalOwners* external_owners) {
    const auto context = resolve_device_model_initialization_context(ocpp_share_path, config);

    const bool db_initialized =
        ocpp::v2::InitDeviceModelDb(context.database_path, context.migration_files_path).is_db_initialized();

    if (config.EnableLegacyConfigMigration && !db_initialized) {
        // One-time migration: seed a fresh device model database from the legacy OCPP 1.6 JSON config.
        auto configured_config_path = fs::path(config.ChargePointConfigPath);
        if (!configured_config_path.is_absolute()) {
            configured_config_path = ocpp_share_path / configured_config_path;
        }
        auto user_config_path = fs::path(config.UserConfigPath);
        if (!user_config_path.is_absolute()) {
            user_config_path = ocpp_share_path / user_config_path;
        }

        const auto charge_point_config_json =
            load_charge_point_config_json(configured_config_path, user_config_path, n_evse);
        initialize_device_model_with_migration(ocpp_share_path, user_config_path, charge_point_config_json, context,
                                               n_evse);
    } else {
        if (config.EnableLegacyConfigMigration) {
            EVLOG_info << "Legacy OCPP1.6 config migration is enabled but the device model database is already "
                          "initialized; skipping the one-time migration.";
        }
        // No migration: bootstrap/refresh the device model directly from the component configs.
        initialize_device_model_direct(context, n_evse);
    }

    return create_device_model_charge_point_configuration(ocpp_share_path, context, everest_device_model_database_path,
                                                          everest_device_model, config_service, external_owners, n_evse,
                                                          config.FailOnDeviceModelConflicts);
}

} // namespace module::config_factory_v16
