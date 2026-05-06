// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "dc_external_derate_consumer_API.hpp"
#include "everest_api_types/dc_external_derate/API.hpp"
#include "everest_api_types/dc_external_derate/codec.hpp"
#include "everest_api_types/dc_external_derate/wrapper.hpp"
#include "everest_api_types/generic/codec.hpp"
#include "everest_api_types/generic/string.hpp"
#include "everest_api_types/utilities/codec.hpp"

namespace module {
namespace API_types_ext = API_types::dc_external_derate;
namespace API_generic = API_types::generic;
using ev_API::deserialize;

namespace {
double to_external_api(double val) {
    return val;
}
} // namespace

void dc_external_derate_consumer_API::init() {
    invoke_init(*p_generic_error);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);

    // setup var forwarding before modules start publishing
    generate_api_var_plug_temperature_C();
}

void dc_external_derate_consumer_API::ready() {
    invoke_ready(*p_generic_error);

    // setup commands now, as the target modules are ready
    generate_api_cmd_set_external_derating();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

auto dc_external_derate_consumer_API::forward_and_cache_api_var(std::string const& var) {
    return helper.forward_and_cache_api_var(var, config.latch_variable_values, [](auto const& val) {
        using namespace API_types_ext;
        using namespace API_generic;
        return serialize(to_external_api(val));
    });
}

void dc_external_derate_consumer_API::generate_api_var_plug_temperature_C() {
    r_derate->subscribe_plug_temperature_C(forward_and_cache_api_var("plug_temperature_C"));
}

void dc_external_derate_consumer_API::generate_api_cmd_set_external_derating() {
    helper.subscribe_api_topic("set_external_derating", [=](std::string const& data) {
        API_types_ext::ExternalDerating external;
        if (deserialize(data, external)) {
            r_derate->call_set_external_derating(to_internal_api(external));
            return true;
        }
        return false;
    });
}

} // namespace module
