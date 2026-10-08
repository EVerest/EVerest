// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <string>

namespace ocpp::v2 {

/// \brief Returns the canonical NetworkConfiguration_1 JSON schema embedded at build time.
const std::string& get_default_network_configuration_schema();

} // namespace ocpp::v2
