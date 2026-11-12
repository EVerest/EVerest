// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/device_model/device_model_registrations.hpp>

#include <utility>

#include <everest/ocpp_module_common/device_model/config_service_binding.hpp>
#include <everest/ocpp_module_common/device_model/definitions.hpp>
#include <everest/ocpp_module_common/device_model/everest_device_model.hpp>

namespace ocpp_module_common::device_model {

DeviceModelRegistrations::~DeviceModelRegistrations() {
    this->end();
}

DeviceModelRegistrations::DeviceModelRegistrations(DeviceModelRegistrations&& other) noexcept :
    everest_device_model(std::exchange(other.everest_device_model, nullptr)),
    config_service_binding(std::move(other.config_service_binding)),
    registration_results(std::move(other.registration_results)) {
}

DeviceModelRegistrations& DeviceModelRegistrations::operator=(DeviceModelRegistrations&& other) noexcept {
    if (this != &other) {
        this->end();
        this->everest_device_model = std::exchange(other.everest_device_model, nullptr);
        this->config_service_binding = std::move(other.config_service_binding);
        this->registration_results = std::move(other.registration_results);
    }
    return *this;
}

void DeviceModelRegistrations::register_everest_device_model(EverestDeviceModel& everest_device_model,
                                                             ocpp::v2::DeviceModelRegistryInterface& registry) {
    this->everest_device_model = &everest_device_model;
    this->registration_results.push_back({VARIABLE_OWNER_EVEREST, everest_device_model.register_with(registry)});
}

void DeviceModelRegistrations::register_config_service(ConfigServiceAccess& config_service,
                                                       ocpp::v2::DeviceModelRegistryInterface& registry) {
    this->config_service_binding = ConfigServiceBinding::create(config_service, registry);
    this->registration_results.push_back(
        {VARIABLE_OWNER_EVEREST_CONFIG, this->config_service_binding->registration_result()});
}

const std::vector<OwnerRegistrationResult>& DeviceModelRegistrations::results() const {
    return this->registration_results;
}

void DeviceModelRegistrations::end() {
    if (this->config_service_binding != nullptr) {
        this->config_service_binding->stop();
        this->config_service_binding.reset();
    }
    if (this->everest_device_model != nullptr) {
        this->everest_device_model->detach();
        this->everest_device_model = nullptr;
    }
    this->registration_results.clear();
}

} // namespace ocpp_module_common::device_model
