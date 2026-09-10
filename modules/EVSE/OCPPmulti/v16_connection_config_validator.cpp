// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "v16_connection_config_validator.hpp"

#include <everest/logging.hpp>
#include <ocpp/common/utils.hpp>
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
            for (const auto& priority_slot_str : ocpp::split_string(priority_opt.value(), ',')) {
                int priority_slot = 0;
                try {
                    priority_slot = std::stoi(priority_slot_str);
                } catch (const std::exception& e) {
                    EVLOG_warning << "NetworkConfigurationPriority contains non-integer token '" << priority_slot_str
                                  << "': " << e.what();
                    continue;
                }
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

        // URL-scheme / security-profile consistency of the resulting profile (current state + proposed
        // change), checked only once the slot holds a complete profile, like 2.x.
        if (auto profile_opt = NC::read_profile_from_device_model(m_device_model, slot); profile_opt.has_value()) {
            auto profile = *profile_opt;
            if (variable.name == "SecurityProfile") {
                profile.securityProfile = std::stoi(value);
            } else if (variable.name == "OcppCsmsUrl") {
                profile.ocppCsmsUrl = ocpp::CiString<2000>(value);
            }
            const std::string url = profile.ocppCsmsUrl.get();
            if (url.find("wss://") == 0 && profile.securityProfile < 2) {
                EVLOG_warning << "configurationSlot " << slot << ": wss:// URL requires securityProfile >= 2, got "
                              << profile.securityProfile;
                return "InvalidNetworkConf";
            }
            if (url.find("ws://") == 0 && profile.securityProfile >= 2) {
                EVLOG_warning << "configurationSlot " << slot
                              << ": ws:// URL is not allowed with securityProfile >= 2, got "
                              << profile.securityProfile;
                return "InvalidNetworkConf";
            }
        }
    } catch (const std::exception& e) {
        EVLOG_warning << "Error validating NetworkConfiguration: " << e.what();
        return "InvalidNetworkConf";
    }
    return std::nullopt;
}

std::optional<std::string>
V16ConnectionConfigValidator::validate_network_configuration_priority_write(const std::string& value) const {
    namespace NC = ocpp::v2::NetworkConfigurationComponentVariables;
    try {
        for (const auto& slot_str : ocpp::split_string(value, ',')) {
            const int slot = std::stoi(slot_str);
            if (!NC::read_profile_from_device_model(m_device_model, slot).has_value()) {
                EVLOG_warning << "Could not find network profile for configurationSlot: " << slot_str;
                return "InvalidNetworkConf";
            }
        }
    } catch (const std::exception& e) {
        EVLOG_warning << "NetworkConfigurationPriority contains at least one value which is not an integer: " << value;
        return "InvalidNetworkConf";
    }
    return std::nullopt;
}

} // namespace ocpp_multi
