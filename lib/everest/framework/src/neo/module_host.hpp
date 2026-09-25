// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <framework/local_module.hpp>
#include <framework/module_plugin.hpp>
#include <utils/config.hpp>
#include <utils/config/settings.hpp>

namespace Everest::neo {

/// Module ids this process hosts: everything that is not standalone and has a shared object installed.
std::vector<std::string> hosted_module_ids(const ManagerConfig& config, const ManagerSettings& ms);

/// Loads every hosted module from its shared object and drives all of them through the lifecycle in lockstep, so
/// every module is registered before any module is initialized, and initialized before any ready() runs.
class ModuleHost {
public:
    ModuleHost(const ManagerSettings& ms, std::shared_ptr<const ManagerConfig> config,
               std::shared_ptr<const everest::config::ModuleConfigurations> module_configs,
               LocalModuleEnvironment environment);
    ModuleHost(const ModuleHost&) = delete;
    ModuleHost& operator=(const ModuleHost&) = delete;
    ~ModuleHost();

    void load_all();
    void register_all();
    void init_all();
    void start_all();

    /// Runs every module's shutdown handler in parallel.
    /// \returns the identifiers of the modules whose handler did not return by \p deadline
    std::vector<std::string> shutdown_all(std::chrono::steady_clock::time_point deadline);

    /// \returns the identifiers of the modules whose ready() is still running at \p deadline
    std::vector<std::string> wait_ready_finished(std::chrono::steady_clock::time_point deadline);

private:
    struct LoadedModule {
        std::string module_id;
        std::string identifier;
        // Never dlclosed: callbacks, vtables and thread-local state of the module live in the shared object.
        void* handle{nullptr};
        std::unique_ptr<ModulePluginInstance> instance;
        std::unique_ptr<LocalModule> module;
        std::chrono::steady_clock::time_point load_start;
    };

    std::unique_ptr<LoadedModule> load(const std::string& module_id, const std::string& module_name);

    const ManagerSettings& m_ms;
    std::shared_ptr<const ManagerConfig> m_config;
    std::shared_ptr<const everest::config::ModuleConfigurations> m_module_configs;
    LocalModuleEnvironment m_env;
    std::vector<std::unique_ptr<LoadedModule>> m_modules;
};

} // namespace Everest::neo
