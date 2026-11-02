// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <string>
#include <vector>

#include <ocpp/v2/device_model_registry_interface.hpp>

namespace ocpp_module_common::device_model {

/// \brief Collects the conflicts between the component config and the registrations in \p results, the variables
/// that a registration defines again, and logs them as one error.
/// \return One description per conflict
/// \throws std::runtime_error naming the conflicts, when \p fail_on_conflicts is set and there is one
std::vector<std::string> check_device_model_conflicts(const std::vector<ocpp::v2::ComponentRegistrationResult>& results,
                                                      bool fail_on_conflicts);

} // namespace ocpp_module_common::device_model
