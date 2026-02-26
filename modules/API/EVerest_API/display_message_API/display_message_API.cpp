// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#include "display_message_API.hpp"
#include <string>
#include <utility>
#include <everest_api_types/display_message/API.hpp>
#include <everest_api_types/display_message/codec.hpp>
#include <everest_api_types/display_message/wrapper.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>
#include <generated/types/display_message.hpp>


namespace module {

namespace API_types_ext = API_types::display_message;
namespace API_generic = API_types::generic;
using ev_API::deserialize;

void display_message_API::init() {
    invoke_init(*p_main);
    invoke_init(*p_generic_error);

    init_entrypoint_api();
    init_topics();
}

void display_message_API::ready() {
    invoke_ready(*p_main);
    invoke_ready(*p_generic_error);

    generate_api_entrypoint_cmd_query_module();
    generate_api_entrypoint_cmd_discover();
    generate_api_entrypoint_cmd_query_everest_configuration();

    comm_check.start(config.cfg_communication_check_to_s);
    setup_heartbeat_generator(comm_check, config.cfg_heartbeat_interval_ms);
}

} // namespace module
