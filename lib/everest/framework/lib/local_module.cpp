// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <framework/local_module.hpp>

#include <algorithm>
#include <array>
#include <list>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <date/date.h>
#include <date/tz.h>
#include <fmt/format.h>

#include <everest/logging.hpp>
#include <framework/everest.hpp>
#include <utils/date.hpp>
#include <utils/error/error_database_map.hpp>
#include <utils/error/error_factory.hpp>
#include <utils/error/error_manager_impl.hpp>
#include <utils/error/error_manager_req.hpp>
#include <utils/error/error_manager_req_global.hpp>
#include <utils/error/error_state_monitor.hpp>
#include <utils/error/error_type_map.hpp>
#include <utils/exceptions.hpp>

namespace Everest {

namespace {
constexpr std::array<std::string_view, 1> TELEMETRY_RESERVED_KEYS = {{"connector_id"}};

std::list<std::string> interface_error_types(const nlohmann::json& interface_definition) {
    std::list<std::string> error_types;
    if (not interface_definition.contains("errors")) {
        return error_types;
    }
    for (const auto& error_namespace_it : interface_definition.at("errors").items()) {
        for (const auto& error_name_it : error_namespace_it.value().items()) {
            error_types.push_back(error_namespace_it.key() + "/" + error_name_it.key());
        }
    }
    return error_types;
}
} // namespace

nlohmann::json build_local_module_config(const ManagerConfig& config,
                                         const everest::config::ModuleConfigurations& module_configs,
                                         const std::string& module_id, bool validate_schema) {
    auto result = get_serialized_module_config(module_id, module_configs);
    result["interface_definitions"] = config.get_interface_definitions();
    result["types"] = config.get_types();
    result["settings"] = config.get_settings();
    if (validate_schema) {
        result["schemas"] = config.get_schemas();
    }
    result["module_names"] = config.get_module_names();
    auto manifests = nlohmann::json::object();
    for (const auto& manifest : config.get_manifests().items()) {
        auto manifest_copy = manifest.value();
        manifest_copy.erase("config");
        manifests[manifest.key()] = std::move(manifest_copy);
    }
    result["manifests"] = std::move(manifests);
    return result;
}

LocalModule::LocalModule(std::string module_id, const nlohmann::json& module_config, ModuleCallbacks callbacks,
                         LocalModuleEnvironment environment) :
    m_module_id(std::move(module_id)),
    m_config(std::make_unique<Config>(environment.mqtt_settings, module_config)),
    m_runtime_settings(module_config.at("settings").get<RuntimeSettings>()),
    m_callbacks(std::move(callbacks)),
    m_env(std::move(environment)) {
    if (not m_config->contains(m_module_id)) {
        throw std::runtime_error(fmt::format("Module id '{}' not found in config", m_module_id));
    }
    m_identifier = m_config->printable_identifier(m_module_id);
    m_module_name = m_config->get_module_config().module_name;
    m_manifest = m_config->get_manifests().at(m_module_name);
    m_telemetry_config = m_config->get_telemetry_config();
    m_config_service_client =
        std::make_shared<config::ConfigServiceClient>(m_env.config_service, m_module_id, m_config->get_module_names());
    setup_error_managers();
}

LocalModule::~LocalModule() {
    if (not m_ready_thread.joinable()) {
        return;
    }
    const std::lock_guard<std::mutex> lock(m_ready_mutex);
    if (m_ready_finished) {
        m_ready_thread.join();
    } else {
        // ready() never returned; the host ends the process instead of destroying a module in this state
        m_ready_thread.detach();
    }
}

void LocalModule::setup_error_managers() {
    const auto error_map = m_config->get_error_map();

    if (m_manifest.contains("enable_global_errors") and m_manifest.at("enable_global_errors").get<bool>()) {
        auto global_error_database = std::make_shared<error::ErrorDatabaseMap>();
        const error::ErrorManagerReqGlobal::SubscribeGlobalAllErrorsFunc subscribe_all =
            [this](const error::ErrorCallback& raise, const error::ErrorCallback& clear) {
                if (not m_config->get_module_info(m_module_id).global_errors_enabled) {
                    EVLOG_error << fmt::format("Module {} is not allowed to subscribe to all errors, ignore "
                                               "subscription",
                                               m_identifier);
                    return;
                }
                m_env.bus->subscribe_all_errors(m_module_id, raise, clear);
            };
        m_global_error_manager = std::make_shared<error::ErrorManagerReqGlobal>(
            std::make_shared<error::ErrorTypeMap>(error_map), global_error_database, subscribe_all);
        m_global_error_state_monitor = std::make_shared<error::ErrorStateMonitor>(global_error_database);
    }

    for (const std::string& impl : Config::keys(m_manifest.at("provides"))) {
        auto error_database = std::make_shared<error::ErrorDatabaseMap>();
        const std::string interface_name = m_manifest.at("provides").at(impl).at("interface");
        const auto allowed_error_types = interface_error_types(m_config->get_interface_definition(interface_name));
        const error::ErrorManagerImpl::PublishErrorFunc publish = [this, impl](const error::Error& error) {
            m_env.bus->publish_error(m_module_id, impl, error);
        };
        m_impl_error_managers[impl] = std::make_shared<error::ErrorManagerImpl>(
            std::make_shared<error::ErrorTypeMap>(error_map), error_database, allowed_error_types, publish, publish);
        m_impl_error_state_monitors[impl] = std::make_shared<error::ErrorStateMonitor>(error_database);

        const auto mapping = resolve_error_origin_mapping(*m_config, m_module_id, m_module_name, impl);
        const ImplementationIdentifier default_origin(m_module_id, impl, mapping);
        m_error_factories[impl] =
            std::make_shared<error::ErrorFactory>(std::make_shared<error::ErrorTypeMap>(error_map), default_origin);
    }

    for (const Requirement& req : m_config->get_requirements(m_module_id)) {
        const auto& requirement = m_manifest.at("requires").at(req.id);
        if (requirement.contains("ignore") and requirement.at("ignore").contains("errors") and
            requirement.at("ignore").at("errors").get<bool>()) {
            continue;
        }
        auto error_database = std::make_shared<error::ErrorDatabaseMap>();
        const auto allowed_error_types =
            interface_error_types(m_config->get_interface_definition(requirement.at("interface").get<std::string>()));
        const error::ErrorManagerReq::SubscribeErrorFunc subscribe =
            [this, req](const error::ErrorType& type, const error::ErrorCallback& raise,
                        const error::ErrorCallback& clear) { subscribe_error(req, type, raise, clear); };
        m_req_error_managers[req] = std::make_shared<error::ErrorManagerReq>(
            std::make_shared<error::ErrorTypeMap>(error_map), error_database, allowed_error_types, subscribe);
        m_req_error_state_monitors[req] = std::make_shared<error::ErrorStateMonitor>(error_database);
    }
}

void LocalModule::subscribe_error(const Requirement& req, const error::ErrorType& type,
                                  const error::ErrorCallback& raise, const error::ErrorCallback& clear) {
    const auto connections = m_config->resolve_requirement(m_module_id, req.id);
    if (req.index >= connections.size()) {
        EVLOG_error << fmt::format("{}: requirement {}[{}] is not connected, ignore error subscription", m_identifier,
                                   req.id, req.index);
        return;
    }
    const auto& connection = connections.at(req.index);
    const auto provider_name = m_config->get_module_name(connection.module_id);
    const auto provider_interface = m_config->get_interface_definitions().at(
        m_config->get_interfaces().at(provider_name).at(connection.implementation_id));
    const auto allowed = interface_error_types(provider_interface);
    if (std::find(allowed.begin(), allowed.end(), type) == allowed.end()) {
        EVLOG_error << fmt::format("{}: Error {} not listed in interface, ignore subscription!",
                                   m_config->printable_identifier(connection.module_id, connection.implementation_id),
                                   type);
        return;
    }
    m_env.bus->subscribe_error(m_module_id, connection.module_id, connection.implementation_id, type, raise, clear);
}

UnsubscribeToken LocalModule::subscribe_external(const std::string& topic, const Handler& handler) {
    if (not m_env.external_mqtt) {
        EVLOG_warning << fmt::format("{}: no external MQTT broker connected, ignoring subscription to {}", m_identifier,
                                     topic);
        return [] {};
    }
    const auto external_topic = m_env.mqtt_settings.external_prefix + topic;
    const auto log_name = m_identifier;
    const auto token = std::make_shared<TypedHandler>(
        topic, HandlerType::ExternalMQTT,
        std::make_shared<Handler>([handler, log_name](const std::string& incoming_topic, const nlohmann::json& data) {
            const ModuleLogScope log_scope(log_name);
            handler(incoming_topic, data);
        }));
    m_env.external_mqtt->register_handler(external_topic, token, QOS::QOS0);
    return [mqtt = m_env.external_mqtt, external_topic, token]() { mqtt->unregister_handler(external_topic, token); };
}

void LocalModule::telemetry_publish(const std::string& category, const std::string& subcategory,
                                    const std::string& type, const TelemetryMap& telemetry) {
    if (not m_runtime_settings.telemetry_enabled or not m_telemetry_config.has_value() or not m_env.external_mqtt) {
        return;
    }
    const int id = m_telemetry_config->id;
    auto telemetry_data = nlohmann::json::object(
        {{"timestamp", Date::to_rfc3339(date::utc_clock::now())}, {"connector_id", id}, {"type", type}});
    for (const auto& [key, entry] : telemetry) {
        if (std::find(TELEMETRY_RESERVED_KEYS.begin(), TELEMETRY_RESERVED_KEYS.end(), key) !=
            TELEMETRY_RESERVED_KEYS.end()) {
            EVLOG_warning << "Telemetry key " << key << " is reserved and will be overwritten.";
        } else {
            nlohmann::json data;
            std::visit([&data](const auto& value) { data = value; }, entry);
            telemetry_data[key] = data;
        }
    }
    const auto topic = fmt::format("{}{}/{}/{}", m_runtime_settings.telemetry_prefix, category, id, subcategory);
    m_env.external_mqtt->publish(topic, telemetry_data.dump());
}

ModuleAdapter LocalModule::make_adapter() {
    ModuleAdapter adapter;

    // The generated code sends variables and commands between hosted modules through the LocalBus; these two are
    // only reached for providers that are not hosted in this process.
    adapter.call = [this](const Requirement& req, const std::string& cmd_name, const Parameters&) -> Result {
        throw CmdError(fmt::format("{}: the provider of requirement {} is not hosted in this process, cannot call {}()",
                                   m_identifier, req.id, cmd_name));
    };
    adapter.subscribe = [this](const Requirement& req, const std::string& var_name, const ValueCallback&) {
        EVLOG_warning << fmt::format("{}: the provider of requirement {} is not hosted in this process, variable {} "
                                     "will never be updated",
                                     m_identifier, req.id, var_name);
    };
    adapter.publish = [](const std::string&, const std::string&, const Value&) {};

    adapter.get_error_manager_impl = [this](const std::string& impl_id) -> std::shared_ptr<error::ErrorManagerImpl> {
        const auto it = m_impl_error_managers.find(impl_id);
        return it == m_impl_error_managers.end() ? nullptr : it->second;
    };
    adapter.get_error_state_monitor_impl =
        [this](const std::string& impl_id) -> std::shared_ptr<error::ErrorStateMonitor> {
        const auto it = m_impl_error_state_monitors.find(impl_id);
        return it == m_impl_error_state_monitors.end() ? nullptr : it->second;
    };
    adapter.get_error_factory = [this](const std::string& impl_id) -> std::shared_ptr<error::ErrorFactory> {
        const auto it = m_error_factories.find(impl_id);
        return it == m_error_factories.end() ? nullptr : it->second;
    };
    adapter.get_error_manager_req = [this](const Requirement& req) -> std::shared_ptr<error::ErrorManagerReq> {
        const auto it = m_req_error_managers.find(req);
        if (it == m_req_error_managers.end()) {
            throw std::runtime_error(fmt::format("Error manager for {} not found", req.id));
        }
        return it->second;
    };
    adapter.get_error_state_monitor_req = [this](const Requirement& req) -> std::shared_ptr<error::ErrorStateMonitor> {
        const auto it = m_req_error_state_monitors.find(req);
        return it == m_req_error_state_monitors.end() ? nullptr : it->second;
    };
    adapter.get_global_error_manager = [this]() { return m_global_error_manager; };
    adapter.get_global_error_state_monitor = [this]() { return m_global_error_state_monitor; };
    adapter.get_config_service_client = [this]() { return m_config_service_client; };

    adapter.ext_mqtt_publish = [this](const std::string& topic, const std::string& data, bool retain) {
        if (m_env.external_mqtt) {
            m_env.external_mqtt->publish(m_env.mqtt_settings.external_prefix + topic, data, QOS::QOS2, retain);
        }
    };
    adapter.ext_mqtt_subscribe = [this](const std::string& topic, const StringHandler& handler) {
        return subscribe_external(topic, [handler](const std::string&, const nlohmann::json& data) {
            handler(data.is_string() ? data.get<std::string>() : data.dump());
        });
    };
    adapter.ext_mqtt_subscribe_pair = [this](const std::string& topic, const StringPairHandler& handler) {
        return subscribe_external(topic, [handler](const std::string& incoming_topic, const nlohmann::json& data) {
            handler(incoming_topic, data.is_string() ? data.get<std::string>() : data.dump());
        });
    };
    adapter.telemetry_publish = [this](const std::string& category, const std::string& subcategory,
                                       const std::string& type, const TelemetryMap& telemetry) {
        telemetry_publish(category, subcategory, type, telemetry);
    };
    adapter.get_mapping = [this]() { return m_config->get_module_3_tier_model_mappings(m_module_id); };

    adapter.module_id = m_module_id;
    adapter.local = m_env.bus;
    return adapter;
}

void LocalModule::register_module() {
    const ModuleLogScope log_scope(m_identifier);
    m_callbacks.register_module_adapter(make_adapter());
    // provided commands register with the LocalBus from inside everest_register; the returned list is for transports
    m_callbacks.everest_register(m_config->get_requirement_initialization(m_module_id));
}

void LocalModule::init_module() {
    const ModuleLogScope log_scope(m_identifier);
    const auto module_configs = m_config->get_module_configs(m_module_id);
    auto module_info = m_config->get_module_info(m_module_id);
    populate_module_info_path_from_runtime_settings(module_info, m_runtime_settings);
    module_info.telemetry_enabled = m_runtime_settings.telemetry_enabled;
    const auto module_mappings = m_config->get_module_3_tier_model_mappings(m_module_id);
    if (module_mappings.has_value()) {
        module_info.mapping = module_mappings.value().module;
    }
    m_callbacks.init(module_configs, module_info);
}

void LocalModule::start() {
    m_ready_thread = std::thread([this] {
        const ModuleLogScope log_scope(m_identifier);
        try {
            if (m_callbacks.ready) {
                m_callbacks.ready();
            }
        } catch (const std::exception& e) {
            EVLOG_critical << fmt::format("ready() of {} threw: {}", m_identifier, e.what());
        }
        {
            const std::lock_guard<std::mutex> lock(m_ready_mutex);
            m_ready_finished = true;
        }
        m_ready_cv.notify_all();
    });
}

void LocalModule::shutdown() {
    const ModuleLogScope log_scope(m_identifier);
    try {
        if (m_callbacks.shutdown) {
            m_callbacks.shutdown();
        }
    } catch (const std::exception& e) {
        EVLOG_error << fmt::format("shutdown() of {} threw: {}", m_identifier, e.what());
    }
}

bool LocalModule::wait_ready_finished(std::chrono::steady_clock::time_point deadline) {
    if (not m_ready_thread.joinable()) {
        return true;
    }
    std::unique_lock<std::mutex> lock(m_ready_mutex);
    return m_ready_cv.wait_until(lock, deadline, [this] { return m_ready_finished; });
}

const std::string& LocalModule::module_id() const {
    return m_module_id;
}

const std::string& LocalModule::identifier() const {
    return m_identifier;
}

} // namespace Everest
