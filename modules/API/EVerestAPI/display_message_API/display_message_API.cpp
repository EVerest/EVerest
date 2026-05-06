// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#include "display_message_API.hpp"
#include <everest_api_types/display_message/API.hpp>
#include <everest_api_types/display_message/codec.hpp>
#include <everest_api_types/display_message/wrapper.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>
#include <generated/types/display_message.hpp>
#include <string>
#include <utility>

namespace module {

namespace API_types_ext = API_types::display_message;
namespace API_generic = API_types::generic;
using ev_API::deserialize;

void display_message_API::init() {
    invoke_init(*p_main);
    invoke_init(*p_generic_error);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    comm_params.request_reply_timeout_s = config.cfg_request_reply_to_s;
    helper.init(comm_params);
}

void display_message_API::ready() {
    invoke_ready(*p_main);
    invoke_ready(*p_generic_error);

    helper.generate_api_var_communication_check(&comm_check);

    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);

    helper.publish_ready_beacon();
}

} // namespace module
