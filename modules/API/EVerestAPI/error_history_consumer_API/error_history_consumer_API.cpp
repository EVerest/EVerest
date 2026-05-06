// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "error_history_consumer_API.hpp"

#include "error_wrapper.hpp"
#include <everest_api_types/error_history/API.hpp>
#include <everest_api_types/error_history/codec.hpp>
#include <everest_api_types/error_history/wrapper.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>

#include <map>
#include <utility>
#include <vector>

#include <generated/types/error_history.hpp>

namespace module {

namespace API_generic = API_types::generic;
using ev_API::deserialize;

void error_history_consumer_API::init() {
    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void error_history_consumer_API::ready() {
    invoke_ready(*p_main);

    generate_api_cmd_active_errors();
    generate_api_cmd_get_errors();
    generate_api_var_error_events();

    helper.generate_api_var_communication_check(&comm_check);

    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);

    helper.publish_ready_beacon();
}

auto error_history_consumer_API::forward_and_cache_api_var(std::string const& var) {
    using namespace API_types_ext;
    const auto topic = helper.get_topics().everest_to_extern(var);

    if (config.latch_variable_values) {
        helper.subscribe_api_topic(var + "/get", [this, topic](std::string const& data) {
            API_generic::RequestReply msg;
            if (deserialize(data, msg)) {
                if (serialized_variables_cache.count(topic) > 0) {
                    mqtt.publish(msg.replyTo, serialized_variables_cache[topic]);
                } else {
                    EVLOG_info << "No latched value for '" << topic << "' to return";
                }
                return true;
            }
            return false;
        });
    }

    return [this, topic](auto const& val) {
        try {
            auto&& external = to_external_api(val);
            auto&& payload = serialize(external);
            serialized_variables_cache[topic] = payload;
            mqtt_v.publish(topic, payload);
        } catch (const std::exception& e) {
            EVLOG_warning << "Variable: '" << topic << "' failed with -> " << e.what();
        } catch (...) {
            EVLOG_warning << "Invalid data: Cannot convert internal to external or serialize it.\n" << topic;
        }
    };
}

void error_history_consumer_API::generate_api_cmd_active_errors() {
    using namespace API_types_ext;
    helper.subscribe_api_topic("active_errors", [=](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            types::error_history::FilterArguments&& filter{};
            filter.state_filter = types::error_history::State::Active;
            auto active_errors = r_error_history->call_get_errors(std::move(filter));
            auto reply = to_external_api(active_errors);
            mqtt_v.publish(msg.replyTo, serialize(reply));
            return true;
        }
        return false;
    });
}

void error_history_consumer_API::generate_api_cmd_get_errors() {
    using namespace API_types_ext;
    helper.subscribe_api_topic("get_errors", [=](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            API_types_ext::FilterArguments_External payload;
            if (deserialize(msg.payload, payload)) {
                auto errors = r_error_history->call_get_errors(to_internal_api(payload));
                auto reply = to_external_api(errors);
                mqtt_v.publish(msg.replyTo, serialize(reply));
                return true;
            }
        }
        return false;
    });
}

void error_history_consumer_API::generate_api_var_error_events() {
    auto convert = [](auto const& ftor) {
        return [ftor](auto&& elem) { return ftor(error_converter::framework_to_internal_api(elem)); };
    };
    subscribe_global_all_errors(convert(forward_and_cache_api_var("error_raised")),
                                convert(forward_and_cache_api_var("error_cleared")));
}

} // namespace module
