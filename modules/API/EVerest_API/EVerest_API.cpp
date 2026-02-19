// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "EVerest_API.hpp"

#include <everest_api_types/entrypoint/codec.hpp>
#include <everest_api_types/entrypoint/json_codec.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>
#include <utils/yaml_loader.hpp>

#include <map>
#include <string>
#include <utility>

namespace {

struct ApiModuleParameter {
    std::string api_type{};
    std::optional<int> api_version{};
};

std::map<std::string, ApiModuleParameter>
get_all_ApiModuleParameters(Everest::config::ConfigServiceClient* config_service_client,
                            const std::filesystem::path& api_details_filepath) {
    std::map<std::string, ApiModuleParameter> api_modules;

    json schema_data;
    try {
        schema_data = Everest::load_yaml(api_details_filepath);
    } catch (const std::exception& err) {
        EVLOG_error << "Error parsing YAML file at " << api_details_filepath << ": " << err.what();
        return {};
    }

    const auto& module_configs = config_service_client->get_module_configs();

    for (const auto& [config, _] : module_configs) {
        if (schema_data.contains(config.module_type)) {
            ApiModuleParameter module_parameters;
            module_parameters.api_type = config.module_type;

            // No mechanism to retreive the version yet

            api_modules.emplace(config.module_id, module_parameters);
        }
    }

    return api_modules;
}

std::string strip_suffix(const std::string& input, const std::string& suffix) {
    if (input.size() >= suffix.size() && input.substr(input.size() - suffix.size()) == suffix) {
        return input.substr(0, input.size() - suffix.size());
    }
    return input;
}

API_types_ext::ApiDiscoverResponse
module_config_to_ApiDiscoverResponse(const std::map<std::string, ApiModuleParameter>& api_modules) {
    API_types_ext::ApiDiscoverResponse response;

    for (const auto& [name, parameter] : api_modules) {
        API_types_ext::ApiTypeEnum type_as_enum;
        json api_type_j = strip_suffix(parameter.api_type, "_API");
        if (not ev_API::deserialize(api_type_j.dump(), type_as_enum)) {
            EVLOG_error << "Could not deserialize API type for '" << parameter.api_type << "'";
        }
        API_types_ext::ApiParameter api_params;
        api_params.type = type_as_enum;
        api_params.module_id = name;
        api_params.version = parameter.api_version;
        response.apis.emplace_back(api_params);
    }

    return response;
}
} // namespace

namespace module {

namespace API_generic = API_types::generic;
using ev_API::deserialize;

void EVerest_API::init() {
    invoke_init(*p_main);

    topics.setup(info.id, "entrypoint", 1);
}

void EVerest_API::ready() {
    invoke_ready(*p_main);

    auto config_service_client = get_config_service_client();

    const auto apis_details_path = info.paths.share / "apis.yaml";

    auto api_modules = get_all_ApiModuleParameters(config_service_client.get(), apis_details_path);

    api_discovery_response = module_config_to_ApiDiscoverResponse(api_modules);

    generate_api_cmd_discover();

    // Not calling start on comm_check - we only use the heartbeat functionality here
    setup_heartbeat_generator();
}

void EVerest_API::generate_api_cmd_discover() {
    subscribe_api_topic("discover", [this](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            mqtt.publish(msg.replyTo, API_types_ext::serialize(api_discovery_response));
            return true;
        }
        return false;
    });
}

void EVerest_API::subscribe_api_topic(std::string const& var, ParseAndPublishFtor const& parse_and_publish) {
    auto topic = topics.extern_to_everest(var);
    mqtt.subscribe(topic, [=](std::string const& data) {
        try {
            if (not parse_and_publish(data)) {
                EVLOG_warning << "Invalid data: Deserialization failed.\n" << topic << "\n" << data;
            }
        } catch (const std::exception& e) {
            EVLOG_warning << "Topic: '" << topic << "' failed with -> " << e.what() << "\n => " << data;
        } catch (...) {
            EVLOG_warning << "Invalid data: Failed to parse JSON or to get data from it.\n" << topic;
        }
    });
}

void EVerest_API::setup_heartbeat_generator() {
    auto topic = topics.everest_to_extern("heartbeat");
    auto action = [this, topic]() {
        mqtt.publish(topic, API_generic::serialize(hb_id++));
        return true;
    };
    comm_check.heartbeat(config.cfg_heartbeat_interval_ms, action);
}

} // namespace module
