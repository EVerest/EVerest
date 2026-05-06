// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "session_cost_API.hpp"

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

void session_cost_API::init() {
    invoke_init(*p_main);
    invoke_init(*p_generic_error);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void session_cost_API::ready() {
    invoke_ready(*p_main);
    invoke_ready(*p_generic_error);

    generate_api_var_tariff_message();
    generate_api_var_session_cost();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

void session_cost_API::generate_api_var_tariff_message() {
    helper.subscribe_api_topic("tariff_message", [=](const std::string& data) {
        API_types_ext::TariffMessage payload;
        if (deserialize(data, payload)) {
            p_main->publish_tariff_message(to_internal_api(payload));
            return true;
        }
        return false;
    });
}

void session_cost_API::generate_api_var_session_cost() {
    helper.subscribe_api_topic("session_cost", [=](const std::string& data) {
        API_types_ext::SessionCost payload;
        if (deserialize(data, payload)) {
            p_main->publish_session_cost(to_internal_api(payload));
            return true;
        }
        return false;
    });
}

} // namespace module
