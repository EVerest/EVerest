// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <everest/exceptions.hpp>
#include <everest/logging.hpp>

#include <ocpp/v16/charge_point_configuration_interface.hpp>

namespace module::config_factory_v16 {

/// \brief Parameters for creating the OCPP 1.6 device-model-backed configuration.
///
/// The combined OCPPmulti module always stores OCPP 1.6 configuration in the (shared) device model
/// database, bootstrapped/refreshed from the component configs on every startup. The device-model
/// database, migration and component-config paths are the same ones used by the OCPP 2.x path.
///
/// If \ref EnableLegacyConfigMigration is set, the legacy OCPP 1.6 JSON config (\ref ChargePointConfigPath)
/// is migrated into the device model as a one-time operation: it runs only on the first startup, while the
/// device model database does not yet exist. Once the database is initialized, migration is skipped and the
/// legacy JSON is neither read nor required.
struct Ocpp16DeviceModelParams {
    std::string DeviceModelDatabasePath;
    std::string DeviceModelDatabaseMigrationPath;
    std::string DeviceModelConfigPath;
    std::string DeviceModelConfigMappings;
    std::int32_t Ocpp16NetworkConfigSlot;
    bool EnableLegacyConfigMigration;
    std::string ChargePointConfigPath;
    std::string UserConfigPath;
};

/// \brief Factory function to create the device-model-backed ChargePointConfigurationInterface implementation.
///
/// Initializes the device model database from the component configs, optionally performing a one-time
/// migration from the legacy OCPP 1.6 JSON config on the first startup (see \ref Ocpp16DeviceModelParams),
/// then returns a device-model-backed configuration after an integrity check.
/// \param ocpp_share_path The share path of the OCPP module, used to resolve relative paths in the config.
/// \param config The OCPP 1.6 device-model configuration parameters.
/// \param n_evse The number of EVSEs, used for the device model integrity check.
/// \return A unique pointer to the created ChargePointConfigurationInterface implementation.
std::unique_ptr<ocpp::v16::ChargePointConfigurationInterface>
create_charge_point_configuration(const std::filesystem::path& ocpp_share_path, const Ocpp16DeviceModelParams& config,
                                  std::int32_t n_evse);

} // namespace module::config_factory_v16
