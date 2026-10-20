// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/device_model/config_service_binding.hpp>

#include <set>
#include <stdexcept>
#include <tuple>

#include <everest/logging.hpp>

namespace ocpp_module_common::device_model {

using ocpp::v2::AttributeEnum;
using ocpp::v2::Component;
using ocpp::v2::SetVariableStatusEnum;
using ocpp::v2::Variable;

namespace {

class ConfigServiceClientAccess : public ConfigServiceAccess {
public:
    explicit ConfigServiceClientAccess(std::shared_ptr<Everest::config::ConfigServiceClient> client) :
        client(std::move(client)) {
    }

    std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters>
    get_module_configs() override {
        return this->client->get_module_configs();
    }

    std::map<std::string, ModuleTierMappings> get_mappings() override {
        return this->client->get_mappings();
    }

    Everest::config::SetConfigResult
    set_config_value(const everest::config::ConfigurationParameterIdentifier& identifier,
                     const std::string& value) override {
        return this->client->set_config_value(identifier, value);
    }

    std::optional<std::function<void()>>
    register_config_update_handler(const std::string& module_id,
                                   Everest::config::ConfigServiceClient::ConfigUpdateHandler handler) override {
        return this->client->register_config_update_handler(module_id, std::move(handler));
    }

private:
    std::shared_ptr<Everest::config::ConfigServiceClient> client;
};

ocpp::v2::DataEnum to_data_enum(const everest::config::Datatype& data_type) {
    switch (data_type) {
    case everest::config::Datatype::Unknown:
        break;
    case everest::config::Datatype::String:
        return ocpp::v2::DataEnum::string;
    case everest::config::Datatype::Decimal:
        return ocpp::v2::DataEnum::decimal;
    case everest::config::Datatype::Integer:
        return ocpp::v2::DataEnum::integer;
    case everest::config::Datatype::Boolean:
        return ocpp::v2::DataEnum::boolean;
    }
    throw std::out_of_range("configuration parameter of unknown type");
}

ocpp::v2::MutabilityEnum to_mutability_enum(const everest::config::Mutability& mutability) {
    switch (mutability) {
    case everest::config::Mutability::ReadOnly:
        return ocpp::v2::MutabilityEnum::ReadOnly;
    case everest::config::Mutability::ReadWrite:
        return ocpp::v2::MutabilityEnum::ReadWrite;
    case everest::config::Mutability::WriteOnly:
        return ocpp::v2::MutabilityEnum::WriteOnly;
    }
    throw std::out_of_range("configuration parameter of unknown mutability");
}

SetVariableStatusEnum to_set_variable_status(const Everest::config::SetConfigResult& result) {
    if (result.status != Everest::config::ResponseStatus::Ok) {
        return SetVariableStatusEnum::Rejected;
    }
    switch (result.set_status) {
    case everest::config::SetConfigStatus::Accepted:
        return SetVariableStatusEnum::Accepted;
    case everest::config::SetConfigStatus::RebootRequired:
        return SetVariableStatusEnum::RebootRequired;
    case everest::config::SetConfigStatus::Rejected:
        return SetVariableStatusEnum::Rejected;
    }
    return SetVariableStatusEnum::Rejected;
}

std::optional<ocpp::v2::EVSE> evse_of(const std::map<std::string, ModuleTierMappings>& mappings,
                                      const std::string& module_id) {
    const auto mapping_it = mappings.find(module_id);
    if (mapping_it == mappings.end() or not mapping_it->second.module.has_value()) {
        return std::nullopt;
    }
    const auto& module_mapping = mapping_it->second.module.value();
    // In OCPP 2.x the id and connectorId of an EVSE must be > 0
    if (module_mapping.evse <= 0) {
        return std::nullopt;
    }
    ocpp::v2::EVSE evse;
    evse.id = module_mapping.evse;
    if (module_mapping.connector.has_value() and module_mapping.connector.value() > 0) {
        evse.connectorId = module_mapping.connector.value();
    }
    return evse;
}

ocpp::v2::VariableAttribute make_actual_attribute(const everest::config::ConfigurationParameter& parameter) {
    ocpp::v2::VariableAttribute attribute;
    attribute.type = AttributeEnum::Actual;
    attribute.mutability = to_mutability_enum(parameter.characteristics.mutability);
    // The configuration service keeps the value across restarts
    attribute.persistent = true;
    attribute.constant = false;
    attribute.value = everest::config::config_entry_to_string(parameter.value);
    return attribute;
}

} // namespace

std::unique_ptr<ConfigServiceAccess>
make_config_service_access(std::shared_ptr<Everest::config::ConfigServiceClient> config_service_client) {
    return std::make_unique<ConfigServiceClientAccess>(std::move(config_service_client));
}

ocpp::v2::VariableCharacteristics
to_variable_characteristics(const everest::config::ConfigurationParameterCharacteristics& characteristics) {
    ocpp::v2::VariableCharacteristics result;
    result.dataType = to_data_enum(characteristics.datatype);
    // A monitor would put the value into a NotifyEvent, which OCPP never reports for WriteOnly values
    result.supportsMonitoring = characteristics.mutability != everest::config::Mutability::WriteOnly;
    if (characteristics.unit.has_value()) {
        result.unit = characteristics.unit.value();
    }
    // Integer limits are compared as unsigned values by the device model, so negative limits are not represented
    const bool limits_representable =
        result.dataType == ocpp::v2::DataEnum::decimal or
        (result.dataType == ocpp::v2::DataEnum::integer and characteristics.min_value.value_or(0) >= 0 and
         characteristics.max_value.value_or(0) >= 0);
    if (limits_representable) {
        result.minLimit = characteristics.min_value;
        result.maxLimit = characteristics.max_value;
    }
    return result;
}

std::vector<ConfigParameterBinding> build_module_configuration_views(
    const std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters>& module_configs,
    const std::map<std::string, ModuleTierMappings>& mappings) {
    std::vector<ConfigParameterBinding> views;
    for (const auto& [module_id_type, module_config] : module_configs) {
        Component component;
        try {
            component.name = module_id_type.module_type;
            component.instance = module_id_type.module_id;
        } catch (const ocpp::StringConversionException& e) {
            EVLOG_error << "Could not add the configuration of module " << module_id_type.module_id
                        << " to the OCPP device model: " << e.what();
            continue;
        }
        component.evse = evse_of(mappings, module_id_type.module_id);

        for (const auto& [implementation_id, parameters] : module_config) {
            const auto prefix = implementation_id == Everest::config::MODULE_IMPLEMENTATION_ID
                                    ? std::string{}
                                    : implementation_id + ".";
            for (const auto& parameter : parameters) {
                ConfigParameterBinding view;
                try {
                    view.variable.name = prefix + parameter.name;
                } catch (const ocpp::StringConversionException& e) {
                    EVLOG_error << "Could not add the configuration parameter " << module_id_type.module_id << "."
                                << prefix << parameter.name << " to the OCPP device model: " << e.what();
                    continue;
                }
                view.parameter.module_id = module_id_type.module_id;
                view.parameter.module_implementation_id = implementation_id;
                view.parameter.configuration_parameter_name = parameter.name;
                view.component = component;
                views.push_back(std::move(view));
            }
        }
    }
    return views;
}

bool ConfigServiceBinding::ParameterKey::operator<(const ParameterKey& rhs) const {
    return std::tie(this->module_id, this->implementation_id, this->name) <
           std::tie(rhs.module_id, rhs.implementation_id, rhs.name);
}

ConfigServiceBinding::ParameterKey
ConfigServiceBinding::key_of(const everest::config::ConfigurationParameterIdentifier& identifier) {
    return {identifier.module_id,
            identifier.module_implementation_id.value_or(Everest::config::MODULE_IMPLEMENTATION_ID),
            identifier.configuration_parameter_name};
}

std::unique_ptr<ConfigServiceBinding> ConfigServiceBinding::create(ConfigServiceAccess& config_service,
                                                                   ocpp::v2::DeviceModelRegistryInterface& registry) {
    auto binding = std::make_unique<ConfigServiceBinding>(config_service, registry);
    binding->start();
    return binding;
}

ConfigServiceBinding::ConfigServiceBinding(ConfigServiceAccess& config_service,
                                           ocpp::v2::DeviceModelRegistryInterface& registry) :
    config_service(config_service), registry(registry) {
}

ConfigServiceBinding::~ConfigServiceBinding() {
    this->stop();
}

void ConfigServiceBinding::unregister_update_handlers() {
    for (const auto& unregister : this->unregister_update_handler_functions) {
        unregister();
    }
    this->unregister_update_handler_functions.clear();
}

void ConfigServiceBinding::stop() {
    if (this->stopped) {
        return;
    }
    this->stopped = true;
    // Unregistering the handlers waits for a report in progress, so no report follows the unregistration
    this->unregister_update_handlers();
    this->registry.unregister_components(VARIABLE_SOURCE_EVEREST_CONFIG);
}

void ConfigServiceBinding::start() {
    // Notifications are not retained: subscribe first, then read the values
    for (const auto& [module_id_type, module_config] : this->config_service.get_module_configs()) {
        auto unregister = this->config_service.register_config_update_handler(
            module_id_type.module_id,
            [this](const Everest::config::ConfigUpdateNotification& notification) { on_config_update(notification); });
        if (not unregister.has_value()) {
            EVLOG_warning << "Configuration changes of module " << module_id_type.module_id
                          << " are not delivered: its views keep the values read at start";
            continue;
        }
        this->unregister_update_handler_functions.push_back(std::move(unregister.value()));
    }

    const auto module_configs = this->config_service.get_module_configs();
    const auto views = build_module_configuration_views(module_configs, this->config_service.get_mappings());

    std::map<ParameterKey, const everest::config::ConfigurationParameter*> parameters;
    for (const auto& [module_id_type, module_config] : module_configs) {
        for (const auto& [implementation_id, module_parameters] : module_config) {
            for (const auto& parameter : module_parameters) {
                parameters[{module_id_type.module_id, implementation_id, parameter.name}] = &parameter;
            }
        }
    }

    std::map<Component, ocpp::v2::RegisteredComponent> components;
    std::map<ParameterKey, std::vector<ConfigParameterBinding>> bindings;
    for (const auto& view : views) {
        const auto key = key_of(view.parameter);
        const auto* parameter = parameters.at(key);
        ocpp::v2::RegisteredVariable registered;
        registered.variable = view.variable;
        try {
            registered.characteristics = to_variable_characteristics(parameter->characteristics);
            registered.attributes.push_back(make_actual_attribute(*parameter));
        } catch (const std::exception& e) {
            EVLOG_error << "Could not add the configuration parameter " << key.module_id << "." << view.variable.name
                        << " to the OCPP device model: " << e.what();
            continue;
        }
        auto& component = components[view.component];
        component.component = view.component;
        component.variables.push_back(std::move(registered));
        bindings[key].push_back(view);
    }

    std::vector<ocpp::v2::RegisteredComponent> registration;
    registration.reserve(components.size());
    for (auto& [component, registered] : components) {
        registration.push_back(std::move(registered));
    }

    {
        auto handle = this->state.handle();
        handle->bindings = std::move(bindings);
    }
    const auto result = this->registry.register_components(VARIABLE_SOURCE_EVEREST_CONFIG, registration, this);
    for (const auto& rejected : result.rejected) {
        EVLOG_warning << "The OCPP device model rejected the configuration parameter view " << rejected.component << "."
                      << rejected.variable;
    }

    // Changes that arrived while registering are applied in order before notifications are applied directly
    while (true) {
        std::map<ParameterKey, std::string> pending;
        {
            auto handle = this->state.handle();
            if (handle->pending.empty()) {
                handle->registered = true;
                return;
            }
            pending.swap(handle->pending);
        }
        for (const auto& [key, value] : pending) {
            this->notify(key, value);
        }
    }
}

void ConfigServiceBinding::notify(const ParameterKey& key, const std::string& value) {
    std::vector<ConfigParameterBinding> bindings;
    {
        auto handle = this->state.handle();
        const auto binding_it = handle->bindings.find(key);
        if (binding_it == handle->bindings.end()) {
            return;
        }
        bindings = binding_it->second;
    }
    // The state is released first: a variable listener may write the variable again, which calls set_value
    for (const auto& binding : bindings) {
        this->registry.notify_value_changed(binding.component, binding.variable, AttributeEnum::Actual, value,
                                            VARIABLE_SOURCE_EVEREST_CONFIG);
    }
}

void ConfigServiceBinding::on_config_update(const Everest::config::ConfigUpdateNotification& notification) {
    const auto key = key_of(notification.identifier);
    {
        auto handle = this->state.handle();
        if (not handle->registered) {
            handle->pending[key] = notification.value;
            return;
        }
    }
    this->notify(key, notification.value);
}

std::optional<everest::config::ConfigurationParameterIdentifier>
ConfigServiceBinding::parameter_of(const Component& component, const Variable& variable) {
    auto handle = this->state.handle();
    for (const auto& [key, bindings] : handle->bindings) {
        for (const auto& binding : bindings) {
            if (binding.component == component and binding.variable == variable) {
                return binding.parameter;
            }
        }
    }
    return std::nullopt;
}

SetVariableStatusEnum ConfigServiceBinding::set_value(const Component& component, const Variable& variable,
                                                      AttributeEnum attribute, const std::string& value,
                                                      const std::string& /*source*/) {
    if (attribute != AttributeEnum::Actual) {
        return SetVariableStatusEnum::Rejected;
    }
    const auto parameter = this->parameter_of(component, variable);
    if (not parameter.has_value()) {
        return SetVariableStatusEnum::Rejected;
    }
    return to_set_variable_status(this->config_service.set_config_value(parameter.value(), value));
}

} // namespace ocpp_module_common::device_model
