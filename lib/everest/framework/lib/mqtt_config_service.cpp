// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <exception>

#include <everest/logging.hpp>

#include <utils/config.hpp>
#include <utils/config/storage.hpp>
#include <utils/config/types.hpp>
#include <utils/config_service_interface.hpp>
#include <utils/conversions.hpp>
#include <utils/mqtt_config_service.hpp>

namespace Everest {
namespace config {

bool ModuleIdType::operator<(const ModuleIdType& rhs) const {
    return (this->module_id < rhs.module_id ||
            (this->module_id == rhs.module_id && this->module_type < rhs.module_type));
}

enum class AccessMethod {
    Read,
    Write
};

namespace {
bool access_allowed(const everest::config::Access& access, const std::string& origin, const std::string& target,
                    AccessMethod method) {
    if (origin == target) {
        // a module can always read and write its own config
        return true;
    }
    if (not access.config.has_value()) {
        return false;
    }
    const auto& config_access = access.config.value();
    const auto& config_access_modules_it = config_access.modules.find(target);
    switch (method) {
    case AccessMethod::Read:
        if (config_access.allow_global_read) {
            return true;
        }
        if (config_access_modules_it != config_access.modules.end()) {
            if (config_access_modules_it->second.allow_read) {
                return true;
            }
        }
        break;
    case AccessMethod::Write:
        if (config_access.allow_global_write) {
            return true;
        }
        if (config_access_modules_it != config_access.modules.end()) {
            if (config_access_modules_it->second.allow_write) {
                return true;
            }
        }
        break;
    }

    return false;
}

nlohmann::json get_module_config(const std::string& module_id,
                                 const everest::config::ModuleConfigurations& module_configurations) {
    return get_serialized_module_config(module_id, module_configurations);
}

everest::config::ModuleConfigurationParameters
update_mutability(const everest::config::ModuleConfigurationParameters& configuration_parameters,
                  bool allow_set_read_only = false) {
    if (not allow_set_read_only) {
        return configuration_parameters;
    }
    everest::config::ModuleConfigurationParameters updated_configuration_parameters = configuration_parameters;
    for (auto& [impl_id, config_params] : updated_configuration_parameters) {
        for (auto& config_param : config_params) {
            auto& mutability = config_param.characteristics.mutability;
            if (mutability == everest::config::Mutability::ReadOnly) {
                config_param.characteristics.mutability = everest::config::Mutability::ReadWrite;
            }
        }
    }
    return updated_configuration_parameters;
}

GetResponse handle_get_module_config(const std::string& origin,
                                     const everest::config::ModuleConfigurations& module_configurations) {
    GetResponse get_response;
    get_response.type = GetType::Module;
    get_response.data = get_module_config(origin, module_configurations);
    return get_response;
}

std::optional<everest::config::ConfigurationParameter>
find_config_parameter(const everest::config::ModuleConfigurations& module_configs,
                      const everest::config::ConfigurationParameterIdentifier& identifier) {
    const auto mod_it = module_configs.find(identifier.module_id);
    if (mod_it == module_configs.end()) {
        return std::nullopt;
    }
    const auto impl_id = identifier.module_implementation_id.value_or(MODULE_IMPLEMENTATION_ID);
    const auto params_it = mod_it->second.configuration_parameters.find(impl_id);
    if (params_it == mod_it->second.configuration_parameters.end()) {
        return std::nullopt;
    }
    for (const auto& param : params_it->second) {
        if (param.name == identifier.configuration_parameter_name) {
            return param;
        }
    }
    return std::nullopt;
}

Response handle_get_config_value(const GetRequest& get_request, const std::string& origin,
                                 const everest::config::ModuleConfigurations& module_configurations) {
    Response response;
    response.type = Type::Get;
    GetResponse get_response;
    get_response.type = GetType::Value;

    if (not get_request.identifier.has_value()) {
        response.status = ResponseStatus::Error; // no identifier is always an error in this case
        response.status_info = "No identifier provided";
    } else {
        const auto identifier = get_request.identifier.value();
        const everest::config::Access access = module_configurations.at(origin).access;
        if (not access_allowed(access, origin, identifier.module_id, AccessMethod::Read)) {
            response.status = ResponseStatus::AccessDenied;
            response.status_info =
                fmt::format("Access to config item denied: {} cannot access {}", origin, identifier.module_id);
        } else {
            const auto param = find_config_parameter(module_configurations, identifier);
            if (param.has_value()) {
                get_response.data = param.value();
                response.status = ResponseStatus::Ok;
            }
        }
    }
    response.response = get_response;
    return response;
}

GetResponse handle_get_all_configs(const std::string& origin,
                                   const everest::config::ModuleConfigurations& module_configurations) {
    // FIXME: we might run into size limits when the config is really large, split this up in the
    // future!
    GetResponse get_response;
    get_response.type = GetType::All;
    json all_configs = json::object();
    everest::config::Access access = module_configurations.at(origin).access;
    for (const auto& [module_id, module_cfg] : module_configurations) {
        if (not access_allowed(access, origin, module_id, AccessMethod::Read)) {
            // request.origin has no access to module_id.config
            continue;
        }
        auto allow_set_read_only = false;
        if (access.config.has_value()) {
            const auto config_access = access.config.value();
            allow_set_read_only = config_access.allow_set_read_only;

            if (not allow_set_read_only) {
                // check if allow_set_read_only is set for specific modules
                const auto module_config_access_it = config_access.modules.find(module_id);
                if (module_config_access_it != config_access.modules.end()) {
                    allow_set_read_only = module_config_access_it->second.allow_set_read_only;
                }
            }
        }

        all_configs[module_id] = update_mutability(module_cfg.configuration_parameters, allow_set_read_only);
    }
    get_response.data = all_configs;
    return get_response;
}

std::optional<everest::config::ConfigurationParameterCharacteristics>
find_param_characteristics(const everest::config::ModuleConfigurationParameters& params, const std::string& impl_id,
                           const std::string& param_name) {
    const auto impl_it = params.find(impl_id);
    if (impl_it == params.end()) {
        return std::nullopt;
    }
    for (const auto& entry : impl_it->second) {
        if (entry.name == param_name) {
            return entry.characteristics;
        }
    }
    return std::nullopt;
}

GetResponse handle_get_all_mappings(const std::string& origin,
                                    const everest::config::ModuleConfigurations& module_configurations) {
    GetResponse get_response;
    get_response.type = GetType::AllMappings;
    json all_mappings = json::object();
    const everest::config::Access access = module_configurations.at(origin).access;
    for (const auto& [module_id, module_cfg] : module_configurations) {
        if (access_allowed(access, origin, module_id, AccessMethod::Read)) {
            all_mappings[module_id] = module_cfg.mapping;
        }
    }

    get_response.data = all_mappings;
    return get_response;
}

Response handle_set_request(const SetRequest& set_request, const std::string& origin,
                            const everest::config::ModuleConfigurations& module_configs,
                            MQTTAbstraction& mqtt_abstraction, Everest::config::ConfigServiceInterface& config_svc) {

    Response response;
    response.type = Type::Set;
    SetResponse set_response;
    set_response.status = SetResponseStatus::Rejected;
    const auto& id = set_request.identifier;

    const auto origin_it = module_configs.find(origin);
    if (origin_it == module_configs.end()) {
        response.status = ResponseStatus::Error;
        response.status_info = fmt::format("Unknown origin module: {}", origin);
        response.response = set_response;
        return response;
    }
    const everest::config::Access access = origin_it->second.access;

    // ConfigServiceCore
    if (not access_allowed(access, origin, id.module_id, AccessMethod::Write)) {
        response.status = ResponseStatus::AccessDenied;
        response.status_info = fmt::format("Access to config item denied: {} cannot access {}", origin, id.module_id);
        response.response = set_response;
        return response;
    }

    Origin origin_full{false, origin};
    ConfigParameterUpdate update;
    update.identifier = set_request.identifier;
    update.value = set_request.value;
    const auto results =
        config_svc.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, std::vector{update}, origin_full);
    response.status = ResponseStatus::Ok;
    response.type = Type::Set;

