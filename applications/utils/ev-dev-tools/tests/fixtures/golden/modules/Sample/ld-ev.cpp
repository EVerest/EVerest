// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// AUTO GENERATED - DO NOT EDIT!
// template version 6
//

#include "ld-ev.hpp"

#ifdef EVEREST_COVERAGE_ENABLED
#include <everest/helpers/coverage.hpp>
#endif

#include "Sample.hpp"
#include "main/sample_sourceImpl.hpp"

#include <framework/runtime.hpp>
#include <utils/types.hpp>

#include <everest/project_info.hpp>

#include <utils/error/error_manager_req_global.hpp>

namespace module {

// FIXME (aw): could this way of keeping static variables be changed somehow?
static Everest::ModuleAdapter adapter {};
static Everest::PtrContainer<Sample> mod_ptr {};

// per module configs
static main::RwConf main_rwconfig;
static main::Conf main_config(main_rwconfig);
// pointer to the concrete implementation, set in everest_register(), so config change
// handlers can be registered without casting the module's base class pointer
static main::sample_sourceImpl* main_impl_p{nullptr};
static RwConf module_rwconf;
static Conf module_conf(module_rwconf);
static ModuleInfo module_info;

void subscribe_global_all_errors(
    const Everest::error::ErrorCallback& callback,
    const Everest::error::ErrorCallback& clear_callback
) {
    adapter.get_global_error_manager()->subscribe_global_all_errors(callback, clear_callback);
}

std::shared_ptr<Everest::error::ErrorStateMonitor> get_global_error_state_monitor() {
    return adapter.get_global_error_state_monitor();
}


std::shared_ptr<Everest::config::ConfigServiceClient> get_config_service_client() {
    return adapter.get_config_service_client();
}

void LdEverest::init(ModuleConfigs module_configs, const ModuleInfo& mod_info) {
    using namespace Everest::config;

    EVLOG_debug << "init() called on module Sample";

    // populate config for provided implementations
    auto main_config_input = std::move(module_configs["main"]);
    main_config.impl_setting = std::get<bool>(main_config_input["impl_setting"]);
    main_rwconfig.impl_changeable = std::get<double>(main_config_input["impl_changeable"]);

    module_conf.read_only_setting = std::get<std::string>(module_configs["!module"]["read_only_setting"]);
    module_rwconf.changeable_number = std::get<int>(module_configs["!module"]["changeable_number"]);
    module_rwconf.changeable_text = std::get<std::string>(module_configs["!module"]["changeable_text"]);

    module_info = mod_info;

    // register runtime config change handlers for rw parameters for implementations
    auto config_client = adapter.get_config_service_client();
    if (config_client) {
        config_client->register_config_change_handler("main", "impl_changeable",
            [](const std::string& new_value) -> Everest::config::ConfigChangeResult {
                const auto typed_value = Everest::config::conversions::ConfigFromString<double>(new_value);
                return main_impl_p->on_impl_changeable_changed(typed_value);
            });
    }
    if (config_client) {
        config_client->register_config_change_handler("!module", "changeable_number",
            [](const std::string& new_value) -> Everest::config::ConfigChangeResult {
                const auto typed_value = Everest::config::conversions::ConfigFromString<int>(new_value);
                return mod_ptr->on_changeable_number_changed(typed_value);
            });
        config_client->register_config_change_handler("!module", "changeable_text",
            [](const std::string& new_value) -> Everest::config::ConfigChangeResult {
                return mod_ptr->on_changeable_text_changed(new_value);
            });
    }

    mod_ptr->init();
}

void LdEverest::ready() {
    EVLOG_debug << "ready() called on module Sample";
    mod_ptr->ready();
}

void LdEverest::shutdown() {
    EVLOG_debug << "shutdown() called on module Sample";
    mod_ptr->shutdown();
}

void register_module_adapter(Everest::ModuleAdapter module_adapter) {
    adapter = std::move(module_adapter);
}

std::vector<Everest::cmd> everest_register(const RequirementInitialization& requirement_init) {
    EVLOG_debug << "everest_register() called on module Sample";

    adapter.check_complete();

    auto p_main = std::make_unique<main::sample_sourceImpl>(&adapter, mod_ptr, main_config, main_rwconfig);
    main_impl_p = p_main.get();
    adapter.gather_cmds(*p_main);

    std::string r_one_source_requirement_module_id;
    Requirement r_one_source_requirement;
    std::optional<Mapping> r_one_source_mapping;
    if (auto it = requirement_init.find("one_source"); it != requirement_init.end()) {
        auto requirement_initializer = (*it).second;
        if (requirement_initializer.size() > 0) {
            r_one_source_requirement_module_id = requirement_initializer.at(0).fulfillment.module_id;
            r_one_source_requirement = requirement_initializer.at(0).requirement;
            r_one_source_mapping = requirement_initializer.at(0).mapping;
        }
    }
    auto r_one_source = std::make_unique<sample_sourceIntf>(&adapter, r_one_source_requirement, r_one_source_requirement_module_id, r_one_source_mapping);
    auto r_many_sources = std::vector<std::unique_ptr<sample_sourceIntf>>();
    if (auto it = requirement_init.find("many_sources"); it != requirement_init.end()) {
        for (const auto& requirement_initializer : (*it).second) {
            auto requirement_module_id = requirement_initializer.fulfillment.module_id;
            auto requirement = requirement_initializer.requirement;
            auto mapping = requirement_initializer.mapping;
            r_many_sources.emplace_back(std::make_unique<sample_sourceIntf>(&adapter, requirement, requirement_module_id, mapping));
        }
    }

    static Everest::MqttProvider mqtt_provider(adapter);
    static Everest::TelemetryProvider telemetry_provider(adapter);

    static Sample module(
    module_info,mqtt_provider, telemetry_provider, std::move(p_main), std::move(r_one_source), std::move(r_many_sources),     module_conf, module_rwconf);

    mod_ptr.set(&module);

    return adapter.registered_commands;
}

} // namespace module

#ifndef LD_EV_EXCLUDE_MAIN
int main(int argc, char* argv[]) {
#ifdef EVEREST_COVERAGE_ENABLED
    everest::helpers::install_signal_handlers_for_gcov();
#endif

    auto module_loader = Everest::ModuleLoader(argc, argv, Everest::ModuleCallbacks(
        module::register_module_adapter, module::everest_register,
        module::LdEverest::init, module::LdEverest::ready, module::LdEverest::shutdown),
        {std::string{everest::project_info::name()}, std::string{everest::project_info::version()},
         std::string{everest::project_info::git_commit()}});

    return module_loader.initialize();
}
#endif
