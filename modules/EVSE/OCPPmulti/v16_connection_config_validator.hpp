// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>
#include <string>

#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/ocpp_types.hpp>

namespace ocpp_multi {

/// \brief B09-style validation of connection-config writes in v16 mode, mirroring the 2.x
///        Provisioning::validate_set_variable. The 2.x certificate-installation checks are not
///        mirrored; a missing certificate surfaces at connect time through slot failover.
class V16ConnectionConfigValidator {
public:
    explicit V16ConnectionConfigValidator(ocpp::v2::DeviceModelInterface& device_model);

    /// \returns the reasonCode when the write must be rejected
    std::optional<std::string> validate_write(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                                              const std::string& value) const;

private:
    std::optional<std::string> validate_network_configuration_slot_write(const ocpp::v2::Component& component,
                                                                         const ocpp::v2::Variable& variable,
                                                                         const std::string& value) const;
    std::optional<std::string> validate_network_configuration_priority_write(const std::string& value) const;

    ocpp::v2::DeviceModelInterface& m_device_model;
};

} // namespace ocpp_multi
