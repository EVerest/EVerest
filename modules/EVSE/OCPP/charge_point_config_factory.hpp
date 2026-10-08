// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "OCPP.hpp"

#include <filesystem>
#include <memory>

#include <ocpp/v16/charge_point_configuration_interface.hpp>

namespace module {

/// \brief Factory function to create the ChargePointConfigurationInterface implementation based on the config.
/// \param ocpp_share_path The share path of the OCPP module, used to resolve relative paths in the config.
/// \param config The OCPP module configuration struct.
/// \param n_evse The number of EVSEs, used for integrity checks when using the device model backend.
/// \return A unique pointer to the created ChargePointConfigurationInterface implementation.
std::unique_ptr<ocpp::v16::ChargePointConfigurationInterface>
create_charge_point_configuration(const std::filesystem::path& ocpp_share_path, const Conf& config, int32_t n_evse);

} // namespace module
