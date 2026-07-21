// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ocppImpl.hpp"
#include <conversions.hpp>
#include <everest/conversions/ocpp/ocpp_conversions.hpp>

namespace module {
namespace ocpp_generic {

void ocppImpl::init() {
}

void ocppImpl::ready() {
}

bool ocppImpl::handle_stop() {
    // your code for cmd stop goes here
    return true;
}

bool ocppImpl::handle_restart() {
    // your code for cmd restart goes here
    return true;
}

void ocppImpl::handle_security_event(types::ocpp::SecurityEvent& event) {
    if (this->mod->charge_point == nullptr) {
        EVLOG_warning << "ChargePoint not yet initialized. Cannot handle security event.";
        return;
    }

    std::optional<ocpp::DateTime> timestamp;
    if (event.timestamp.has_value()) {
        timestamp = ocpp_conversions::to_ocpp_datetime_or_now(event.timestamp.value());
    }
    this->mod->charge_point->on_security_event(event.type, event.info, event.critical, timestamp);
}

std::vector<types::ocpp::GetVariableResult>
ocppImpl::handle_get_variables(std::vector<types::ocpp::GetVariableRequest>& requests) {
    if (this->mod->charge_point == nullptr) {
        EVLOG_warning << "ChargePoint not yet initialized. Cannot handle get variables request.";
        std::vector<types::ocpp::GetVariableResult> results;
        for (const auto& req : requests) {
            types::ocpp::GetVariableResult result;
            result.status = types::ocpp::GetVariableStatusEnumType::Rejected;
            result.component_variable.component = req.component_variable.component;
            result.component_variable.variable = req.component_variable.variable;
            result.attribute_type = req.attribute_type;
            results.push_back(result);
        }
        return results;
    }

    const auto _requests = conversions::to_ocpp_get_variable_data_vector(requests);
    const auto response = this->mod->charge_point->get_variables(_requests);
    return conversions::to_everest_get_variable_result_vector(response);
}

std::vector<types::ocpp::SetVariableResult>
ocppImpl::handle_set_variables(std::vector<types::ocpp::SetVariableRequest>& requests, std::string& source) {
    if (this->mod->charge_point == nullptr) {
        EVLOG_warning << "ChargePoint not yet initialized. Cannot handle set variables request.";
        std::vector<types::ocpp::SetVariableResult> results;
        for (const auto& req : requests) {
            types::ocpp::SetVariableResult result;
            result.status = types::ocpp::SetVariableStatusEnumType ::Rejected;
            result.component_variable.component = req.component_variable.component;
            result.component_variable.variable = req.component_variable.variable;
            result.attribute_type = req.attribute_type;
            results.push_back(result);
        }
        return results;
    }

    const auto _requests = conversions::to_ocpp_set_variable_data_vector(requests);
    const auto response_map = this->mod->charge_point->set_variables(_requests, source);
    std::vector<ocpp::v2::SetVariableResult> response;
    for (const auto& [set_variable_data, set_variable_result] : response_map) {
        response.push_back(set_variable_result);
    }
    return conversions::to_everest_set_variable_result_vector(response);
}

types::ocpp::ChangeAvailabilityResponse
ocppImpl::handle_change_availability(types::ocpp::ChangeAvailabilityRequest& request) {
    // your code for cmd change_availability goes here
    return {};
}

void ocppImpl::handle_monitor_variables(std::vector<types::ocpp::ComponentVariable>& component_variables) {
<<<<<<< HEAD
    // your code for cmd monitor_variables goes here
=======
    using namespace ocpp::v2;

    if (mod->charge_point == nullptr) {
        EVLOG_warning << "ChargePoint not initialized, cannot handle monitor variables command";
    } else {
        std::lock_guard lock(monitor_list_mutex);

        // guard with a flag, not monitor_list.empty(): a first call with an empty list would
        // otherwise register the handler again on the next call
        if (!variable_listener_registered) {
            mod->charge_point->register_variable_listener(
                [this](auto&, const Component& component, const Variable& variable, auto&, auto&, auto&,
                       const std::string& value) { variable_changed(component, variable, value); });
            variable_listener_registered = true;
        }

        // add variables to monitor list
        for (const auto& cv : component_variables) {
            // failures to insert are likely to be the same variable being
            // requested again
            (void)monitor_list.insert(convert(cv));
        }
    }
}

void ocppImpl::variable_changed(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                                const std::string& value) {
    using namespace conversions;

    MonitorListEntry entry{component, variable};
    bool publish;
    {
        std::lock_guard lock(monitor_list_mutex);
        const auto it = monitor_list.find(entry);
        publish = it != monitor_list.end();
    }
    if (publish) {
        // monitor entry exists - publish
        types::ocpp::EventData event_data;
        event_data.component_variable.component = to_everest_component(component);
        event_data.component_variable.variable = to_everest_variable(variable);
        event_data.event_id = 0;
        event_data.timestamp = ocpp::DateTime();
        event_data.trigger = types::ocpp::EventTriggerEnum::Delta;
        event_data.actual_value = value;
        event_data.event_notification_type = types::ocpp::EventNotificationType::CustomMonitor;
        publish_event_data(event_data);
    }
>>>>>>> e2e5d92 (fix(ocpp): make v2 DeviceModel thread-safe (#2471))
}

} // namespace ocpp_generic
} // namespace module
