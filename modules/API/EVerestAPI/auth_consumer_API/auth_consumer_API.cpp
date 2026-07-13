// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "auth_consumer_API.hpp"

#include <everest_api_types/auth/API.hpp>
#include <everest_api_types/auth/codec.hpp>
#include <everest_api_types/auth/wrapper.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>

#include <utility>

namespace module {

namespace API_types_ext = API_types::auth;
namespace API_generic = API_types::generic;
using ev_API::deserialize;

void auth_consumer_API::init() {
    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void auth_consumer_API::ready() {
    invoke_ready(*p_main);

    generate_api_cmd_withdraw_authorization();
    generate_api_var_token_validation_status();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

auto auth_consumer_API::forward_and_cache_api_var(std::string const& var) {
    return helper.forward_and_cache_api_var(var, config.latch_variable_values, [](auto const& val) {
        using namespace API_types_ext;
        return serialize(to_external_api(val));
    });
}

void auth_consumer_API::generate_api_cmd_withdraw_authorization() {
    helper.subscribe_api_topic("withdraw_authorization", [this](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            API_types_ext::WithdrawAuthorizationRequest payload;
            if (deserialize(msg.payload, payload)) {
                auto int_res = r_auth->call_withdraw_authorization(to_internal_api(payload));
                auto ext_res = API_types_ext::to_external_api(int_res);
                mqtt_v.publish(msg.replyTo, serialize(ext_res));
                return true;
            }
        }
        return false;
    });
}

void auth_consumer_API::generate_api_var_token_validation_status() {
    r_auth->subscribe_token_validation_status(forward_and_cache_api_var("token_validation_status"));
}

} // namespace module
