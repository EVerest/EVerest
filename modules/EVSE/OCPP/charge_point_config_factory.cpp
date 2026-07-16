// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "charge_point_config_factory.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include <ocpp/v16/charge_point_configuration.hpp>
#include <ocpp/v16/charge_point_configuration_devicemodel.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>
#include <ocpp/v2/ocpp16_component_config_patcher.hpp>
#include <ocpp/v2/ocpp16_custom_config_mappings.hpp>

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
    int32_t ocpp16_network_config_slot{1};
};

DeviceModelInitializationContext resolve_device_model_initialization_context(const fs::path& ocpp_share_path,
                                                                             const module::Conf& config) {
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

    if (!config.DeviceModelConfigMappings.empty()) {
        const auto custom_mappings_path =
            resolve_device_model_resource_path(ocpp_share_path, fs::path(config.DeviceModelConfigMappings));
        if (!custom_mappings_path.has_value()) {
            EVLOG_AND_THROW(std::runtime_error(fmt::format(
                "Could not locate OCPP1.6 custom config mapping file at '{}'.", config.DeviceModelConfigMappings)));
        }
        context.custom_mappings_path = custom_mappings_path.value();
    }

    context.ocpp16_network_config_slot = static_cast<int32_t>(config.Ocpp16NetworkConfigSlot);

    return context;
}

enum class ConfigBackend {
    LegacyJSON,
    DeviceModelDirect,
    DeviceModelWithMigration
};

struct ChargePointConfigStrategy {
    ConfigBackend backend;
    bool json_fallback_enabled;
    fs::path charge_point_config_path;
    fs::path user_config_path;
    std::optional<DeviceModelInitializationContext> dm_context;
};

ChargePointConfigStrategy validate_and_derive_strategy(const fs::path& ocpp_share_path, const module::Conf& config) {
    ChargePointConfigStrategy strategy;

    if (config.ConfigBackend == "legacy") {
        strategy.backend = ConfigBackend::LegacyJSON;
    } else if (config.ConfigBackend == "device_model") {
        strategy.backend = ConfigBackend::DeviceModelDirect;
    } else if (config.ConfigBackend == "device_model_with_migration") {
        strategy.backend = ConfigBackend::DeviceModelWithMigration;
    } else {
        EVLOG_AND_THROW(
            std::runtime_error(fmt::format("Unknown ConfigBackend '{}'. Valid values: \"legacy\", \"device_model\", "
                                           "\"device_model_with_migration\".",
                                           config.ConfigBackend)));
    }

    strategy.json_fallback_enabled = config.EnableDeviceModelFallbackToLegacyJson;

    if (strategy.backend == ConfigBackend::LegacyJSON && strategy.json_fallback_enabled) {
        EVLOG_warning
            << "EnableDeviceModelFallbackToLegacyJson has no effect when ConfigBackend is \"legacy\", ignoring.";
        strategy.json_fallback_enabled = false;
    }

    const bool needs_json_config = strategy.backend == ConfigBackend::LegacyJSON ||
                                   strategy.backend == ConfigBackend::DeviceModelWithMigration ||
                                   strategy.json_fallback_enabled;
    auto configured_config_path = fs::path(config.ChargePointConfigPath);
    if (!configured_config_path.is_absolute()) {
        configured_config_path = ocpp_share_path / configured_config_path;
    }
    if (needs_json_config && !fs::exists(configured_config_path)) {
        EVLOG_AND_THROW(Everest::EverestConfigError(
            fmt::format("ChargePointConfigPath '{}' does not exist (required for ConfigBackend \"{}\"{})",
                        configured_config_path.string(), config.ConfigBackend,
                        strategy.json_fallback_enabled ? " with fallback enabled" : "")));
    }
    strategy.charge_point_config_path = configured_config_path;

    auto configured_user_config_path = fs::path(config.UserConfigPath);
    if (!configured_user_config_path.is_absolute()) {
        configured_user_config_path = ocpp_share_path / configured_user_config_path;
    }
    strategy.user_config_path = configured_user_config_path;

    if (strategy.backend != ConfigBackend::LegacyJSON) {
        strategy.dm_context = resolve_device_model_initialization_context(ocpp_share_path, config);
    }

    return strategy;
}

void initialize_device_model_direct(const DeviceModelInitializationContext& context) {
    EVLOG_info << "Updating device model database from component configs.";
    auto component_configs = ocpp::v2::get_all_component_configs(context.component_config_path);
    ocpp::v2::InitDeviceModelDb init_device_model_db(context.database_path, context.migration_files_path);
    init_device_model_db.initialize_database(component_configs, false);
}

