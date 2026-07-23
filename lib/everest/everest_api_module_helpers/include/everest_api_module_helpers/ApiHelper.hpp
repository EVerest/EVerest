// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <everest/logging.hpp>
#include <utils/config_service.hpp>

#include <everest_api_module_helpers/ValidatingMqttProxy.hpp>
#include <everest_api_types/entrypoint/API.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/CommCheckHandler.hpp>
#include <everest_api_types/utilities/MqttProviderInterface.hpp>
#include <everest_api_types/utilities/Topics.hpp>
#include <everest_api_types/utilities/codec.hpp>

namespace everest::lib::API {

inline constexpr std::string_view bridge_connection_lost_message{"Bridge to implementation connection lost"};

class ApiHelper {
public:
    using ParseAndPublishFtor = std::function<bool(std::string const&)>;

    ApiHelper(const ModuleInfo& info_, Mqtt::MqttProviderInterface& mqtt_provider_,
              std::map<std::string, size_t> implemented_apis_,
              std::shared_ptr<Everest::config::ConfigServiceClient> config_service_client_) :
        info(info_),
        mqtt(mqtt_provider_),
        implemented_apis(std::move(implemented_apis_)),
        config_service_client(std::move(config_service_client_)) {
    }

    const ModuleInfo& info;
    Mqtt::MqttProviderInterface& mqtt;

    const Topics& get_topics() const;

    // Initialization helpers (called from module::init())
    void init(V1_0::types::entrypoint::CommunicationParameters const& comm_parameters);

    void publish_ready_beacon();

    // Topic subscription helpers
    void subscribe_api_topic(std::string const& topic, ParseAndPublishFtor parse_and_publish);
    void subscribe_entrypoint_var(std::string const& var, ParseAndPublishFtor parse_and_publish);

    template <typename CommCheckT> void generate_api_var_communication_check(CommCheckT* comm_check) {
        subscribe_api_topic("communication_check", [comm_check](std::string const& data) {
            bool val = false;
            if (deserialize(data, val)) {
                comm_check->set_value(val);
                return true;
            }
            return false;
        });
    }

    // Returns a callback suitable for r_xxx->subscribe_*(). serialize_fn converts the internal
    // type to its external representation and serializes it; it is supplied by the module
    // because the to_external_api/serialize overloads are only visible there.
    template <typename SerializeFn>
    auto forward_and_cache_api_var(std::string const& var, bool latch_value, SerializeFn serialize_fn) {
        auto topic = topics.everest_to_extern(var);
        if (latch_value) {
            subscribe_latched_value_request(var, topic);
        }
        return [this, topic = std::move(topic), serialize_fn = std::move(serialize_fn), latch_value](auto const& val) {
            try {
                publish_and_cache_variable(topic, serialize_fn(val), latch_value);
            } catch (const std::exception& e) {
                log_forward_api_var_error(topic, e.what());
            } catch (...) {
                log_forward_api_var_error(topic, nullptr);
            }
        };
    }

    template <typename CommCheckT> void setup_heartbeat_generator(CommCheckT* comm_check, int interval_ms) {
        auto topic = topics.everest_to_extern("heartbeat");
        auto action = [this, topic]() {
            using V1_0::types::generic::serialize;
            mqtt.publish(topic, serialize(hb_id++));
            return true;
        };
        comm_check->heartbeat(interval_ms, action);
    }

private:
    void subscribe_latched_value_request(std::string const& var, std::string const& topic);
    void publish_and_cache_variable(std::string const& topic, std::string const& payload, bool cache_value);
    void log_forward_api_var_error(std::string const& topic, char const* what);
    void subscribe_mqtt_topic(std::string const& topic, ParseAndPublishFtor parse_and_publish);
    void init_entrypoint_API(V1_0::types::entrypoint::CommunicationParameters const& comm_parameters);
    void init_topics();
    void generate_api_entrypoint_cmd_discover();
    void generate_api_entrypoint_cmd_query_module();

    Topics topics;
    std::map<std::string, std::string> serialized_variables_cache;
    std::mutex serialized_variables_mutex;
    std::atomic<size_t> hb_id{0};
    V1_0::types::entrypoint::ApiDiscoverResponse discover_response{};
    std::map<std::string, V1_0::types::entrypoint::ApiDiscoverResponse> module_query_response{};
    std::map<std::string, size_t> implemented_apis;
    std::atomic<bool> responsible_for_sending_ready_beacon{false};
    std::shared_ptr<Everest::config::ConfigServiceClient> config_service_client;
};

} // namespace everest::lib::API
