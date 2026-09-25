// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <framework/ModuleAdapter.hpp>
#include <framework/local_bus.hpp>
#include <framework/runtime.hpp>
#include <utils/config.hpp>
#include <utils/mqtt_abstraction.hpp>
#include <utils/mqtt_config_service.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace Everest {

/// Everything the modules of one process share.
struct LocalModuleEnvironment {
    std::shared_ptr<LocalBus> bus;
    /// Connection to a real broker for external MQTT and telemetry; may be null.
    std::shared_ptr<MQTTAbstraction> external_mqtt;
    std::shared_ptr<config::LocalConfigService> config_service;
    MQTTSettings mqtt_settings;
};

/// Builds the configuration document a module needs, the in-process equivalent of get_module_config().
nlohmann::json build_local_module_config(const ManagerConfig& config,
                                         const everest::config::ModuleConfigurations& module_configs,
                                         const std::string& module_id, bool validate_schema);

/// One module hosted in this process. Variables, commands and errors go through the LocalBus, configuration through
/// the LocalConfigService, external MQTT and telemetry through the shared broker connection. It owns no transport and
/// no dispatch threads; ready() runs on a thread of its own, as it does in a module process.
class LocalModule {
public:
    LocalModule(std::string module_id, const nlohmann::json& module_config, ModuleCallbacks callbacks,
                LocalModuleEnvironment environment);
    LocalModule(const LocalModule&) = delete;
    LocalModule& operator=(const LocalModule&) = delete;
    ~LocalModule();

    /// Hands the adapter to the module and lets it register its implementations and requirements.
    void register_module();
    /// Passes configuration and module info to the module and runs its init().
    void init_module();
    /// Starts ready() on its own thread.
    void start();
    /// Runs the module's shutdown handler on the calling thread.
    void shutdown();
    /// Waits until ready() has returned; false when it is still running at \p deadline.
    bool wait_ready_finished(std::chrono::steady_clock::time_point deadline);

    const std::string& module_id() const;
    const std::string& identifier() const;

private:
    ModuleAdapter make_adapter();
    void setup_error_managers();
    void subscribe_error(const Requirement& req, const error::ErrorType& type, const error::ErrorCallback& raise,
                         const error::ErrorCallback& clear);
    UnsubscribeToken subscribe_external(const std::string& topic, const Handler& handler);
    void telemetry_publish(const std::string& category, const std::string& subcategory, const std::string& type,
                           const TelemetryMap& telemetry);

    std::string m_module_id;
    std::unique_ptr<Config> m_config;
    RuntimeSettings m_runtime_settings;
    std::string m_identifier;
    std::string m_module_name;
    nlohmann::json m_manifest;
    std::optional<TelemetryConfig> m_telemetry_config;
    ModuleCallbacks m_callbacks;
    LocalModuleEnvironment m_env;
    std::shared_ptr<config::ConfigServiceClient> m_config_service_client;

    std::map<std::string, std::shared_ptr<error::ErrorManagerImpl>> m_impl_error_managers;
    std::map<std::string, std::shared_ptr<error::ErrorStateMonitor>> m_impl_error_state_monitors;
    std::map<std::string, std::shared_ptr<error::ErrorFactory>> m_error_factories;
    std::map<Requirement, std::shared_ptr<error::ErrorManagerReq>> m_req_error_managers;
    std::map<Requirement, std::shared_ptr<error::ErrorStateMonitor>> m_req_error_state_monitors;
    std::shared_ptr<error::ErrorManagerReqGlobal> m_global_error_manager;
    std::shared_ptr<error::ErrorStateMonitor> m_global_error_state_monitor;

    std::thread m_ready_thread;
    std::mutex m_ready_mutex;
    std::condition_variable m_ready_cv;
    bool m_ready_finished{false};
};

} // namespace Everest
