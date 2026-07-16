// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ocppImpl.hpp"

namespace module {
namespace ocpp_generic {

void ocppImpl::init() {
}

void ocppImpl::ready() {
}

bool ocppImpl::handle_stop() {
    return mod->m_ocpp.handle_stop();
}

bool ocppImpl::handle_restart() {
    return mod->m_ocpp.handle_restart();
}

void ocppImpl::handle_security_event(types::ocpp::SecurityEvent& event) {
    mod->m_ocpp.handle_security_event(event);
}

std::vector<types::ocpp::GetVariableResult>
ocppImpl::handle_get_variables(std::vector<types::ocpp::GetVariableRequest>& requests) {
    return mod->m_ocpp.handle_get_variables(requests);
}

std::vector<types::ocpp::SetVariableResult>
ocppImpl::handle_set_variables(std::vector<types::ocpp::SetVariableRequest>& requests, std::string& source) {
    return mod->m_ocpp.handle_set_variables(requests, source);
}

types::ocpp::ChangeAvailabilityResponse
ocppImpl::handle_change_availability(types::ocpp::ChangeAvailabilityRequest& request) {
    return mod->m_ocpp.handle_change_availability(request);
}

void ocppImpl::handle_monitor_variables(std::vector<types::ocpp::ComponentVariable>& component_variables) {
    mod->m_ocpp.handle_monitor_variables(component_variables);
}

} // namespace ocpp_generic
} // namespace module
