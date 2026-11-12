// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <ocpp/v2/device_model_registry_interface.hpp>

namespace ocpp_module_common::device_model {

class ConfigServiceAccess;
class ConfigServiceBinding;
class EverestDeviceModel;

struct OwnerRegistrationResult {
    std::string owner;
    ocpp::v2::ComponentRegistrationResult result;
};

/// \brief The registrations of the EVerest device model and of the module configuration views with one device model.
/// Ending them stops the views and detaches the EVerest device model, so neither reports into the device model
/// afterwards; this happens at the latest when the object is destroyed. End them before the device model and before
/// anything whose variable listeners the device model calls, the charge point for example, is destroyed.
class DeviceModelRegistrations {
public:
    DeviceModelRegistrations() = default;
    ~DeviceModelRegistrations();
    DeviceModelRegistrations(const DeviceModelRegistrations&) = delete;
    DeviceModelRegistrations& operator=(const DeviceModelRegistrations&) = delete;
    DeviceModelRegistrations(DeviceModelRegistrations&& other) noexcept;
    DeviceModelRegistrations& operator=(DeviceModelRegistrations&& other) noexcept;

    /// \brief Registers \p everest_device_model with \p registry. \p everest_device_model must outlive this object.
    void register_everest_device_model(EverestDeviceModel& everest_device_model,
                                       ocpp::v2::DeviceModelRegistryInterface& registry);

    /// \brief Registers the module configuration views of \p config_service with \p registry
    void register_config_service(ConfigServiceAccess& config_service, ocpp::v2::DeviceModelRegistryInterface& registry);

    /// \returns The result of each registration, with its owner
    const std::vector<OwnerRegistrationResult>& results() const;

    /// \brief Stops the module configuration views and detaches the EVerest device model
    void end();

private:
    EverestDeviceModel* everest_device_model{nullptr};
    std::shared_ptr<ConfigServiceBinding> config_service_binding;
    std::vector<OwnerRegistrationResult> registration_results;
};

} // namespace ocpp_module_common::device_model
