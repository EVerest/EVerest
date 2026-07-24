// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "external_energy_node_client_API.hpp"

namespace module {

void external_energy_node_client_API::init() {
    invoke_init(*p_main);
    invoke_init(*p_energy_grid);

    // Initialise ApiHelper — registers heartbeat and communication-check parameters
    // for this client's own API identity (external_energy_node_client).
    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void external_energy_node_client_API::ready() {
    invoke_ready(*p_main);
    invoke_ready(*p_energy_grid);

    helper.generate_api_var_communication_check(&comm_check);

    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);

    helper.publish_ready_beacon();
}

} // namespace module
