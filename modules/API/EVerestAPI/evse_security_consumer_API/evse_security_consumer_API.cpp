// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "evse_security_consumer_API.hpp"

#include <everest_api_types/evse_security/API.hpp>
#include <everest_api_types/evse_security/codec.hpp>
#include <everest_api_types/evse_security/wrapper.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>

#include <utility>

namespace module {

namespace API_types_ext = API_types::evse_security;
namespace API_generic = API_types::generic;
using ev_API::deserialize;

void evse_security_consumer_API::init() {
    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void evse_security_consumer_API::ready() {
    invoke_ready(*p_main);

    // setup commands now, as the target modules are ready
    generate_api_cmd_is_ca_certificate_installed();
    generate_api_cmd_get_leaf_certificate_info();
    generate_api_cmd_get_verify_location();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

void evse_security_consumer_API::generate_api_cmd_is_ca_certificate_installed() {
    helper.subscribe_api_topic("is_ca_certificate_installed", [this](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            API_types_ext::CaCertificateType payload;
            if (deserialize(msg.payload, payload)) {
                bool response = r_evse_security->call_is_ca_certificate_installed(to_internal_api(payload));
                mqtt_v.publish(msg.replyTo, response);
                if (response) {
                    mqtt_v.publish(msg.replyTo, "true");
                } else {
                    mqtt_v.publish(msg.replyTo, "false");
                }
                return true;
            }
        }
        return false;
    });
}

void evse_security_consumer_API::generate_api_cmd_get_leaf_certificate_info() {
    helper.subscribe_api_topic("get_leaf_certificate_info", [this](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            API_types_ext::GetLeafCertificateInfoRequest payload;
            if (deserialize(msg.payload, payload)) {
                auto int_res = r_evse_security->call_get_leaf_certificate_info(
                    to_internal_api(payload.certificate_type), to_internal_api(payload.encoding), payload.include_ocsp);
                auto ext_res = API_types_ext::to_external_api(int_res);
                mqtt_v.publish(msg.replyTo, serialize(ext_res));
                return true;
            }
        }
        return false;
    });
}

void evse_security_consumer_API::generate_api_cmd_get_verify_location() {
    helper.subscribe_api_topic("get_verify_location", [this](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            API_types_ext::CaCertificateType payload;
            if (deserialize(msg.payload, payload)) {
                auto response = r_evse_security->call_get_verify_location(to_internal_api(payload));
                mqtt_v.publish(msg.replyTo, response);
                return true;
            }
        }
        return false;
    });
}

} // namespace module
