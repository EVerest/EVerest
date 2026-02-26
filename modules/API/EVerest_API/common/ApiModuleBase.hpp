// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#ifndef API_MODULE_BASE_HPP
#define API_MODULE_BASE_HPP

#include "ld-ev.hpp"
#include <everest_api_types/entrypoint/API.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/CommCheckHandler.hpp>
#include <everest_api_types/utilities/Topics.hpp>
#include <everest_api_types/utilities/codec.hpp>
#include <functional>
#include <string>

namespace module {

namespace ev_API = everest::lib::API;
namespace API_types = ev_API::V1_0::types;
namespace API_types_entry = API_types::entrypoint;
using ev_API::deserialize;

class ApiModuleBase : public Everest::ModuleBase {
public:
    using ParseAndPublishFtor = std::function<bool(std::string const&)>;

    ApiModuleBase(const ModuleInfo& info_, Everest::MqttProvider& mqtt_,
                  std::map<std::string, size_t> const& implemented_apis_) :
        ModuleBase(info_), mqtt(mqtt_), implemented_apis(implemented_apis_) {
    }

    Everest::MqttProvider& mqtt;
    const ev_API::Topics& get_topics() const;

protected:
    // Shared state
    ev_API::Topics topics;
    size_t hb_id{0};
    API_types_entry::ApiDiscoverResponse discover_response{};
    std::map<std::string, API_types_entry::ApiDiscoverResponse> module_query_response{};
    API_types_entry::QueryEVerestConfigurationResponse config_response{};
    // API type and version:
    const std::map<std::string, size_t> implemented_apis;

    // Topic subscription helpers
    void subscribe_api_topic(std::string const& topic, ParseAndPublishFtor parse_and_publish);
    void subscribe_api_var(std::string const& var, ParseAndPublishFtor parse_and_publish);
    void subscribe_entrypoint_var(std::string const& var, ParseAndPublishFtor parse_and_publish);

    // Common initialization
    void init_entrypoint_api();
    void init_topics();
    void generate_api_entrypoint_cmd_discover();
    void generate_api_entrypoint_cmd_query_module();
    void generate_api_entrypoint_cmd_query_everest_configuration();

    // comm_check-dependent helpers — templated to stay type-safe
    template <typename CommCheckT> void generate_api_var_communication_check(CommCheckT& comm_check) {
        subscribe_api_topic("communication_check", [&comm_check](std::string const& data) {
            bool val = false;
            if (ev_API::deserialize(data, val)) {
                comm_check.set_value(val);
                return true;
            }
            return false;
        });
    }

    template <typename CommCheckT> void setup_heartbeat_generator(CommCheckT& comm_check, int interval_ms) {
        auto topic = topics.everest_to_extern("heartbeat");
        auto action = [this, topic]() {
            using namespace API_types::generic;
            mqtt.publish(topic, serialize(hb_id++));
            return true;
        };
        comm_check.heartbeat(interval_ms, action);
    }
};

} // namespace module

#endif // API_MODULE_BASE_HPP
