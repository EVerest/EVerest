// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "powermeter_consumer_API.hpp"

#include <everest/logging.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/powermeter/API.hpp>
#include <everest_api_types/powermeter/codec.hpp>
#include <everest_api_types/powermeter/wrapper.hpp>
#include <everest_api_types/utilities/codec.hpp>

namespace module {
namespace API_types = everest::lib::API::V1_0::types;
namespace API_powermeter = API_types::powermeter;
namespace API_generic = API_types::generic;

void powermeter_consumer_API::init() {
    EVLOG_warning << "This powermeter_consumer_API module is currently experimental! Its AsyncAPI channels, "
                     "operations and message payloads may change without further notice";

    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);

    // setup var forwarding before modules start publishing
    generate_api_var_powermeter();
    generate_api_var_public_key_ocmf();
    generate_api_var_capabilities();
}

void powermeter_consumer_API::ready() {
    invoke_ready(*p_main);

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

void powermeter_consumer_API::shutdown() {
    invoke_shutdown(*p_main);
}

auto powermeter_consumer_API::forward_and_cache_api_var(std::string const& var) {
    return helper.forward_and_cache_api_var(var, config.latch_variable_values, [](auto const& val) {
        using namespace API_powermeter;
        return serialize(to_external_api(val));
    });
}

void powermeter_consumer_API::generate_api_var_powermeter() {
    r_powermeter->subscribe_powermeter(forward_and_cache_api_var("powermeter"));
}

void powermeter_consumer_API::generate_api_var_public_key_ocmf() {
    // A plain string: no conversion between the internal and the external type.
    r_powermeter->subscribe_public_key_ocmf(
        helper.forward_and_cache_api_var("public_key_ocmf", config.latch_variable_values,
                                         [](std::string const& val) { return API_generic::serialize(val); }));
}

void powermeter_consumer_API::generate_api_var_capabilities() {
    r_powermeter->subscribe_capabilities(forward_and_cache_api_var("capabilities"));
}

} // namespace module
