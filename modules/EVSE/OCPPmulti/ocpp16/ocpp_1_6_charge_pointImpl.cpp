// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ocpp_1_6_charge_pointImpl.hpp"

namespace module {
namespace ocpp16 {

void ocpp_1_6_charge_pointImpl::init() {
}

void ocpp_1_6_charge_pointImpl::ready() {
}

bool ocpp_1_6_charge_pointImpl::handle_stop() {
    // your code for cmd stop goes here
    return mod->m_ocpp.handle_stop();
}

bool ocpp_1_6_charge_pointImpl::handle_restart() {
    // your code for cmd restart goes here
    return mod->m_ocpp.handle_restart();
}

types::ocpp::GetConfigurationResponse ocpp_1_6_charge_pointImpl::handle_get_configuration_key(Array& keys) {
    // your code for cmd get_configuration_key goes here
    return mod->m_ocpp.handle_get_configuration_key(keys);
}

types::ocpp::ConfigurationStatus ocpp_1_6_charge_pointImpl::handle_set_configuration_key(std::string& key,
                                                                                         std::string& value) {
    // your code for cmd set_configuration_key goes here
    return mod->m_ocpp.handle_set_configuration_key(key, value);
}

void ocpp_1_6_charge_pointImpl::handle_monitor_configuration_keys(Array& keys) {
    // your code for cmd monitor_configuration_keys goes here
    mod->m_ocpp.handle_monitor_configuration_keys(keys);
}

void ocpp_1_6_charge_pointImpl::handle_security_event(std::string& type, std::string& info) {
    // your code for cmd security_event goes here
    mod->m_ocpp.handle_security_event(type, info);
}

} // namespace ocpp16
} // namespace module
