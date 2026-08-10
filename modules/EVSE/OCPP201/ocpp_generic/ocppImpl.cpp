// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ocppImpl.hpp"
<<<<<<< HEAD
#include <conversions.hpp>
=======
#include "everest/conversions/ocpp/evse_security_ocpp.hpp"
#include "ocpp/v2/component_state_manager.hpp"
#include "ocpp/v2/ocpp_types.hpp"
>>>>>>> 5731aad (fix(OCPP): Harden ChangeAvailability by catching EvseOutOfRange and ConnectorOutOfRange (#2591))
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
<<<<<<< HEAD
    // your code for cmd change_availability goes here
    return {};
=======
    using ChangeAvailabilityStatusEnum = ocpp::v2::ChangeAvailabilityStatusEnum;
    ocpp::v2::ChangeAvailabilityResponse result;
    result.status = ChangeAvailabilityStatusEnum::Rejected;

    if (mod->charge_point == nullptr) {
        EVLOG_warning << "ChargePoint not initialized, cannot handle change availability command";
    } else {
        const auto ocpp_request = conversions::to_ocpp_change_availability_request(request);
        try {
            result = mod->charge_point->on_change_availability(ocpp_request);
        } catch (const ocpp::v2::EvseOutOfRangeException& e) {
            result.status = ChangeAvailabilityStatusEnum::Rejected;
            result.statusInfo = ocpp::v2::StatusInfo{"InvalidInput", e.what()};
        } catch (const ocpp::v2::ConnectorOutOfRangeException& e) {
            result.status = ChangeAvailabilityStatusEnum::Rejected;
            result.statusInfo = ocpp::v2::StatusInfo{"InvalidInput", e.what()};
        }
    }

    return conversions::to_everest_change_availability_response(result);
>>>>>>> 5731aad (fix(OCPP): Harden ChangeAvailability by catching EvseOutOfRange and ConnectorOutOfRange (#2591))
}

void ocppImpl::handle_monitor_variables(std::vector<types::ocpp::ComponentVariable>& component_variables) {
    // your code for cmd monitor_variables goes here
}

} // namespace ocpp_generic
} // namespace module
