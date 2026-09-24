// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <utils/config.hpp>
#include <utils/config/settings.hpp>
#include <utils/mqtt_abstraction.hpp>

namespace Everest {

/// \brief Publishes the retained interface, type, settings, schema, manifest and module name topics that modules
/// read through get_module_config() during startup.
void publish_startup_metadata(const ManagerConfig& config, MQTTAbstraction& mqtt, const ManagerSettings& ms);

} // namespace Everest
