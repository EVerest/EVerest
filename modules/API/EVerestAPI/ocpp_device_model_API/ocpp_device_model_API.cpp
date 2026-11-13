// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "ocpp_device_model_API.hpp"
#include <everest_api_types/ocpp_device_model/API.hpp>
#include <everest_api_types/ocpp_device_model/codec.hpp>
#include <everest_api_types/ocpp_device_model/wrapper.hpp>
#include <everest_api_types/utilities/codec.hpp>

#include <chrono>

namespace module {

namespace API_types_ext = API_types::ocpp_device_model;
using ev_API::deserialize;

void ocpp_device_model_API::init() {
    collector = std::make_unique<RegistrationCollector>(info.id, config.cfg_expected_owners,
                                                        std::chrono::seconds(config.cfg_expected_owners_timeout_s),
                                                        std::chrono::seconds(config.cfg_registration_timeout_s));

    invoke_init(*p_main);
    invoke_init(*p_generic_error);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    comm_params.request_reply_timeout_s = config.cfg_request_reply_to_s;
    helper.init(comm_params);

    // get_registrations may be called before ready(): the registrations answering its prompt must not be lost
    generate_api_var_registration();
}

void ocpp_device_model_API::ready() {
    invoke_ready(*p_main);
    invoke_ready(*p_generic_error);

    generate_api_var_value_changed();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);

    helper.publish_ready_beacon();
}

void ocpp_device_model_API::shutdown() {
    collector->cancel();
    invoke_shutdown(*p_main);
    invoke_shutdown(*p_generic_error);
}

void ocpp_device_model_API::generate_api_var_registration() {
    helper.subscribe_api_topic("registration", [this](std::string const& data) {
        API_types_ext::Registration payload;
        if (not deserialize(data, payload)) {
            return false;
        }
        if (collector->add(payload)) {
            p_main->publish_registration(to_internal_api(payload));
        }
        return true;
    });
}

void ocpp_device_model_API::generate_api_var_value_changed() {
    helper.subscribe_api_topic("value_changed", [this](std::string const& data) {
        API_types_ext::ValueChange payload;
        if (not deserialize(data, payload)) {
            return false;
        }
        p_main->publish_value_changed(to_internal_api(payload));
        return true;
    });
}

} // namespace module