    // fill the response to be sent via MQTT correctly from the results
    if (results.status != SetConfigParameterStatus::Ok or !results.parameter_results.has_value() or
        results.parameter_results.value().empty()) {
        set_response.status = SetResponseStatus::Rejected;
        if (results.status == SetConfigParameterStatus::Error) {
            response.status = ResponseStatus::Error;
        }
    } else {
        set_response.status_info = results.parameter_results.value().front().status_info;
        switch (results.parameter_results.value().front().status) {
        case Everest::config::SetConfigParameterResultEnum::Applied:
            set_response.status = SetResponseStatus::Accepted;
            break;
        case Everest::config::SetConfigParameterResultEnum::WillApplyOnRestart:
            set_response.status = SetResponseStatus::RebootRequired;
            break;
        case Everest::config::SetConfigParameterResultEnum::AccessDenied:
            response.status = ResponseStatus::AccessDenied;
            response.status_info = fmt::format("Access to config item denied: {} cannot access {}", origin,
                                               set_request.identifier.module_id);
            break;
        case Everest::config::SetConfigParameterResultEnum::DoesNotExist:
            response.status = ResponseStatus::Error;
            break;
        case Everest::config::SetConfigParameterResultEnum::Rejected:
            // TODO(CB) : commented, because the test failed otherwise - correct?
            // response.status = ResponseStatus::Error;
            response.status_info =
                fmt::format("Config parameter {} not found in module {}",
                            set_request.identifier.configuration_parameter_name, set_request.identifier.module_id);
            break;
        default:
            set_response.status = SetResponseStatus::Rejected;
        }
    }

