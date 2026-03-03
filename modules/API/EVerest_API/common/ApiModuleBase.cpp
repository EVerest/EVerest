// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "ApiModuleBase.hpp"

#include <sstream>
#include <string>

#include <everest_api_types/entrypoint/codec.hpp>
#include <everest_api_types/generic/string.hpp>

#include <generated/version_information.hpp>
#include "utils/error.hpp"
#include <everest/logging.hpp>

// TODO(CB): Wrap mqtt.publish to not call it when the topic is empty

namespace module {

namespace API_generic = API_types::generic;
using ev_API::deserialize;

void ApiModuleBase::init_topics() {
    // TODO(CB): init_entrypoint_response can handle multiple interface implementations in one module
    //           but there is only one topics instance stored in the ApiModuleBase class now
    for (const auto& [api_type, version] : implemented_apis) {
        topics.setup(info.id, api_type, version);
        // TODO(CB): Stops after setting up the first api topic - change to support multiple
        break;
    }
}

void ApiModuleBase::init_entrypoint_api(API_types_entry::CommunicationParameters const& comm_parameters) {
    auto config_service_client = get_config_service_client();

    const auto& module_configs = config_service_client->get_module_configs();
    for (const auto& [id, _] : module_configs) {
        API_types_entry::ModuleParameter parameter{};
        parameter.module_id = id.module_id;
        parameter.name = id.module_type;
        config_response.modules.push_back(std::move(parameter));
    }

    config_response.version.name = PROJECT_NAME;
    config_response.version.version = PROJECT_VERSION;
    config_response.version.git_version = GIT_VERSION;

    // TODO(CB): Need to enumerate the API instances in each module (for modules which implement multiple APIs)
    // TODO(CB): Also need to explain this in the AsyncAPI definition
    for (const auto& [api_type, version] : implemented_apis) {
        API_types_entry::ApiParameter api_params;
        api_params.type = api_type;
        api_params.module_id = info.id;
        api_params.version = version;
        api_params.communication_monitoring = comm_parameters;
        discover_response.apis.push_back(api_params);

        if (module_query_response.count(api_type) == 0) {
            module_query_response[api_type] = API_types_entry::ApiDiscoverResponse{};
        }
        module_query_response[api_type].apis.push_back(std::move(api_params));
    }
    // TODO(CB): Still missing to fill api_params.associated_module from required by this module
}

void ApiModuleBase::generate_api_entrypoint_cmd_discover() {
    subscribe_entrypoint_var("discover", [this](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            mqtt.publish(msg.replyTo, API_types_entry::serialize(discover_response));
            return true;
        }
        return false;
    });
}

void ApiModuleBase::generate_api_entrypoint_cmd_query_module() {
    for (const auto& [api_type, _] : implemented_apis) {
        std::stringstream topic;
        topic << "query-modules/" << api_type;

        subscribe_entrypoint_var(topic.str(), [api_type = api_type, this](std::string const& data) {
            API_generic::RequestReply msg;
            if (deserialize(data, msg)) {
                if (module_query_response.count(api_type) > 0) {
                    mqtt.publish(msg.replyTo, API_types_entry::serialize(module_query_response[api_type]));
                }
                return true;
            }
            return false;
        });
    }
}

void ApiModuleBase::generate_api_entrypoint_cmd_query_everest_configuration() {
    std::stringstream topic;
    topic << "query-everest-configuration/" << this->info.id;
    subscribe_entrypoint_var(topic.str(), [this](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            mqtt.publish(msg.replyTo, API_types_entry::serialize(config_response));
            return true;
        }
        return false;
    });
}

void ApiModuleBase::subscribe_api_var(std::string const& var, ParseAndPublishFtor parse_and_publish) {
    subscribe_api_topic(topics.extern_to_everest(var), std::move(parse_and_publish));
}

void ApiModuleBase::subscribe_entrypoint_var(std::string const& var, ParseAndPublishFtor parse_and_publish) {
    subscribe_api_topic(topics.entrypoint(var), std::move(parse_and_publish));
}

void ApiModuleBase::subscribe_api_topic(std::string const& topic, ParseAndPublishFtor parse_and_publish) {
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

const ev_API::Topics& ApiModuleBase::get_topics() const {
    return topics;
}

} // namespace module