// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "session_storage_consumer_API.hpp"

#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/session_storage/API.hpp>
#include <everest_api_types/session_storage/codec.hpp>
#include <everest_api_types/session_storage/json_codec.hpp>
#include <everest_api_types/session_storage/wrapper.hpp>
#include <everest_api_types/utilities/codec.hpp>
#include <everest_api_types/utilities/request_reply.hpp>

#include <string>

#include <generated/types/session_storage.hpp>

namespace module {

namespace API_generic = API_types::generic;
using ev_API::deserialize;
using ev_API::deserialize_request;

void session_storage_consumer_API::init() {
    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void session_storage_consumer_API::ready() {
    invoke_ready(*p_main);

    // setup commands now, as the target modules are ready
    generate_api_cmd_get_sessions();
    generate_api_cmd_get_session();
    generate_api_cmd_clear_sessions();

    helper.generate_api_var_communication_check(&comm_check);

    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);

    helper.publish_ready_beacon();
}

void session_storage_consumer_API::shutdown() {
    invoke_shutdown(*p_main);
}

void session_storage_consumer_API::generate_api_cmd_get_sessions() {
    helper.subscribe_api_topic("get_sessions", [this](std::string const& data) {
        API_generic::RequestReply msg;
        if (not deserialize(data, msg)) {
            return false;
        }
        // an absent payload requests the first page with defaults
        types::session_storage::GetSessionsRequest request{};
        if (not msg.payload.empty()) {
            API_types_ext::GetSessionsRequest_External payload;
            if (not deserialize(msg.payload, payload)) {
                return false;
            }
            request = API_types_ext::to_internal_api(payload);
        }
        auto reply = API_types_ext::to_external_api(r_session_storage->call_get_sessions(request));
        mqtt_v.publish(msg.replyTo, serialize(reply));
        return true;
    });
}

void session_storage_consumer_API::generate_api_cmd_get_session() {
    helper.subscribe_api_topic("get_session", [this](std::string const& data) {
        std::string reply_to;
        API_types_ext::SessionIdentifier_External payload;
        if (deserialize_request(data, reply_to, payload)) {
            auto result = r_session_storage->call_get_session(API_types_ext::to_internal_api(payload));
            if (not result.session) {
                mqtt_v.publish(reply_to, std::string{"null"});
                return true;
            }
            auto reply = API_types_ext::to_external_api(result.session.value());
            mqtt_v.publish(reply_to, serialize(reply));
            return true;
        }
        return false;
    });
}

void session_storage_consumer_API::generate_api_cmd_clear_sessions() {
    helper.subscribe_api_topic("clear_sessions", [this](std::string const& data) {
        std::string reply_to;
        if (deserialize_request(data, reply_to)) {
            auto reply = API_types_ext::to_external_api(r_session_storage->call_clear_sessions());
            mqtt_v.publish(reply_to, serialize(reply));
            return true;
        }
        return false;
    });
}

} // namespace module
