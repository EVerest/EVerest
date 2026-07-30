// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "system_API.hpp"

#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/generic/string.hpp>
#include <everest_api_types/system/API.hpp>
#include <everest_api_types/system/codec.hpp>
#include <everest_api_types/system/wrapper.hpp>
#include <everest_api_types/utilities/codec.hpp>

#include <everest/logging.hpp>

namespace module {

namespace API_types_ext = API_types::system;
namespace API_generic = API_types::generic;
using ev_API::deserialize;

void system_API::init() {
    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    comm_params.request_reply_timeout_s = config.cfg_request_reply_to_s;
    helper.init(comm_params);
}

void system_API::ready() {
    invoke_ready(*p_main);

    generate_api_var_firmware_update_status();
    generate_api_var_log_status();
    generate_api_var_configure_network_status();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

void system_API::generate_api_var_firmware_update_status() {
    helper.subscribe_api_topic("firmware_update_status", [this](std::string const& data) {
        API_types_ext::FirmwareUpdateStatus payload;
        if (deserialize(data, payload)) {
            p_main->publish_firmware_update_status(to_internal_api(payload));
            return true;
        }
        return false;
    });
}

void system_API::generate_api_var_log_status() {
    helper.subscribe_api_topic("log_status", [this](std::string const& data) {
        API_types_ext::LogStatus payload;
        if (deserialize(data, payload)) {
            p_main->publish_log_status(to_internal_api(payload));
            return true;
        }
        return false;
    });
}

void system_API::generate_api_var_configure_network_status() {
    helper.subscribe_api_topic("configure_network_status", [this](std::string const& data) {
        API_types_ext::ConfigureNetworkStatus payload;
        if (deserialize(data, payload)) {
            p_main->publish_configure_network_status(to_internal_api(payload));
            return true;
        }
        return false;
    });
}

} // namespace module
