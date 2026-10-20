// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <map>
#include <memory>

#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_registry_interface.hpp>

namespace ocpp::v2 {

/// \brief Registers the AvailabilityState of the charging station and of every EVSE and connector of
/// \p evse_connector_structure under VARIABLE_SOURCE_OCPP_RUNTIME. libocpp computes these values in its
/// ComponentStateManager, the same computation that drives StatusNotification, so it owns them whether or not the
/// component config defines them; a definition in the component config is overridden. A variable that another runtime
/// source registered already is left to that source.
/// \return The number of registered variables
std::size_t register_availability_states(DeviceModelRegistryInterface& registry,
                                         const std::map<std::int32_t, std::int32_t>& evse_connector_structure);

/// \brief Creates a DeviceModel on \p storage and registers the AvailabilityState of the charging station and of every
/// EVSE and connector of \p evse_connector_structure, as ChargePoint requires.
/// \throws std::exception when the DeviceModel cannot be created
std::shared_ptr<DeviceModel>
create_device_model_with_availability_states(const std::map<std::int32_t, std::int32_t>& evse_connector_structure,
                                             std::unique_ptr<DeviceModelStorageInterface> storage);

} // namespace ocpp::v2
