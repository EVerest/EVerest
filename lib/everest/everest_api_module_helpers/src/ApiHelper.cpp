// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest_api_module_helpers/ApiHelper.hpp>

#include <algorithm>
#include <exception>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>

#include <everest_api_types/entrypoint/codec.hpp>
#include <everest_api_types/generic/string.hpp>

#include <everest/logging.hpp>
#include <generated/version_information.hpp>

namespace {
inline bool ends_with(std::string_view str, std::string_view suffix) {
    return str.size() >= suffix.size() && str.substr(str.size() - suffix.size()) == suffix;
}
} // namespace

namespace everest::lib::API {

void ApiHelper::init(V1_0::types::entrypoint::CommunicationParameters const& comm_parameters) {
    init_entrypoint_API(comm_parameters);
    init_topics();
    generate_api_entrypoint_cmd_query_module();
    generate_api_entrypoint_cmd_discover();
}

void ApiHelper::init_entrypoint_API(V1_0::types::entrypoint::CommunicationParameters const& comm_parameters) {
    const auto& module_configs = config_service_client->get_module_configs();

    const std::string api_module_type_ending = "_API";
    std::vector<std::string> active_api_modules;

    for (const auto& [id, _] : module_configs) {
        // Heuristics for figuring out if this module is an API module: type ends with "_API"
        if (ends_with(id.module_type, "_API")) {
            active_api_modules.push_back(id.module_id);
        }
    }

    std::sort(active_api_modules.begin(), active_api_modules.end());
    if (active_api_modules.size() > 0 && info.id == active_api_modules[0]) {
        // each module will figure out if it happens to be the one with the
        // lowest-ordering alphabetical id if so, it shall be the one which sends
        // out a ready beacon
        responsible_for_sending_ready_beacon = true;
    }

    for (const auto& [api_type, version] : implemented_apis) {
        V1_0::types::entrypoint::ApiParameter api_params;
        api_params.type = api_type;
        api_params.module_id = info.id;
        api_params.version = version;
        api_params.communication_monitoring = comm_parameters;

        discover_response.apis.push_back(api_params);

        if (module_query_response.count(api_type) == 0) {
            module_query_response[api_type] = V1_0::types::entrypoint::ApiDiscoverResponse{};
        }
        module_query_response[api_type].apis.push_back(std::move(api_params));
    }
}

void ApiHelper::init_topics() {
    for (const auto& [api_type, version] : implemented_apis) {
        topics.setup(info.id, api_type, version);
        // Extend here to support multiple api implementations per module
        break;
    }
}

void ApiHelper::generate_api_entrypoint_cmd_discover() {
    subscribe_entrypoint_var("discover", [this](std::string const& data) {
        V1_0::types::generic::RequestReply msg;
        if (deserialize(data, msg)) {
            mqtt.publish(msg.replyTo, V1_0::types::entrypoint::serialize(discover_response));
            return true;
        }
        return false;
    });
}

void ApiHelper::generate_api_entrypoint_cmd_query_module() {
    for (const auto& [api_type, _] : implemented_apis) {
        std::stringstream topic;
        topic << "query-modules/" << api_type;

        subscribe_entrypoint_var(topic.str(), [api_type = api_type, this](std::string const& data) {
            V1_0::types::generic::RequestReply msg;
            if (deserialize(data, msg)) {
                if (module_query_response.count(api_type) > 0) {
                    mqtt.publish(msg.replyTo, V1_0::types::entrypoint::serialize(module_query_response[api_type]));
                }
                return true;
            }
            return false;
        });
    }
}

void ApiHelper::publish_ready_beacon() {
    if (responsible_for_sending_ready_beacon) {
        mqtt.publish(topics.entrypoint("ready_beacon"), std::string{"{}"});
    }
}

void ApiHelper::subscribe_latched_value_request(std::string const& var, std::string const& topic) {
    subscribe_api_topic(var + "/get", [this, topic](std::string const& data) {
        V1_0::types::generic::RequestReply msg;
        if (deserialize(data, msg)) {
            std::optional<std::string> payload;
            {
                std::lock_guard<std::mutex> lock(serialized_variables_mutex);
                auto it = serialized_variables_cache.find(topic);
                if (it != serialized_variables_cache.end()) {
                    payload = it->second;
                }
            }
            if (payload) {
                mqtt.publish(msg.replyTo, *payload);
            } else {
                EVLOG_info << "No latched value for '" << topic << "' to return";
            }
            return true;
        }
        return false;
    });
}

void ApiHelper::log_forward_api_var_error(std::string const& topic, char const* what) {
    if (what) {
        EVLOG_warning << "Variable: '" << topic << "' failed with -> " << what;
    } else {
        EVLOG_warning << "Invalid data: Cannot convert internal to external or serialize it.\n" << topic;
    }
}

void ApiHelper::publish_and_cache_variable(std::string const& topic, std::string const& payload, bool cache_value) {
    if (cache_value) {
        std::lock_guard<std::mutex> lock(serialized_variables_mutex);
        serialized_variables_cache[topic] = payload;
    }
    mqtt.publish(topic, payload);
}

void ApiHelper::subscribe_entrypoint_var(std::string const& var, ParseAndPublishFtor parse_and_publish) {
    subscribe_mqtt_topic(topics.entrypoint(var), std::move(parse_and_publish));
}

void ApiHelper::subscribe_api_topic(std::string const& topic, ParseAndPublishFtor parse_and_publish) {
    subscribe_mqtt_topic(topics.extern_to_everest(topic), std::move(parse_and_publish));
}

void ApiHelper::subscribe_mqtt_topic(std::string const& topic, ParseAndPublishFtor parse_and_publish) {
    mqtt.subscribe(topic, [topic = topic, f = std::move(parse_and_publish)](std::string const& data) {
        try {
            if (not f(data)) {
                EVLOG_warning << "Invalid data: Deserialization failed.\n" << topic << "\n" << data;
            }
        } catch (const std::exception& e) {
            EVLOG_warning << "Topic: '" << topic << "' failed with -> " << e.what() << "\n => " << data;
        } catch (...) {
            EVLOG_warning << "Invalid data: Failed to parse JSON or to get data from it.\n" << topic;
        }
    });
}

const Topics& ApiHelper::get_topics() const {
    return topics;
}

} // namespace everest::lib::API
