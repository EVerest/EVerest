// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "auth_token_provider_API.hpp"

#include <everest/logging.hpp>
#include <everest_api_types/auth/API.hpp>
#include <everest_api_types/auth/codec.hpp>
#include <everest_api_types/auth/wrapper.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>

namespace module {

namespace API_types_ext = API_types::auth;
namespace API_generic = API_types::generic;

using ev_API::deserialize;

void auth_token_provider_API::init() {
    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void auth_token_provider_API::ready() {
    invoke_ready(*p_main);

    generate_api_var_provided_token();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

void auth_token_provider_API::generate_api_var_provided_token() {
    helper.subscribe_api_topic("provided_token", [this](const std::string& data) {
        API_types_ext::ProvidedIdToken payload;
        if (deserialize(data, payload)) {
            p_main->publish_provided_token(to_internal_api(payload));
            return true;
        }
        return false;
    });
}

} // namespace module
