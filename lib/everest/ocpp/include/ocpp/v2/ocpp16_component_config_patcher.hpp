// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <map>

#include <ocpp/v16/charge_point_configuration.hpp>
#include <ocpp/v2/init_device_model_db.hpp>
#include <ocpp/v2/ocpp16_key_mappings.hpp>

namespace ocpp::v2 {

/// \brief Patch an in-memory OCPP 2.x component config map with values from an OCPP 1.6 configuration.
///
/// For each OCPP 1.6 key that has a known OCPP 2.x mapping, writes the 1.6 value into the
/// corresponding variable in \p component_configs. Keys without a standard mapping are looked
/// up in \p custom_key_mappings. Patching is skipped when the target component/variable does
/// not exist in \p component_configs.
///
/// Network connection keys (CentralSystemURI, SecurityProfile, AuthorizationKey, HostName,
/// ChargePointId) are written into the NetworkConfiguration slot given by \p network_config_slot;
/// set to 0 to skip those keys entirely.
///
/// Missing mappings and unknown components/variables are logged and skipped.
///
/// \param component_configs    In-memory component config map to patch (modified in place).
/// \param ocpp16_config        Source OCPP 1.6 configuration to read values from.
/// \param custom_key_mappings  Additional OCPP 1.6 key → ComponentVariable mappings used as
///                             a fallback for keys absent from the built-in mapping table.
/// \param network_config_slot  1-based NetworkConfiguration slot for connection-detail keys;
///                             0 disables migration of those keys.
/// \throws Ocpp16KeyMappingsError when the target of an overridden key has no Actual attribute or another data
///         type than the key's built-in variable.
void patch_component_config_with_ocpp16(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                                        ocpp::v16::ChargePointConfiguration& ocpp16_config,
                                        const Ocpp16KeyMappings& custom_key_mappings = {},
                                        int32_t network_config_slot = 1);

/// \brief Remove the built-in OCPP16LegacyCtrlr variable of every key that \p custom_key_mappings maps to another
/// variable in \p component_configs, so an OCPP 2.x CSMS does not see a variable that no longer holds the key's value.
///
/// The built-in variable stays reserved for its key (see ocpp::v16::VariableResolver). An override target without an
/// Actual attribute or with another data type than the built-in variable throws Ocpp16KeyMappingsError. An override
/// whose target does not exist in \p component_configs keeps the built-in variable; the integrity check of the
/// OCPP 1.6 device model configuration reports it.
///
/// \param component_configs    In-memory component config map to modify in place.
/// \param custom_key_mappings  The OCPP 1.6 key mapping file entries.
void remove_overridden_ocpp16_legacy_variables(
    std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
    const Ocpp16KeyMappings& custom_key_mappings);

} // namespace ocpp::v2
