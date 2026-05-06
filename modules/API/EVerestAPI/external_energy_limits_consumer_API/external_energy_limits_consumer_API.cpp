// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#include "external_energy_limits_consumer_API.hpp"

#include <everest_api_types/energy/API.hpp>
#include <everest_api_types/energy/codec.hpp>
#include <everest_api_types/energy/wrapper.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>

namespace module {
namespace API_types = everest::lib::API::V1_0::types;
namespace API_types_ext = API_types::energy;
namespace API_generic = API_types::generic;

using ev_API::deserialize;

void external_energy_limits_consumer_API::init() {
    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void external_energy_limits_consumer_API::ready() {
    invoke_ready(*p_main);

    generate_api_cmd_set_external_limits();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

void external_energy_limits_consumer_API::generate_api_cmd_set_external_limits() {
    helper.subscribe_api_topic("set_external_limits", [this](std::string const& data) {
        API_types_ext::ExternalLimits val;
        if (deserialize(data, val)) {
            r_energy_node->call_set_external_limits(to_internal_api(val));
            return true;
        }
        return false;
    });
}

} // namespace module
