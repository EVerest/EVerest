// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include <utils/config/mqtt_settings.hpp>
#include <utils/config/storage_sqlite.hpp>
#include <utils/config/types.hpp>

#include <optional>

namespace Everest {

namespace fs = std::filesystem;

/// \brief EVerest framework runtime settings needed to successfully run modules
struct RuntimeSettings {
    fs::path prefix;      ///< Prefix for EVerest installation
    fs::path etc_dir;     ///< Directory that contains configs, certificates
    fs::path data_dir;    ///< Directory for general data, definitions for EVerest interfaces, types, errors an schemas
    fs::path modules_dir; ///< Directory that contains EVerest modules
    fs::path logging_config_file;    ///< Path to the logging configuration file
    std::string telemetry_prefix;    ///< MQTT prefix for telemetry
    bool telemetry_enabled = false;  ///< If telemetry is enabled
    bool validate_schema = false;    ///< If schema validation for all var publishes and cmd calls is enabled
    bool forward_exceptions = false; ///< If exceptions in cmd handlers should be caught and forwarded to the caller
};

RuntimeSettings create_runtime_settings(const fs::path& prefix, const fs::path& etc_dir, const fs::path& data_dir,
                                        const fs::path& modules_dir, const fs::path& logging_config_file,
                                        const std::string& telemetry_prefix, bool telemetry_enabled,
                                        bool validate_schema, bool forward_exceptions);
void populate_runtime_settings(RuntimeSettings& runtime_settings, const fs::path& prefix, const fs::path& etc_dir,
                               const fs::path& data_dir, const fs::path& modules_dir,
                               const fs::path& logging_config_file, const std::string& telemetry_prefix,
                               bool telemetry_enabled, bool validate_schema, bool forward_exceptions);

/// \brief Settings needed to parse and validate a config (no runtime/DB concerns)
struct ConfigParseSettings {
    fs::path schemas_dir;         ///< Directory that contains schemas for config, manifest, interfaces, etc.
    fs::path interfaces_dir;      ///< Directory that contains interface definitions
    fs::path types_dir;           ///< Directory that contains type definitions
    fs::path errors_dir;          ///< Directory that contains error definitions
    fs::path modules_dir;         ///< Directory that contains EVerest modules
    fs::path configs_dir;         ///< Directory that contains EVerest configs
    fs::path config_file;         ///< Path to the loaded config file
    nlohmann::json config;        ///< Parsed json of the config_file
    bool validate_schema = false; ///< If schema validation is enabled
};

/// \brief Settings needed by the manager to load and validate a config
struct ManagerSettings : public ConfigParseSettings {
    fs::path db_dir;                   ///< Directory that contains the database
    fs::path www_dir;                  ///< Directory that contains the everest-admin-panel
    int controller_port = 0;           ///< Websocket port of the controller
    int controller_rpc_timeout_ms = 0; ///< RPC timeout for controller commands

    std::string run_as_user; ///< Username under which EVerest should run

    std::string version_information; ///< Version information string reported on startup of the manager

    MQTTSettings mqtt_settings;       ///< MQTT connection settings
    RuntimeSettings runtime_settings; ///< Runtime settings needed to successfully run modules

    ManagerSettings() = default;

    /// \brief Constructor that initializes the ManagerSettings with the given prefix and config file.
    ManagerSettings(const std::string& prefix, const std::string& config);

    /// \brief Initializes the ManagerSettings with the given settings and prefix.
    void init_settings(const everest::config::Settings& settings);

    /// \brief Initializes the ManagerSettings based on the user provided \p config file or fallback options
    void init_config_file(const std::string& config);

    /// \brief Initializes the ManagerSettings prefix and data_dir base on user provided \p prefix or the default
    /// prefix.
    void init_prefix_and_data_dir(const std::string& prefix);
};

/// \brief Result of a database bootstrap operation
struct DatabaseBootstrap {
    ManagerSettings ms;
    std::unique_ptr<everest::config::SqliteStorage> storage;
    bool module_configs_initialized = false;
    std::optional<everest::config::ModuleConfigurations> module_configs;
};

/// \brief Initialize a DatabaseBootstrap, loading module configs from the database if it is already valid,
/// or seeding the database from YAML if it is not yet valid or \p reset_from_yaml is true.
DatabaseBootstrap init_database_bootstrap(const std::string& prefix, const std::string& config,
                                          const std::string& db_path, bool reset_from_yaml = false);

} // namespace Everest

NLOHMANN_JSON_NAMESPACE_BEGIN
template <> struct adl_serializer<Everest::RuntimeSettings> {
    static void to_json(nlohmann::json& j, const Everest::RuntimeSettings& r);

    static void from_json(const nlohmann::json& j, Everest::RuntimeSettings& r);
};
NLOHMANN_JSON_NAMESPACE_END
