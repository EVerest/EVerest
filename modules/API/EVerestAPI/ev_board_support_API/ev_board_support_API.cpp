// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "ev_board_support_API.hpp"

#include <everest_api_types/ev_board_support/codec.hpp>
#include <everest_api_types/ev_board_support/wrapper.hpp>
#include <everest_api_types/evse_board_support/codec.hpp>
#include <everest_api_types/evse_board_support/wrapper.hpp>
#include <everest_api_types/evse_manager/codec.hpp>
#include <everest_api_types/evse_manager/wrapper.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/generic/string.hpp>
#include <everest_api_types/utilities/codec.hpp>

#include "everest_api_types/ev_board_support/API.hpp"
#include "utils/error.hpp"

namespace module {

namespace API_generic = API_types::generic;
using ev_API::deserialize;

void ev_board_support_API::init() {
    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void ev_board_support_API::ready() {
    invoke_ready(*p_main);

    generate_api_var_bsp_event();
    generate_api_var_bsp_measurement();
    generate_api_var_ev_info();

    generate_api_var_raise_error();
    generate_api_var_clear_error();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

void ev_board_support_API::generate_api_var_bsp_event() {
    helper.subscribe_api_topic("bsp_event", [=](std::string const& data) {
        API_types::evse_board_support::BspEvent ext;
        if (deserialize(data, ext)) {
            p_main->publish_bsp_event(to_internal_api(ext));
            return true;
        }
        return false;
    });
}

void ev_board_support_API::generate_api_var_bsp_measurement() {
    helper.subscribe_api_topic("bsp_measurement", [=](std::string const& data) {
        API_types::ev_board_support::BspMeasurement ext;
        if (deserialize(data, ext)) {
            p_main->publish_bsp_measurement(to_internal_api(ext));
            return true;
        }
        return false;
    });
}

void ev_board_support_API::generate_api_var_ev_info() {
    helper.subscribe_api_topic("ev_info", [=](std::string const& data) {
        API_types::evse_manager::EVInfo ext;
        if (deserialize(data, ext)) {
            p_main->publish_ev_info(to_internal_api(ext));
            return true;
        }
        return false;
    });
}

void ev_board_support_API::generate_api_var_raise_error() {
    helper.subscribe_api_topic("raise_error", [=](std::string const& data) {
        API_types::generic::Error error;
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

void ev_board_support_API::generate_api_var_clear_error() {
    helper.subscribe_api_topic("clear_error", [=](std::string const& data) {
        API_types::generic::Error error;
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

std::string ev_board_support_API::make_error_string(API_types::generic::Error const& error) {
    auto error_str = API_generic::trimmed(serialize(error.type));
    auto result = "generic/" + error_str;
    return result;
}

} // namespace module
