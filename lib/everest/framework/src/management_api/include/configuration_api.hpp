// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/// \file Class to implement the ConfigurationAPI, which provides an API to manage the configuration of EVerest modules.

#pragma once

#include <everest_api_types/utilities/Topics.hpp>
#include <utils/config_service_interface.hpp>
#include <utils/mqtt_abstraction.hpp>

namespace ev_API = everest::lib::API;

namespace Everest::api::configuration {

class ConfigurationAPI {
public:
    ConfigurationAPI(MQTTAbstraction& mqtt_abstraction, Everest::config::ConfigServiceInterface& config_service,
                     bool readonly = true);

    /// \brief Unregisters every MQTT handler registered by this object.
    ///
    /// Constraint: the MQTTAbstraction passed to the constructor must outlive this object, and its
    /// message-handler worker threads must already be stopped (see MQTTAbstraction::stop_message_handling())
    /// before this destructor runs. unregister_handler() only unsubscribes; it neither removes the handler
    /// from the dispatch map nor drains already-queued messages, so it alone cannot guarantee a handler stops
    /// firing. The manager stops message handling before destroying this object to establish that guarantee.
    ~ConfigurationAPI();

private:
    MQTTAbstraction& m_mqtt_abstraction;
    Everest::config::ConfigServiceInterface& m_config_service;
    ev_API::Topics m_topics;
    const bool m_readonly;

    // Topic/token pairs for every handler registered via subscribe_api_topic(), kept so the
    // destructor can unregister them.
    std::vector<std::pair<std::string, Token>> m_registered_handlers;

    void generate_api_cmd_list_all_slots();
    void generate_api_cmd_get_active_slot();
    void generate_api_cmd_mark_active_slot();

    void generate_api_cmd_delete_slot();
    void generate_api_cmd_duplicate_slot();
    void generate_api_cmd_load_from_yaml();
    void generate_api_cmd_set_description();
    void generate_api_cmd_set_config_parameters();
    void generate_api_cmd_get_config_parameters();
    void generate_api_cmd_get_configuration();

    void generate_api_var_active_slot();
    void generate_api_var_config_updates();

    using ParseAndPublishFtor = std::function<bool(std::string const&)>;
    void subscribe_api_topic(std::string const& var, ParseAndPublishFtor const& parse_and_publish);
};

} // namespace Everest::api::configuration
