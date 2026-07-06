// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/// \file Class to implement the LifecycleAPI, which provides an API to manage the lifecycle of EVerest modules

#pragma once

#include <everest_api_types/utilities/Topics.hpp>
#include <utils/config_service_interface.hpp>
#include <utils/mqtt_abstraction.hpp>

namespace ev_API = everest::lib::API;

namespace everest::lib::API::V1_0::types::lifecycle {
enum class ModuleExecutionStatusEnum;
}

namespace Everest::api::lifecycle {

enum class ConfigurationApiStatus {
    NotAvailable,
    AvailableRO,
    AvailableRW,
};

enum class StopModulesResult {
    Stopping,
    NoModulesToStop,
    Rejected
};

enum class RestartModulesResult {
    Starting,
    Restarting,
    NoConfigToStart,
    Rejected
};

class LifecycleAPI {
public:
    struct Lwt {
        static std::string get_data();
        static std::string get_topic();

        static const ev_API::Topics topics;
    };

    LifecycleAPI(MQTTAbstraction& mqtt_abstraction, ::Everest::config::ConfigServiceInterface& config_service,
                 ConfigurationApiStatus config_api_availability, bool readonly = true,
                 std::function<StopModulesResult()> stop_fn = {},
                 std::function<RestartModulesResult()> restart_fn = {});

private:
    MQTTAbstraction& m_mqtt_abstraction;
    ::Everest::config::ConfigServiceInterface& m_config_service;

    ConfigurationApiStatus m_config_api_availability;
    const bool m_readonly;
    ::Everest::config::ActiveSlotStatus m_last_module_status{::Everest::config::ActiveSlotStatus::Stopped};

    std::function<StopModulesResult()> stop_fn_;
    std::function<RestartModulesResult()> restart_fn_;

    StopModulesResult stop_modules();
    RestartModulesResult restart_modules();

    void generate_api_cmd_stop_modules();
    void generate_api_cmd_start_modules();
    void generate_api_cmd_get_everest_version();

    void generate_api_var_status();

    void publish_execution_status(const std::string& tstamp,
                                  everest::lib::API::V1_0::types::lifecycle::ModuleExecutionStatusEnum module_status);

    using ParseAndPublishFtor = std::function<bool(std::string const&)>;
    void subscribe_api_topic(std::string const& var, ParseAndPublishFtor const& parse_and_publish);
};

} // namespace Everest::api::lifecycle
