// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include <memory>

#include <everest/database/sqlite/connection.hpp>
#include <utils/config/storage.hpp>

namespace everest::config {

/// \brief Opens the config database at \p db_path, applies SQL migrations from \p migrations_dir
/// exactly once, and returns a shared Connection.  open_count is 0 on return, each consumer
/// calls open_connection() in its own constructor and close_connection() in its destructor.
std::shared_ptr<everest::db::sqlite::ConnectionInterface>
open_config_database(const std::filesystem::path& db_path, const std::filesystem::path& migrations_dir);

/// \brief Implements StorageInterface with SQLite
class SqliteStorage : public StorageInterface {

public:
    /// \brief The config ID used when no explicit ID is given.
    /// Migrated databases carry their original config at this ID; fresh databases write their first config here.
    static constexpr int DEFAULT_CONFIG_ID = 0;

    /// \brief Opens its own Connection and applies migrations.
    /// \param db_path Path to SQLite database file
    /// \param migration_files_path Path to SQL migration files
    /// \param config_id The config slot ID this instance is scoped to (default: DEFAULT_CONFIG_ID)
    /// \throws MigrationException if migration fails
    /// \throws std::runtime_error if database cannot be opened
    SqliteStorage(const fs::path& db_path, const std::filesystem::path& migration_files_path,
                  int config_id = DEFAULT_CONFIG_ID);

    /// \brief Shares an already-migrated Connection.
    /// Calls open_connection() on the shared connection; the destructor calls close_connection().
    /// \param connection Shared database connection (already migrated)
    /// \param config_id The config slot ID this instance is scoped to (default: DEFAULT_CONFIG_ID)
    SqliteStorage(std::shared_ptr<everest::db::sqlite::ConnectionInterface> connection,
                  int config_id = DEFAULT_CONFIG_ID);

    /// \brief Closes this instance's database connection via close_connection().
    ~SqliteStorage();

    /// \brief Writes \p module_configs into this instance's config slot inside a single transaction.
    /// The transaction is committed only if all writes succeed; on any failure it is left uncommitted and rolled back,
    /// so a failed call leaves storage unchanged. Requires the config slot to already exist.
    GenericResponseStatus write_module_configs(const ModuleConfigurations& module_configs) override;

    /// \brief Deletes the existing config items of this instance's slot and writes \p module_configs, inside a single
    /// transaction. The transaction is committed only if both the delete and the write succeed; otherwise it is rolled
    /// back and the previous config stays intact. Requires the config slot to already exist.
    GenericResponseStatus replace_module_configs(const ModuleConfigurations& module_configs) override;

    /// \brief Reads all module configurations of this instance's config slot.
    GetModuleConfigsResponse get_module_configs() override;

    /// \brief Reads the configuration of a single module in this instance's config slot.
    GetModuleConfigurationResponse get_module_config(const std::string& module_id) override;

    /// \brief Reads a single configuration parameter identified by \p identifier from this instance's config slot.
    GetConfigurationParameterResponse
    get_configuration_parameter(const ConfigurationParameterIdentifier& identifier) override;

    /// \brief Updates the value of an existing configuration parameter; returns NotFound if no matching parameter
    /// exists.
    GetSetResponseStatus update_configuration_parameter(const ConfigurationParameterIdentifier& identifier,
                                                        const std::string& value) override;

    /// \brief Inserts or replaces a configuration parameter including its characteristics; returns NotFound if the
    /// target module does not exist in this instance's config slot.
    GetSetResponseStatus write_configuration_parameter(const ConfigurationParameterIdentifier& identifier,
                                                       const ConfigurationParameterCharacteristics characteristics,
                                                       const std::string& value) override;

private:
    std::shared_ptr<everest::db::sqlite::ConnectionInterface> m_db;
    const int m_config_id;
    GenericResponseStatus write_module_config_items(const ModuleConfigurations& module_configs);
    GenericResponseStatus write_module_data(const ModuleData& module_data);
    GenericResponseStatus write_module_fulfillment(const std::string& module_id, const Fulfillment& fulfillment);
    GenericResponseStatus write_module_tier_mapping(const std::string& module_id, const std::string& implementation_id,
                                                    const int32_t evse_id, const std::optional<int32_t> connector_id);
    GenericResponseStatus write_access(const std::string& module_id, const Access& access);
    GenericResponseStatus write_config_access(const std::string& module_id, const ConfigAccess& config_access);
    GenericResponseStatus write_module_config_access(const std::string& module_id, const std::string& other_module_id,
                                                     const ModuleConfigAccess& module_config_access);
    GenericResponseStatus delete_module_config_items();
    GenericResponseStatus delete_configuration_parameters();
    GenericResponseStatus delete_module_data();
    GenericResponseStatus delete_module_fulfillments();
    GenericResponseStatus delete_module_tier_mappings();
    GenericResponseStatus delete_access();
    GenericResponseStatus delete_config_access();
    GenericResponseStatus delete_module_config_access();
    GetModuleFulfillmentsResponse get_module_fulfillments(const std::string& module_id);
    GetModuleDataResponse get_module_data(const std::string& module_id);
    GetModuleTierMappingsResponse get_module_tier_mappings(const std::string& module_id);
    GetModuleConfigAccessResponse get_module_config_access(const std::string& module_id);
    GetConfigAccessResponse get_config_access(const std::string& module_id);
};

} // namespace everest::config
