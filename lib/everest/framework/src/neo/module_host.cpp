// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "module_host.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include <fmt/color.h>
#include <fmt/core.h>

#include <everest/logging.hpp>

namespace Everest::neo {

namespace {
std::int64_t elapsed_ms(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since).count();
}

fs::path plugin_path(const ManagerSettings& ms, const std::string& module_name) {
    return ms.runtime_settings.modules_dir / module_name / (module_name + ".so");
}
} // namespace

std::vector<std::string> hosted_module_ids(const ManagerConfig& config, const ManagerSettings& ms) {
    std::vector<std::string> ids;
    for (const auto& [module_id, module_config] : config.get_module_configurations()) {
        if (module_config.standalone) {
            EVLOG_warning << fmt::format("Module {} is standalone and not hosted by this process",
                                         config.printable_identifier(module_id));
        } else if (not fs::exists(plugin_path(ms, module_config.module_name))) {
            EVLOG_warning << fmt::format("Module {} has no shared object at {} and is not hosted by this process",
                                         config.printable_identifier(module_id),
                                         plugin_path(ms, module_config.module_name).string());
        } else {
            ids.push_back(module_id);
        }
    }
    return ids;
}

ModuleHost::ModuleHost(const ManagerSettings& ms, std::shared_ptr<const ManagerConfig> config,
                       std::shared_ptr<const everest::config::ModuleConfigurations> module_configs,
                       LocalModuleEnvironment environment) :
    m_ms(ms), m_config(std::move(config)), m_module_configs(std::move(module_configs)), m_env(std::move(environment)) {
}

ModuleHost::~ModuleHost() {
    for (auto it = m_modules.rbegin(); it != m_modules.rend(); ++it) {
        (*it)->module.reset();
    }
    for (auto it = m_modules.rbegin(); it != m_modules.rend(); ++it) {
        (*it)->instance.reset();
    }
}

std::unique_ptr<ModuleHost::LoadedModule> ModuleHost::load(const std::string& module_id,
                                                           const std::string& module_name) {
    auto loaded = std::make_unique<LoadedModule>();
    loaded->module_id = module_id;
    loaded->identifier = m_config->printable_identifier(module_id);
    loaded->load_start = std::chrono::steady_clock::now();

    const auto path = plugin_path(m_ms, module_name);
    loaded->handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (loaded->handle == nullptr) {
        throw std::runtime_error(
            fmt::format("Cannot load module {} from {}: {}", loaded->identifier, path.string(), dlerror()));
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): dlsym returns an untyped symbol address
    const auto entry_fn = reinterpret_cast<ModulePluginEntryFn>(dlsym(loaded->handle, MODULE_PLUGIN_ENTRY_SYMBOL));
    if (entry_fn == nullptr) {
        throw std::runtime_error(fmt::format("Module {} at {} has no {} symbol", loaded->identifier, path.string(),
                                             MODULE_PLUGIN_ENTRY_SYMBOL));
    }
    const auto* entry = entry_fn();
    if (entry == nullptr or entry->abi_version != MODULE_PLUGIN_ABI_VERSION) {
        throw std::runtime_error(fmt::format("Module {} at {} was built for plugin ABI {}, expected {}",
                                             loaded->identifier, path.string(),
                                             entry == nullptr ? 0 : entry->abi_version, MODULE_PLUGIN_ABI_VERSION));
    }
    if (entry->module_name != module_name) {
        throw std::runtime_error(fmt::format("Module {} at {} identifies itself as {}", loaded->identifier,
                                             path.string(), entry->module_name));
    }
    loaded->instance = entry->create_instance();
    if (not loaded->instance) {
        throw std::runtime_error(fmt::format("Module {} did not create an instance", loaded->identifier));
    }

    const auto module_config =
        build_local_module_config(*m_config, *m_module_configs, module_id, m_ms.runtime_settings.validate_schema);
    loaded->module = std::make_unique<LocalModule>(module_id, module_config, loaded->instance->callbacks(), m_env);

    EVLOG_info << "Module " << fmt::format(TERMINAL_STYLE_BLUE, "{}", loaded->identifier) << " loaded ["
               << elapsed_ms(loaded->load_start) << "ms]";
    return loaded;
}

void ModuleHost::load_all() {
    for (const auto& module_id : hosted_module_ids(*m_config, m_ms)) {
        m_modules.push_back(load(module_id, m_config->get_module_name(module_id)));
    }
}

void ModuleHost::register_all() {
    for (const auto& loaded : m_modules) {
        loaded->module->register_module();
    }
}

void ModuleHost::init_all() {
    for (const auto& loaded : m_modules) {
        loaded->module->init_module();
        EVLOG_info << "Module " << fmt::format(TERMINAL_STYLE_BLUE, "{}", loaded->module_id) << " initialized ["
                   << elapsed_ms(loaded->load_start) << "ms]";
    }
}

void ModuleHost::start_all() {
    for (const auto& loaded : m_modules) {
        loaded->module->start();
    }
}

std::vector<std::string> ModuleHost::shutdown_all(std::chrono::steady_clock::time_point deadline) {
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<bool> done;
    };
    auto state = std::make_shared<State>();
    state->done.resize(m_modules.size(), false);

    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < m_modules.size(); i++) {
        threads.emplace_back([state, i, module = m_modules.at(i)->module.get()] {
            module->shutdown();
            {
                const std::lock_guard<std::mutex> lock(state->mutex);
                state->done.at(i) = true;
            }
            state->cv.notify_all();
        });
    }

    std::vector<std::string> not_done;
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->cv.wait_until(lock, deadline, [&state] {
            return std::all_of(state->done.begin(), state->done.end(), [](bool done) { return done; });
        });
        for (std::size_t i = 0; i < m_modules.size(); i++) {
            if (not state->done.at(i)) {
                not_done.push_back(m_modules.at(i)->identifier);
            }
        }
    }
    for (std::size_t i = 0; i < threads.size(); i++) {
        if (not_done.empty()) {
            threads.at(i).join();
        } else {
            // the host ends the process when a shutdown handler hangs, so the thread may outlive this frame
            threads.at(i).detach();
        }
    }
    return not_done;
}

std::vector<std::string> ModuleHost::wait_ready_finished(std::chrono::steady_clock::time_point deadline) {
    std::vector<std::string> still_running;
    for (const auto& loaded : m_modules) {
        if (not loaded->module->wait_ready_finished(deadline)) {
            still_running.push_back(loaded->identifier);
        }
    }
    return still_running;
}

} // namespace Everest::neo
