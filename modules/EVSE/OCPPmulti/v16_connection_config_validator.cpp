// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "v16_connection_config_validator.hpp"

#include <everest/logging.hpp>
#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model_helpers.hpp>

namespace ocpp_multi {

V16ConnectionConfigValidator::V16ConnectionConfigValidator(ocpp::v2::DeviceModelInterface& device_model) :
    m_device_model(device_model) {
}

std::optional<std::string> V16ConnectionConfigValidator::validate_write(const ocpp::v2::Component& component,
                                                                        const ocpp::v2::Variable& variable,
                                                                        const std::string& value) const {
    if (component.name == "NetworkConfiguration" && component.instance.has_value()) {
        return validate_network_configuration_slot_write(component, variable, value);
    }
    const ocpp::v2::ComponentVariable cv = {component, variable, std::nullopt};
    if (cv == ocpp::v2::ControllerComponentVariables::NetworkConfigurationPriority) {
        return validate_network_configuration_priority_write(value);
    }
    return std::nullopt;
}

std::optional<std::string> V16ConnectionConfigValidator::validate_network_configuration_slot_write(
    const ocpp::v2::Component& component, const ocpp::v2::Variable& variable, const std::string& value) const {
    namespace CC = ocpp::v2::ControllerComponentVariables;
    namespace NC = ocpp::v2::NetworkConfigurationComponentVariables;
    try {
        const int slot = std::stoi(component.instance.value().get());

        // B09.FR.22: Reject writes targeting the currently active slot or any slot listed in the
        // current NetworkConfigurationPriority. Single reasonCode PriorityNetworkConf, like 2.x.
        const auto active_slot_opt = ocpp::v2::get_optional_value<int>(m_device_model, CC::ActiveNetworkProfile);
        if (active_slot_opt.has_value() && slot == active_slot_opt.value()) {
            EVLOG_warning << "Cannot set NetworkConfiguration variable for slot " << slot
                          << " which is the currently active network profile";
            return "PriorityNetworkConf";
        }
        const auto priority_opt =
            ocpp::v2::get_optional_value<std::string>(m_device_model, CC::NetworkConfigurationPriority);
        if (priority_opt.has_value()) {
            const auto priority_slots = NC::parse_priority(priority_opt.value(), true).value();
            for (const auto priority_slot : priority_slots) {
                if (priority_slot == slot) {
                    EVLOG_warning << "Cannot set NetworkConfiguration variable for slot " << slot
                                  << " which is a priority network profile";
                    return "PriorityNetworkConf";
                }
            }
        }

        // B09.FR.35: Reject security profile downgrades below the confirmed profile. An absent
        // confirmed cell counts as 0 ("nothing confirmed yet"), matching get_security_profile().
        if (variable.name == "SecurityProfile") {
            const int new_profile = std::stoi(value);
            const int confirmed_profile =
                ocpp::v2::get_optional_value<int>(m_device_model, CC::SecurityProfile).value_or(0);
            if (new_profile < confirmed_profile) {
                EVLOG_warning << "Cannot downgrade SecurityProfile from " << confirmed_profile << " to " << new_profile;
                return "NoSecurityDowngrade";
            }
        }
        // B09.FR.23: a slot outside the priority is stored as written; its content is checked when it is
        // added to NetworkConfigurationPriority.
    } catch (const std::exception& e) {
        EVLOG_warning << "Error validating NetworkConfiguration: " << e.what();
        return "InvalidNetworkConf";
    }
    return std::nullopt;
}

std::optional<std::string>
V16ConnectionConfigValidator::validate_network_configuration_priority_write(const std::string& value) const {
    // Same rules as the 2.x Provisioning block, without the 2.x-only certificate preconditions (A05.FR.02/03): a
    // missing certificate surfaces at connect time through slot failover. Without those checks, accepting a write that
    // only removes slots (as OCPP 2.1 does per B09.FR.20) gives the same result as the OCPP 2.0.1 rule.
    return ocpp::v2::NetworkConfigurationComponentVariables::validate_priority_write(m_device_model, value, true);
}

} // namespace ocpp_multi