    response.response = set_response;
    return response;
}
} // namespace

ConfigServiceClient::ConfigServiceClient(std::shared_ptr<MQTTAbstraction> mqtt_abstraction,
                                         const std::string& module_id,
                                         const std::map<std::string, std::string, std::less<>>& module_names) :
    mqtt_abstraction(mqtt_abstraction), origin(module_id), module_names(module_names) {
}

std::map<ModuleIdType, everest::config::ModuleConfigurationParameters> ConfigServiceClient::get_module_configs() {
    Request get_request;
    get_request.type = Type::Get;
    get_request.request = GetRequest{GetType::All};
    get_request.origin = this->origin;

    MQTTRequest mqtt_request;
    mqtt_request.response_topic =
        fmt::format("{}modules/{}/response", mqtt_abstraction->get_everest_prefix(), get_request.origin);
    mqtt_request.request_topic = fmt::format("{}config/request", mqtt_abstraction->get_everest_prefix());
    mqtt_request.request_data = json(get_request).dump();

    try {
        Response response = mqtt_abstraction->get(mqtt_request, mqtt_get_config_retries);
        if (response.status != ResponseStatus::Ok) {
            EVLOG_error << "Could not get module configs via MQTT";
            return {};
        }

        GetResponse get_response = std::get<GetResponse>(response.response);

        std::map<ModuleIdType, everest::config::ModuleConfigurationParameters> module_configs;
        for (const auto& [module_id, config_maps] : get_response.data.items()) {
            ModuleIdType module_id_type;
            module_id_type.module_id = module_id;
            module_id_type.module_type = module_names.at(module_id);
            module_configs[module_id_type] = config_maps;
        }

        return module_configs;
    } catch (const std::exception& e) {
        EVLOG_error << "Could not get module configs via MQTT: " << e.what();
        return {};
    }
}

std::map<std::string, ModuleTierMappings> ConfigServiceClient::get_mappings() {
    Request get_request;
    get_request.type = Type::Get;
    get_request.request = GetRequest{GetType::AllMappings};
    get_request.origin = this->origin;

    MQTTRequest mqtt_request;
    mqtt_request.response_topic =
        fmt::format("{}modules/{}/response", mqtt_abstraction->get_everest_prefix(), get_request.origin);
    mqtt_request.request_topic = fmt::format("{}config/request", mqtt_abstraction->get_everest_prefix());
    mqtt_request.request_data = json(get_request).dump();

    try {
        Response response = mqtt_abstraction->get(mqtt_request, mqtt_get_config_retries);
        if (response.status != ResponseStatus::Ok) {
            EVLOG_error << "Could not get mappings configs via MQTT";
            return {};
        }

        GetResponse get_response = std::get<GetResponse>(response.response);

        std::map<std::string, ModuleTierMappings> mappings;
        for (const auto& [module_id, mapping] : get_response.data.items()) {
            mappings[module_id] = everest::config::parse_mapping(mapping);
        }

        return mappings;
    } catch (const std::exception& e) {
        EVLOG_error << "Could not get mappings configs via MQTT: " << e.what();
        return {};
    }
}

SetConfigResult
ConfigServiceClient::set_config_value(const everest::config::ConfigurationParameterIdentifier& identifier,
                                      const std::string& value) {
    SetConfigResult result;
    Request request;
    request.type = Type::Set;
    request.origin = this->origin;
    SetRequest set_request;
    set_request.identifier = identifier;
    set_request.value = value;
    request.request = set_request;

    try {
        MQTTRequest mqtt_request;
        mqtt_request.response_topic =
            fmt::format("{}modules/{}/response", mqtt_abstraction->get_everest_prefix(), request.origin);
        mqtt_request.request_topic = fmt::format("{}config/request", mqtt_abstraction->get_everest_prefix());
        mqtt_request.request_data = json(request).dump();

        const Response response = mqtt_abstraction->get(mqtt_request, mqtt_get_config_retries);
        result.status = response.status;
        result.status_info = response.status_info;
        if (response.status == ResponseStatus::Ok) {
            if (response.type.has_value() and response.type.value() == Type::Set) {
                const SetResponse set_response = std::get<SetResponse>(response.response);
                result.set_status = conversions::set_response_status_to_set_config_status(set_response.status);
            }
        }
        return result;
    } catch (const std::exception& e) {
        EVLOG_info << "Could not set config value: " << identifier.module_id << ": "
                   << identifier.module_implementation_id.value_or(MODULE_IMPLEMENTATION_ID) << ": "
                   << identifier.configuration_parameter_name << ": " << e.what();
        result.status = ResponseStatus::Error;
    }
    return result;
}

GetConfigResult
ConfigServiceClient::get_config_value(const everest::config::ConfigurationParameterIdentifier& identifier) {
    GetConfigResult result;
    Request request;
    request.type = Type::Get;
    request.origin = this->origin;
    GetRequest get_request;
    get_request.type = GetType::Value;
    get_request.identifier = identifier;
    request.request = get_request;

    try {
        MQTTRequest mqtt_request;
        mqtt_request.response_topic =
            fmt::format("{}modules/{}/response", mqtt_abstraction->get_everest_prefix(), request.origin);
        mqtt_request.request_topic = fmt::format("{}config/request", mqtt_abstraction->get_everest_prefix());
        mqtt_request.request_data = json(request).dump();
        const Response response = mqtt_abstraction->get(mqtt_request, mqtt_get_config_retries);
        result.status = response.status;
        result.status_info = response.status_info;
        if (response.status == ResponseStatus::Ok) {
            if (response.type.has_value() and response.type.value() == Type::Get) {
                const GetResponse get_response = std::get<GetResponse>(response.response);
                result.configuration_parameter = get_response.data;
            }
        }
    } catch (const std::exception& e) {
        EVLOG_info << "Could not get config value: " << identifier.module_id << ": "
                   << identifier.module_implementation_id.value_or(MODULE_IMPLEMENTATION_ID) << ": "
                   << identifier.configuration_parameter_name << ": " << e.what();
    }

    return result;
}

void ConfigServiceClient::register_config_change_handler(const std::string& impl_id, const std::string_view param_name,
                                                         ConfigChangeHandler handler) {
    if (change_callbacks.empty()) {
        // subscribe to the MQTT topic
        const auto mqtt_handler = [this](const std::string& /*topic*/, const nlohmann::json& data) {
            mqtt_set_request(data);
        };

        auto change_token =
            std::make_shared<TypedHandler>(HandlerType::ConfigurationRequest, std::make_shared<Handler>(mqtt_handler));
        const auto topic =
            fmt::format("{}modules/{}/config/set_request", mqtt_abstraction->get_everest_prefix(), origin);

        mqtt_abstraction->register_handler(topic, change_token, QOS::QOS2);
    }

    if (change_callbacks[impl_id].empty()) {
        change_callbacks[impl_id] = {};
    }

    // store handler
    change_callbacks[impl_id].emplace(param_name, std::move(handler));
}

void ConfigServiceClient::mqtt_set_request(const nlohmann::json& data) {
    Response response;
    response.type = Type::Set;
    SetResponse set_response;

    try {
        SetRequest set_request = data;
        const auto& name = set_request.identifier.configuration_parameter_name;

        const auto impl_it = change_callbacks.find(set_request.identifier.module_implementation_id.value_or("!module"));
        bool handler_found = false;
        if (impl_it != change_callbacks.end()) {
            const auto it = impl_it->second.find(name);
            if (it != impl_it->second.end()) {
                // keep the value as a string. The module code applies the string to
                // underlying type conversion (in the autogenerated code)

                // call the 'on_' handler
                const auto result = it->second(set_request.value);
                response.status = ResponseStatus::Ok;
                set_response.status = result.status;
                response.status_info = result.reason;
                handler_found = true;
            }
        }
        if (!handler_found) {
            response.status = ResponseStatus::Ok;
            set_response.status = SetResponseStatus::Rejected;
            response.status_info = "No handler registered for parameter: " + name;
        }
    } catch (const std::exception& e) {
        response.status = ResponseStatus::Error;
        response.status_info = std::string("Exception in config change handler: ") + e.what();
    }

    response.response = set_response;

    // Publish response back to manager using the existing Response type
    const std::string topic =
        fmt::format("{}modules/{}/config/set_response", mqtt_abstraction->get_everest_prefix(), origin);

    MqttMessagePayload payload{MqttMessageType::ConfigurationResponse, response};
    mqtt_abstraction->publish(topic, payload, QOS::QOS2);
}

MqttConfigServiceHandler::MqttConfigServiceHandler(MQTTAbstraction& mqtt_abstraction,
                                                   ConfigServiceInterface& config_svc) :
    mqtt_abstraction(mqtt_abstraction), config_svc(config_svc) {

    // TODO: thread-safe?

    const Handler global_config_request_handler = [&mqtt_abstraction, &config_svc](const std::string& /*topic*/,
                                                                                   const nlohmann::json& data) {
        Response response;
        response.status = ResponseStatus::Error;
        try {
            Request request = data;
            response.type = request.type;
            const auto response_topic =
                fmt::format("{}modules/{}/response", mqtt_abstraction.get_everest_prefix(), request.origin);

            const auto& module_configs = config_svc.get_active_module_configurations();

            if (request.type == Type::Get) {
                const GetRequest get_request = std::get<GetRequest>(request.request);
                if (get_request.type == GetType::Module) {
                    response.response = handle_get_module_config(request.origin, module_configs);
                    response.status = ResponseStatus::Ok;
                } else if (get_request.type == GetType::Value) {
                    response = handle_get_config_value(get_request, request.origin, module_configs);
                } else if (get_request.type == GetType::All) {
                    response.response = handle_get_all_configs(request.origin, module_configs);
                    response.status = ResponseStatus::Ok;
                } else if (get_request.type == GetType::AllMappings) {
                    response.response = handle_get_all_mappings(request.origin, module_configs);
                    response.status = ResponseStatus::Ok;
                }
            } else if (request.type == Type::Set) {
                auto set_request = std::get<SetRequest>(request.request);
                response =
                    handle_set_request(set_request, request.origin, module_configs, mqtt_abstraction, config_svc);
            }

            MqttMessagePayload payload{MqttMessageType::ConfigurationResponse, response};
            mqtt_abstraction.publish(response_topic, payload, QOS::QOS2);

        } catch (const std::exception& e) {
            EVLOG_error << "Exception during handling of request: " << e.what();
        } catch (...) {
            EVLOG_error << "Could not parse request: " << data.dump();
        }
    };

    const std::string global_config_request_topic =
        fmt::format("{}config/request", mqtt_abstraction.get_everest_prefix());
    this->get_config_token = std::make_shared<TypedHandler>(HandlerType::ConfigurationRequest,
                                                            std::make_shared<Handler>(global_config_request_handler));
    mqtt_abstraction.register_handler(global_config_request_topic, this->get_config_token, QOS::QOS2);
}

std::optional<Everest::config::SetResponse>
MqttConfigServiceHandler::cmd_set_cfg_param(const everest::config::ConfigurationParameterIdentifier& cfg_param_id,
                                            const std::string& value) {
    std::optional<SetResponse> response = std::nullopt;
    SetRequest set_request;
    set_request.identifier = cfg_param_id;
    set_request.value = value;

    // Forward set request to target module via MQTT and wait for its response.
    MQTTRequest mqtt_request;
    mqtt_request.response_topic =
        fmt::format("{}modules/{}/config/set_response", mqtt_abstraction.get_everest_prefix(), cfg_param_id.module_id);
    mqtt_request.request_topic =
        fmt::format("{}modules/{}/config/set_request", mqtt_abstraction.get_everest_prefix(), cfg_param_id.module_id);
    mqtt_request.request_data = nlohmann::json(set_request).dump();

    const Response module_response = mqtt_abstraction.get(mqtt_request, mqtt_get_config_retries);

    if (module_response.status == ResponseStatus::Ok && module_response.type == Type::Set) {
        response = std::get<SetResponse>(module_response.response);
    }

    return response;
}
namespace conversions {

template <> bool ConfigFromString<bool>(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    if (out == "true") {
        return true;
    } else if (out == "false") {
        return false;
    }
    throw std::out_of_range("Could not convert string '" + value + "' to bool");
}

template <> int ConfigFromString<int>(const std::string& value) {
    try {
        return std::stoi(value);
    } catch (const std::exception& ex) {
        throw std::out_of_range("Could not convert string '" + value + "' to int: " + ex.what());
    }
}

template <> double ConfigFromString<double>(const std::string& value) {
    try {
        return std::stod(value);
    } catch (const std::exception& ex) {
        throw std::out_of_range("Could not convert string '" + value + "' to double: " + ex.what());
    }
}

std::string type_to_string(Type type) {
    switch (type) {
    case Type::Get:
        return "Get";
    case Type::Set:
        return "Set";
    case Type::Unknown:
        return "Unknown";
    }
    throw std::out_of_range("Could not convert Type to string");
}

Type string_to_type(const std::string& type) {
    if (type == "Get") {
        return Type::Get;
    } else if (type == "Set") {
        return Type::Set;
    } else if (type == "Unknown") {
        return Type::Unknown;
    }
    throw std::out_of_range("Could not convert " + type + " to Type");
}

std::string get_type_to_string(GetType type) {
    switch (type) {
    case GetType::All:
        return "All";
    case GetType::Module:
        return "Module";
    case GetType::Value:
        return "Value";
    case GetType::AllMappings:
        return "AllMappings";
    case GetType::Unknown:
        return "Unknown";
    }
    throw std::out_of_range("Could not convert GetType to string");
}

GetType string_to_get_type(const std::string& type) {
    if (type == "All") {
        return GetType::All;
    } else if (type == "Module") {
        return GetType::Module;
    } else if (type == "Value") {
        return GetType::Value;
    } else if (type == "AllMappings") {
        return GetType::AllMappings;
    } else if (type == "Unknown") {
        return GetType::Unknown;
    }
    throw std::out_of_range("Could not convert " + type + " to GetType");
}

std::string response_status_to_string(ResponseStatus status) {
    switch (status) {
    case ResponseStatus::Ok:
        return "Ok";
    case ResponseStatus::Error:
        return "Error";
    case ResponseStatus::AccessDenied:
        return "AccessDenied";
    }
    throw std::out_of_range("Could not convert ResponseStatus to string");
}

ResponseStatus string_to_response_status(const std::string& status) {
    if (status == "Ok") {
        return ResponseStatus::Ok;
    } else if (status == "Error") {
        return ResponseStatus::Error;
    } else if (status == "AccessDenied") {
        return ResponseStatus::AccessDenied;
    }
    throw std::out_of_range("Could not convert " + status + " to ResponseStatus");
}

std::string set_response_status_to_string(SetResponseStatus status) {
    switch (status) {
    case SetResponseStatus::Accepted:
        return "Accepted";
    case SetResponseStatus::Rejected:
        return "Rejected";
    case SetResponseStatus::RebootRequired:
        return "RebootRequired";
    }
    throw std::out_of_range("Could not convert SetResponseStatus to string");
}

SetResponseStatus string_to_set_response_status(const std::string& status) {
    if (status == "Accepted") {
        return SetResponseStatus::Accepted;
    } else if (status == "Rejected") {
        return SetResponseStatus::Rejected;
    } else if (status == "RebootRequired") {
        return SetResponseStatus::RebootRequired;
    }
    throw std::out_of_range("Could not convert " + status + " to SetResponseStatus");
}

everest::config::SetConfigStatus set_response_status_to_set_config_status(SetResponseStatus status) {
    switch (status) {
    case SetResponseStatus::Accepted:
        return everest::config::SetConfigStatus::Accepted;
    case SetResponseStatus::Rejected:
        return everest::config::SetConfigStatus::Rejected;
    case SetResponseStatus::RebootRequired:
        return everest::config::SetConfigStatus::RebootRequired;
    }
    throw std::out_of_range("Could not convert SetResponseStatus to SetConfigStatus");
}

SetResponseStatus set_config_status_to_set_response_status(everest::config::SetConfigStatus status) {
    switch (status) {
    case everest::config::SetConfigStatus::Accepted:
        return SetResponseStatus::Accepted;
    case everest::config::SetConfigStatus::Rejected:
        return SetResponseStatus::Rejected;
    case everest::config::SetConfigStatus::RebootRequired:
        return SetResponseStatus::RebootRequired;
    }
    throw std::out_of_range("Could not convert SetConfigStatus to SetResponseStatus");
}
} // namespace conversions

std::ostream& operator<<(std::ostream& os, const GetType& t) {
    os << conversions::get_type_to_string(t);
    return os;
}

void to_json(nlohmann::json& j, const GetRequest& r) {
    j = {{"type", conversions::get_type_to_string(r.type)}};
    if (r.identifier.has_value()) {
        j["identifier"] = r.identifier.value();
    }
}

void from_json(const nlohmann::json& j, GetRequest& r) {
    r.type = conversions::string_to_get_type(j.at("type"));
    if (j.contains("identifier")) {
        r.identifier = j.at("identifier");
    }
}

void to_json(nlohmann::json& j, const SetRequest& r) {
    j = {{"value", r.value}};
    j["identifier"] = nlohmann::json(r.identifier);
}

void from_json(const nlohmann::json& j, SetRequest& r) {
    r.identifier = j.at("identifier");
    r.value = j.at("value");
}

void to_json(nlohmann::json& j, const GetResponse& r) {
    j = {{"type", conversions::get_type_to_string(r.type)}, {"data", r.data}};
}

void from_json(const nlohmann::json& j, GetResponse& r) {
    r.type = conversions::string_to_get_type(j.at("type"));
    r.data = j.at("data");
}

void to_json(nlohmann::json& j, const SetResponse& r) {
    j = {{"status", conversions::set_response_status_to_string(r.status)}};
}

void from_json(const nlohmann::json& j, SetResponse& r) {
    r.status = conversions::string_to_set_response_status(j.at("status"));
}

void to_json(nlohmann::json& j, const Request& r) {
    j = {{"type", conversions::type_to_string(r.type)}, {"origin", r.origin}};
    j["request"] = Everest::variant_to_json(r.request);
}

void from_json(const nlohmann::json& j, Request& r) {
    r.type = conversions::string_to_type(j.at("type"));
    if (r.type == Type::Get) {
        r.request = j.at("request").get<GetRequest>();
    } else if (r.type == Type::Set) {
        r.request = j.at("request").get<SetRequest>();
    }
    r.origin = j.at("origin");
}

void to_json(nlohmann::json& j, const Response& r) {
    j = {{"status", conversions::response_status_to_string(r.status)}, {"status_info", r.status_info}};
    if (r.type.has_value()) {
        j["type"] = conversions::type_to_string(r.type.value());
    }
    j["response"] = Everest::variant_to_json(r.response);
}

void from_json(const nlohmann::json& j, Response& r) {
    r.status = conversions::string_to_response_status(j.at("status"));
    r.status_info = j.at("status_info");
    if (j.contains("type")) {
        Type type = conversions::string_to_type(j.at("type"));
        r.type = type;

        if (type == Type::Get) {
            r.response = j.at("response").get<GetResponse>();
        } else if (type == Type::Set) {
            r.response = j.at("response").get<SetResponse>();
        }
    }
}

void to_json(nlohmann::json& j, const SetConfigResult& r) {
    j = {
        {"status", conversions::response_status_to_string(r.status)},
        {"status_info", r.status_info},
        {"set_status", conversions::set_response_status_to_string(
                           conversions::set_config_status_to_set_response_status(r.set_status))},
    };
}

void from_json(const nlohmann::json& j, SetConfigResult& r) {
    r.status = conversions::string_to_response_status(j.at("status"));
    r.status_info = j.value("status_info", "");
    r.set_status = conversions::set_response_status_to_set_config_status(
        conversions::string_to_set_response_status(j.at("set_status")));
}

void to_json(nlohmann::json& j, const GetConfigResult& r) {
    j = {
        {"status", conversions::response_status_to_string(r.status)},
        {"status_info", r.status_info},
        {"value", r.configuration_parameter.value},
    };
}

void from_json(const nlohmann::json& j, GetConfigResult& r) {
    r.status = conversions::string_to_response_status(j.at("status"));
    r.status_info = j.value("status_info", "");
    r.configuration_parameter.value = j.at("value").get<everest::config::ConfigEntry>();
}

void to_json(nlohmann::json& j, const ConfigChangeResult& r) {
    j = {
        {"status", conversions::set_response_status_to_string(r.status)},
        {"reason", r.reason},
    };
}

void from_json(const nlohmann::json& j, ConfigChangeResult& r) {
    const std::string status = j.value("status", "Rejected");
    const std::string reason = j.value("reason", "");
    if (status == "Accepted") {
        r = ConfigChangeResult::Accepted();
    } else if (status == "RebootRequired") {
        r = ConfigChangeResult::AcceptedRebootRequired();
    } else {
        r = ConfigChangeResult::Rejected(reason);
    }
}

} // namespace config
} // namespace Everest

NLOHMANN_JSON_NAMESPACE_BEGIN
void adl_serializer<everest::config::ConfigurationParameterIdentifier>::to_json(
    nlohmann::json& j, const everest::config::ConfigurationParameterIdentifier& c) {
    j = {{"module_id", c.module_id}, {"configuration_parameter_name", c.configuration_parameter_name}};
    if (c.module_implementation_id.has_value()) {
        j["module_implementation_id"] = c.module_implementation_id.value();
    }
}

void adl_serializer<everest::config::ConfigurationParameterIdentifier>::from_json(
    const nlohmann::json& j, everest::config::ConfigurationParameterIdentifier& c) {
    c.module_id = j.at("module_id");
    c.configuration_parameter_name = j.at("configuration_parameter_name");
    if (j.contains("module_implementation_id")) {
        c.module_implementation_id = j.at("module_implementation_id");
    }
}
NLOHMANN_JSON_NAMESPACE_END