void initialize_device_model_with_migration(const fs::path& ocpp_share_path, const fs::path& user_config_path,
                                            const std::string& charge_point_config_json,
                                            const DeviceModelInitializationContext& context) {
    if (ocpp::v2::InitDeviceModelDb(context.database_path, context.migration_files_path).is_db_initialized()) {
        EVLOG_info << "Device model database already initialized. Skipping migration, updating from component configs.";
        initialize_device_model_direct(context);
        return;
    }

    EVLOG_info << "Starting one-time OCPP1.6 configuration migration to device model database.";

    const auto legacy_charge_point_config = std::make_unique<ocpp::v16::ChargePointConfiguration>(
        charge_point_config_json, ocpp_share_path, user_config_path);

    auto component_configs = ocpp::v2::get_all_component_configs(context.component_config_path);
    ocpp::v2::ensure_ocpp16_legacy_ctrlr(component_configs);

    ocpp::v2::Ocpp16CustomConfigMappings custom_mappings;
    if (context.custom_mappings_path.has_value()) {
        EVLOG_info << "Custom OCPP1.6 to device model config mappings provided, loading from "
                   << context.custom_mappings_path.value();
        custom_mappings = ocpp::v2::load_ocpp16_custom_config_mappings_from_yaml(context.custom_mappings_path.value());
    }

    ocpp::v2::patch_component_config_with_ocpp16(component_configs, *legacy_charge_point_config, custom_mappings,
                                                 context.ocpp16_network_config_slot);

    ocpp::v2::InitDeviceModelDb init_device_model_db(context.database_path, context.migration_files_path);
    init_device_model_db.initialize_database(component_configs, false);

    EVLOG_info << "Successfully migrated OCPP1.6 configuration to device model database at " << context.database_path;
}

std::unique_ptr<ocpp::v16::ChargePointConfigurationInterface>
create_device_model_charge_point_configuration(const fs::path& ocpp_share_path,
                                               const DeviceModelInitializationContext& context) {
    auto device_model_storage = std::make_unique<ocpp::v2::DeviceModelStorageSqlite>(context.database_path);
    auto device_model = std::make_unique<ocpp::v2::DeviceModel>(std::move(device_model_storage));

    ocpp::v2::Ocpp16CustomConfigMappings custom_mappings;
    if (context.custom_mappings_path.has_value()) {
        custom_mappings = ocpp::v2::load_ocpp16_custom_config_mappings_from_yaml(context.custom_mappings_path.value());
    }

    return std::make_unique<ocpp::v16::ChargePointConfigurationDeviceModel>(
        ocpp_share_path.string(), std::move(device_model), std::move(custom_mappings));
}

} // namespace

namespace module {

std::unique_ptr<ocpp::v16::ChargePointConfigurationInterface>
create_charge_point_configuration(const fs::path& ocpp_share_path, const Conf& config, int32_t n_evse) {
    const auto strategy = validate_and_derive_strategy(ocpp_share_path, config);

    const auto load_charge_point_config_json = [&]() -> std::string {
        EVLOG_info << "OCPP config: " << strategy.charge_point_config_path.string();
        EVLOG_info << "OCPP user config: " << strategy.user_config_path.string();

        std::ifstream ifs(strategy.charge_point_config_path.c_str());
        std::string config_file((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
        auto json_config = json::parse(config_file);
        json_config.at("Core").at("NumberOfConnectors") = n_evse;

        if (fs::exists(strategy.user_config_path)) {
            std::ifstream uifs(strategy.user_config_path.c_str());
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
            create_empty_user_config(strategy.user_config_path);
        }
        return json_config.dump();
    };

    const auto make_json_backend = [&](const std::string& charge_point_config_json) {
        return std::make_unique<ocpp::v16::ChargePointConfiguration>(charge_point_config_json, ocpp_share_path,
                                                                     strategy.user_config_path);
    };

    const auto try_init_device_model = [&](const std::optional<std::string>& charge_point_config_json)
        -> std::unique_ptr<ocpp::v16::ChargePointConfigurationInterface> {
        const auto& ctx = strategy.dm_context.value();
        switch (strategy.backend) {
        case ConfigBackend::LegacyJSON:
            break;
        case ConfigBackend::DeviceModelDirect:
            initialize_device_model_direct(ctx);
            break;
        case ConfigBackend::DeviceModelWithMigration:
            initialize_device_model_with_migration(ocpp_share_path, strategy.user_config_path,
                                                   charge_point_config_json.value(), ctx);
            break;
        }
        auto dm_config = create_device_model_charge_point_configuration(ocpp_share_path, ctx);
        dm_config->check_integrity(n_evse);
        return dm_config;
    };

    std::unique_ptr<ocpp::v16::ChargePointConfigurationInterface> charge_point_config;

    switch (strategy.backend) {
    case ConfigBackend::LegacyJSON: {
        EVLOG_info << "Using legacy JSON config backend.";
        const auto charge_point_config_json = load_charge_point_config_json();
        charge_point_config = make_json_backend(charge_point_config_json);
        break;
    }
    case ConfigBackend::DeviceModelDirect:
    case ConfigBackend::DeviceModelWithMigration: {
        const bool needs_json =
            strategy.backend == ConfigBackend::DeviceModelWithMigration || strategy.json_fallback_enabled;
        const std::optional<std::string> charge_point_config_json =
            needs_json ? std::make_optional(load_charge_point_config_json()) : std::nullopt;

        const auto& ctx = strategy.dm_context.value();
        const bool db_existed_before = fs::exists(ctx.database_path);

        if (strategy.json_fallback_enabled) {
            try {
                charge_point_config = try_init_device_model(charge_point_config_json);
            } catch (const std::exception& e) {
                EVLOG_error << "Device model initialization or integrity check failed: " << e.what()
                            << ". Falling back to JSON config.";
                if (!db_existed_before && fs::exists(ctx.database_path)) {
                    fs::remove(ctx.database_path);
                }
                charge_point_config = make_json_backend(charge_point_config_json.value());
            }
        } else {
            charge_point_config = try_init_device_model(charge_point_config_json);
        }
        break;
    }
    }

    return charge_point_config;
}

} // namespace module
