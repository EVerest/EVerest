// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "powermeter_API.hpp"

#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/generic/string.hpp>
#include <everest_api_types/powermeter/API.hpp>
#include <everest_api_types/powermeter/codec.hpp>
#include <everest_api_types/powermeter/wrapper.hpp>
#include <everest_api_types/utilities/codec.hpp>

#include <utility>

#include <everest/logging.hpp>

namespace module {

namespace API_types_ext = API_types::powermeter;
namespace API_generic = API_types::generic;
using ev_API::deserialize;

void powermeter_API::init() {
    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void powermeter_API::ready() {
    invoke_ready(*p_main);

    generate_api_var_powermeter_values();
    generate_api_var_public_key_ocmf();
    generate_api_var_capabilities();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

void powermeter_API::generate_api_var_powermeter_values() {
    helper.subscribe_api_topic("powermeter_values", [this](std::string const& data) {
        API_types_ext::PowermeterValues payload;
        if (deserialize(data, payload)) {
            p_main->publish_powermeter(to_internal_api(payload));
            return true;
        }
        return false;
    });
}

void powermeter_API::generate_api_var_capabilities() {
    helper.subscribe_api_topic("capabilities", [this](std::string const& data) {
        API_types_ext::Capabilities payload;
        if (deserialize(data, payload)) {
            p_main->publish_capabilities(to_internal_api(payload));
            return true;
        }
        return false;
    });
}

void powermeter_API::generate_api_var_public_key_ocmf() {
    helper.subscribe_api_topic("public_key_ocmf", [this](std::string const& data) {
        std::string val;
        if (deserialize(data, val)) {
            p_main->publish_public_key_ocmf(std::move(val));
            return true;
        }
        return false;
    });
}

} // namespace module
