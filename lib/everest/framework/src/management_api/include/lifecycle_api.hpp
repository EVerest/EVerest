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
    /// \brief Topic and payload of the "EVerest is not running" status.
    ///
    /// Registered as the MQTT last-will-testament by the manager, and published explicitly by
    /// publish_shutdown_status() when the manager shuts down. The two must stay byte-identical: a
    /// subscriber must not be able to tell a clean shutdown from an unclean death.
    struct Lwt {
        static std::string get_data();
        static std::string get_topic();

        static const ev_API::Topics topics;
    };

    /// \brief Publishes the retained "EVerest is not running" status, byte-identical to the LWT.
    ///
    /// Must be called while the MQTT connection is still up and strictly BEFORE the connection is
    /// disconnected: mosquitto's out-queue is FIFO, so this PUBLISH is written ahead of the
    /// DISCONNECT if the queue is flushed at all - and if it is not, the resulting unclean close
    /// makes the broker publish the identical LWT instead. Either way the retained status ends at
    /// everest_running == false. Publishing from a destructor (i.e. after disconnect() has been
    /// signalled) would be dropped into MQTTAbstractionImpl's pre-connect buffer instead, silently.
    ///
    /// Static on purpose: this runs during manager teardown and must not depend on a live instance.
    static void publish_shutdown_status(MQTTAbstraction& mqtt_abstraction);

    LifecycleAPI(MQTTAbstraction& mqtt_abstraction, ::Everest::config::ConfigServiceInterface& config_service,
                 ConfigurationApiStatus config_api_availability, bool readonly = true,
                 std::function<StopModulesResult()> stop_fn = {},
                 std::function<RestartModulesResult()> restart_fn = {});

    /// \brief Unregisters every MQTT handler registered by this object.
    ///
    /// Constraint: the MQTTAbstraction passed to the constructor must outlive this object, and its
    /// message-handler worker threads must already be stopped (see MQTTAbstraction::stop_message_handling())
    /// before this destructor runs. unregister_handler() only unsubscribes; it neither removes the handler
    /// from the dispatch map nor drains already-queued messages, so it alone cannot guarantee a handler stops
    /// firing. The manager stops message handling before destroying this object to establish that guarantee.
    ~LifecycleAPI();

private:
    MQTTAbstraction& m_mqtt_abstraction;
    ::Everest::config::ConfigServiceInterface& m_config_service;

    // Topic/token pairs for every handler registered via subscribe_api_topic(), kept so the
    // destructor can unregister them.
    std::vector<std::pair<std::string, Token>> m_registered_handlers;

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
