// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "ocpp_consumer_API.hpp"

#include <everest_api_types/generic/API.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/ocpp/API.hpp>
#include <everest_api_types/ocpp/codec.hpp>
#include <everest_api_types/ocpp/wrapper.hpp>
#include <everest_api_types/utilities/codec.hpp>

namespace {
template <class T> T const& to_external_api(T const& val) {
    return val;
}
} // namespace

namespace module {

namespace API_types_ext = API_types::ocpp;
namespace API_generic = API_types::generic;
using ev_API::deserialize;

void ocpp_consumer_API::init() {
    invoke_init(*p_main);
    invoke_init(*p_generic_error);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    comm_params.request_reply_timeout_s = config.cfg_request_reply_to_s;
    helper.init(comm_params);

    // setup var forwarding before modules start publishing
    generate_api_var_security_event();
    generate_api_var_is_connected();
    generate_api_var_boot_notification_response();
    generate_api_var_ocpp_transaction_event();
    generate_api_var_event_data();
    generate_api_var_charging_schedules();
    generate_api_var_ocpp_message();
}

void ocpp_consumer_API::ready() {
    invoke_ready(*p_main);
    invoke_ready(*p_generic_error);

    // setup commands now, as the target modules are ready
    generate_api_cmd_data_transfer();
    generate_api_cmd_get_variables();
    generate_api_cmd_set_variables();
    generate_api_cmd_monitor_variables();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

auto ocpp_consumer_API::forward_and_cache_api_var(std::string const& var) {
    return helper.forward_and_cache_api_var(var, config.latch_variable_values, [](auto const& val) {
        using namespace API_types_ext;
        using namespace API_generic;
        return serialize(to_external_api(val));
    });
}

void ocpp_consumer_API::generate_api_cmd_data_transfer() {
    using namespace API_types_ext;
    helper.subscribe_api_topic("data_transfer_outgoing", [=](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            DataTransferRequest request;
            if (deserialize(msg.payload, request)) {
                auto int_reply = r_data_transfer->call_data_transfer(to_internal_api(request));
                auto reply = to_external_api(int_reply);
                mqtt_v.publish(msg.replyTo, serialize(reply));
                return true;
            }
        }
        return false;
    });
}

void ocpp_consumer_API::generate_api_cmd_get_variables() {
    using namespace API_types_ext;
    helper.subscribe_api_topic("get_variables", [=](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            GetVariableRequestList request;
            if (deserialize(msg.payload, request)) {
                auto int_reply = r_ocpp->call_get_variables(to_internal_api(request));
                auto reply = to_external_api(int_reply);
                mqtt_v.publish(msg.replyTo, serialize(reply));
                return true;
            }
        }
        return false;
    });
}

void ocpp_consumer_API::generate_api_cmd_set_variables() {
    using namespace API_types_ext;
    helper.subscribe_api_topic("set_variables", [=](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            SetVariablesArgs request;
            if (deserialize(msg.payload, request)) {
                auto int_reply = r_ocpp->call_set_variables(to_internal_api(request.variables), request.source);
                auto reply = to_external_api(int_reply);
                mqtt_v.publish(msg.replyTo, serialize(reply));
                return true;
            }
        }
        return false;
    });
}

void ocpp_consumer_API::generate_api_cmd_monitor_variables() {
    using namespace API_types_ext;
    helper.subscribe_api_topic("monitor_variables", [=](std::string const& data) {
        MonitorVariableRequestList request;
        if (deserialize(data, request)) {
            r_ocpp->call_monitor_variables(to_internal_api(request));
            return true;
        }
        return false;
    });
}

void ocpp_consumer_API::generate_api_var_security_event() {
    r_ocpp->subscribe_security_event(forward_and_cache_api_var("security_event"));
}

void ocpp_consumer_API::generate_api_var_is_connected() {
    r_ocpp->subscribe_is_connected(forward_and_cache_api_var("is_connected"));
}

void ocpp_consumer_API::generate_api_var_boot_notification_response() {
    r_ocpp->subscribe_boot_notification_response(forward_and_cache_api_var("boot_notification_response"));
}

void ocpp_consumer_API::generate_api_var_ocpp_transaction_event() {
    r_ocpp->subscribe_ocpp_transaction_event(forward_and_cache_api_var("ocpp_transaction_event"));
}

void ocpp_consumer_API::generate_api_var_event_data() {
    r_ocpp->subscribe_event_data(forward_and_cache_api_var("event_data"));
}

void ocpp_consumer_API::generate_api_var_charging_schedules() {
    r_ocpp->subscribe_charging_schedules(forward_and_cache_api_var("charging_schedules"));
}

void ocpp_consumer_API::generate_api_var_ocpp_message() {
    r_ocpp->subscribe_ocpp_message(forward_and_cache_api_var("ocpp_message"));
}

} // namespace module
