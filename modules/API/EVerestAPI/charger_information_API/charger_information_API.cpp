// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "charger_information_API.hpp"

namespace module {

void charger_information_API::init() {
    invoke_init(*p_main);
    invoke_init(*p_generic_error);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    comm_params.request_reply_timeout_s = config.cfg_request_reply_to_s;
    helper.init(comm_params);
}

void charger_information_API::ready() {
    invoke_ready(*p_main);
    invoke_ready(*p_generic_error);

    helper.generate_api_var_communication_check(&comm_check);

    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);

    helper.publish_ready_beacon();
}

void charger_information_API::shutdown() {
    invoke_shutdown(*p_main);
    invoke_shutdown(*p_generic_error);
}

} // namespace module
