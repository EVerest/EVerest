// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>
#include <string>

#include <framework/telemetry.hpp>
#include <utils/telemetry/catalog.hpp>
#include <utils/telemetry/transport.hpp>

namespace everest::telemetry {

/// \brief Telemetry context sending samples of the declared elements through \p sender
ModuleTelemetry make_module_telemetry(std::string module_id, const ElementDeclarations& elements,
                                      std::unique_ptr<DatagramSender> sender);

/// \brief Telemetry context of a module process
/// \returns a disabled context if telemetry is disabled, the manifest declares no telemetry or the socket cannot
/// be used
ModuleTelemetry make_module_telemetry(const std::string& module_id, const ElementDeclarations& elements, bool enabled,
                                      const std::string& socket_path);

} // namespace everest::telemetry
