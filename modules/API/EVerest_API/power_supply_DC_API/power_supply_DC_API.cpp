// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "power_supply_DC_API.hpp"

#include <string>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/generic/string.hpp>
#include <everest_api_types/power_supply_DC/API.hpp>
#include <everest_api_types/power_supply_DC/codec.hpp>
#include <everest_api_types/power_supply_DC/wrapper.hpp>
#include <everest_api_types/utilities/Topics.hpp>
#include <everest_api_types/utilities/codec.hpp>

#include "utils/error.hpp"

namespace module {

namespace API_generic = API_types::generic;
using ev_API::deserialize;

void power_supply_DC_API::init() {
    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    init_entrypoint_api(comm_params);
    init_topics();
}

void power_supply_DC_API::ready() {
    invoke_ready(*p_main);

    generate_api_var_mode();
    generate_api_var_voltage_current();
    generate_api_var_capabilities();

    generate_api_var_raise_error();
    generate_api_var_clear_error();

    generate_api_entrypoint_cmd_query_module();
    generate_api_entrypoint_cmd_discover();
    generate_api_entrypoint_cmd_query_everest_configuration();

    generate_api_var_communication_check(comm_check);

    comm_check.start(config.cfg_communication_check_to_s);
    setup_heartbeat_generator(comm_check, config.cfg_heartbeat_interval_ms);
}

void power_supply_DC_API::generate_api_var_mode() {
    subscribe_api_topic("mode", [this](const std::string& data) {
        API_types_ext::Mode payload;
        if (deserialize(data, payload)) {
            p_main->publish_mode(to_internal_api(payload));
            return true;
        }
        return false;
    });
}

void power_supply_DC_API::generate_api_var_voltage_current() {
    subscribe_api_topic("voltage_current", [this](const std::string& data) {
        API_types_ext::VoltageCurrent payload;
        if (deserialize(data, payload)) {
            p_main->publish_voltage_current(to_internal_api(payload));
            return true;
        }
        return false;
    });
}

void power_supply_DC_API::generate_api_var_capabilities() {
    subscribe_api_topic("capabilities", [this](const std::string& data) {
        API_types_ext::Capabilities payload;
        if (deserialize(data, payload)) {
            p_main->publish_capabilities(to_internal_api(payload));
            return true;
        }
        return false;
    });
}

void power_supply_DC_API::generate_api_var_raise_error() {
    subscribe_api_topic("raise_error", [this](const std::string& data) {
        API_types_ext::Error payload;
        if (deserialize(data, payload)) {
            auto sub_type_str = payload.sub_type ? payload.sub_type.value() : "";
            auto message_str = payload.message ? payload.message.value() : "";
            auto error_str = make_error_string(payload);
            auto ev_error = p_main->error_factory->create_error(error_str, sub_type_str, message_str,
                                                                Everest::error::Severity::High);
            p_main->raise_error(ev_error);
            return true;
        }
        return false;
    });
}

void power_supply_DC_API::generate_api_var_clear_error() {
    subscribe_api_topic("clear_error", [this](const std::string& data) {
        API_types_ext::Error payload;
        if (deserialize(data, payload)) {
            std::string error_str = make_error_string(payload);
            if (payload.sub_type) {
                p_main->clear_error(error_str, payload.sub_type.value());
            } else {
                p_main->clear_error(error_str);
            }
            return true;
        }
        return false;
    });
}

std::string power_supply_DC_API::make_error_string(API_types_ext::Error const& error) {
    auto error_str = API_generic::trimmed(serialize(error.type));
    auto result = "power_supply_DC/" + error_str;
    return result;
}

} // namespace module
