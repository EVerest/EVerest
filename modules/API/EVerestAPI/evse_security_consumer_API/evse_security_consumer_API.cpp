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

namespace API_types = ev_API::V1_0::types;
namespace API_types_ext = API_types::evse_security;
namespace API_generic = API_types::generic;
using ev_API::deserialize;

void evse_security_consumer_API::init() {
    invoke_init(*p_main);

    topics.setup(info.id, "evse_security_consumer", 1);
}

void evse_security_consumer_API::ready() {
    invoke_ready(*p_main);

    generate_api_cmd_is_ca_certificate_installed();
    generate_api_cmd_get_leaf_certificate_info();
    generate_api_cmd_get_verify_location();

    generate_api_var_communication_check();

    comm_check.start(config.cfg_communication_check_to_s);
    setup_heartbeat_generator();
}

void evse_security_consumer_API::generate_api_cmd_is_ca_certificate_installed() {
    subscribe_api_topic("is_ca_certificate_installed", [this](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            API_types_ext::CaCertificateType payload;
            if (deserialize(msg.payload, payload)) {
                bool response = r_evse_security->call_is_ca_certificate_installed(to_internal_api(payload));
                mqtt.publish(msg.replyTo, response);
                if (response) {
                    mqtt.publish(msg.replyTo, "true");
                } else {
                    mqtt.publish(msg.replyTo, "false");
                }
                return true;
            }
        }
        return false;
    });
}

void evse_security_consumer_API::generate_api_cmd_get_leaf_certificate_info() {
    subscribe_api_topic("get_leaf_certificate_info", [this](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            API_types_ext::GetLeafCertificateInfoRequest payload;
            if (deserialize(msg.payload, payload)) {
                auto int_res = r_evse_security->call_get_leaf_certificate_info(
                    to_internal_api(payload.certificate_type), to_internal_api(payload.encoding), payload.include_ocsp);
                auto ext_res = API_types_ext::to_external_api(int_res);
                mqtt.publish(msg.replyTo, serialize(ext_res));
                return true;
            }
        }
        return false;
    });
}

void evse_security_consumer_API::generate_api_cmd_get_verify_location() {
    subscribe_api_topic("get_verify_location", [this](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            API_types_ext::CaCertificateType payload;
            if (deserialize(msg.payload, payload)) {
                auto response = r_evse_security->call_get_verify_location(to_internal_api(payload));
                mqtt.publish(msg.replyTo, response);
                return true;
            }
        }
        return false;
    });
}

void evse_security_consumer_API::generate_api_var_communication_check() {
    subscribe_api_topic("communication_check", [this](std::string const& data) {
        bool val = false;
        if (deserialize(data, val)) {
            comm_check.set_value(val);
            return true;
        }
        return false;
    });
}

void evse_security_consumer_API::setup_heartbeat_generator() {
    auto topic = topics.everest_to_extern("heartbeat");
    auto action = [this, topic]() {
        mqtt.publish(topic, API_generic::serialize(hb_id++));
        return true;
    };
    comm_check.heartbeat(config.cfg_heartbeat_interval_ms, action);
}

void evse_security_consumer_API::subscribe_api_topic(std::string const& var,
                                                     ParseAndPublishFtor const& parse_and_publish) {
    auto topic = topics.extern_to_everest(var);
    mqtt.subscribe(topic, [=](std::string const& data) {
        try {
            if (not parse_and_publish(data)) {
                EVLOG_warning << "Invalid data: Deserialization failed.\n" << topic << "\n" << data;
            }
        } catch (const std::exception& e) {
            EVLOG_warning << "Topic: '" << topic << "' failed with -> " << e.what() << "\n => " << data;
        } catch (...) {
            EVLOG_warning << "Invalid data: Failed to parse JSON or to get data from it.\n" << topic;
        }
    });
}

} // namespace module
