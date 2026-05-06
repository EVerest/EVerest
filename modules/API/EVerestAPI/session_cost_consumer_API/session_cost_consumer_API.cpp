// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "session_cost_consumer_API.hpp"

#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/session_cost/API.hpp>
#include <everest_api_types/session_cost/codec.hpp>
#include <everest_api_types/session_cost/wrapper.hpp>
#include <everest_api_types/utilities/codec.hpp>

#include <everest/logging.hpp>

namespace module {

namespace API_types_ext = API_types::session_cost;
namespace API_generic = API_types::generic;
using ev_API::deserialize;

void session_cost_consumer_API::init() {
    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void session_cost_consumer_API::ready() {
    invoke_ready(*p_main);

    generate_api_var_tariff_message();
    generate_api_var_session_cost();
    generate_api_var_default_price();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

auto session_cost_consumer_API::forward_and_cache_api_var(std::string const& var) {
    using namespace API_types_ext;
    using namespace API_generic;
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
            mqtt.publish(topic, payload);
        } catch (const std::exception& e) {
            EVLOG_warning << "Variable: '" << topic << "' failed with -> " << e.what();
        } catch (...) {
            EVLOG_warning << "Invalid data: Cannot convert internal to external or serialize it.\n" << topic;
        }
    };
}

void session_cost_consumer_API::generate_api_var_tariff_message() {
    r_session_cost->subscribe_tariff_message(forward_and_cache_api_var("tariff_message"));
}

void session_cost_consumer_API::generate_api_var_session_cost() {
    r_session_cost->subscribe_session_cost(forward_and_cache_api_var("session_cost"));
}

void session_cost_consumer_API::generate_api_var_default_price() {
    r_session_cost->subscribe_default_price(forward_and_cache_api_var("default_price"));
}

} // namespace module
