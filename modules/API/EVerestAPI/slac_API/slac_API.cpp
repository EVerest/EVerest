// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "slac_API.hpp"

#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/generic/string.hpp>
#include <everest_api_types/slac/API.hpp>
#include <everest_api_types/slac/codec.hpp>
#include <everest_api_types/slac/wrapper.hpp>
#include <everest_api_types/utilities/codec.hpp>

namespace module {

namespace API_types_ext = API_types::slac;
namespace API_generic = API_types::generic;
using ev_API::deserialize;

void slac_API::init() {
    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void slac_API::ready() {
    invoke_ready(*p_main);
    generate_api_var_state();
    generate_api_var_dlink_ready();
    generate_api_var_request_error_routine();
    generate_api_var_ev_mac_address();

    generate_api_var_raise_error();
    generate_api_var_clear_error();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

void slac_API::generate_api_var_state() {
    helper.subscribe_api_topic("state", [=](const std::string& data) {
        API_types_ext::State val;
        if (deserialize(data, val)) {
            p_main->publish_state(API_types_ext::to_internal_api(val));
            return true;
        }
        return false;
    });
}

void slac_API::generate_api_var_dlink_ready() {
    helper.subscribe_api_topic("dlink_ready", [=](const std::string& data) {
        bool val = false;
        if (deserialize(data, val)) {
            p_main->publish_dlink_ready(val);
            return true;
        }
        return false;
    });
}

void slac_API::generate_api_var_request_error_routine() {
    helper.subscribe_api_topic("request_error_routine", [=](const std::string& data) {
        p_main->publish_request_error_routine(nullptr);
        return true;
    });
}

void slac_API::generate_api_var_ev_mac_address() {
    helper.subscribe_api_topic("ev_mac_address", [=](const std::string& data) {
        std::string val;
        if (deserialize(data, val)) {
            p_main->publish_ev_mac_address(val);
            return true;
        }
        return false;
    });
}

void slac_API::generate_api_var_raise_error() {
    helper.subscribe_api_topic("raise_error", [=](std::string const& data) {
        API_generic::Error error;
        if (deserialize(data, error)) {
            auto sub_type_str = error.sub_type ? error.sub_type.value() : "";
            auto message_str = error.message ? error.message.value() : "";
            auto error_str = make_error_string(error);
            auto ev_error = p_main->error_factory->create_error(error_str, sub_type_str, message_str,
                                                                Everest::error::Severity::High);
            p_main->raise_error(ev_error);
            return true;
        }
        return false;
    });
}

void slac_API::generate_api_var_clear_error() {
    helper.subscribe_api_topic("clear_error", [=](std::string const& data) {
        API_generic::Error error;
        if (deserialize(data, error)) {
            std::string error_str = make_error_string(error);
            if (error.sub_type) {
                p_main->clear_error(error_str, error.sub_type.value());
            } else {
                p_main->clear_error(error_str);
            }
            return true;
        }
        return false;
    });
}

std::string slac_API::make_error_string(API_generic::Error const& error) {
    auto error_str = API_generic::trimmed(serialize(error.type));
    auto result = "generic/" + error_str;
    return result;
}

} // namespace module
