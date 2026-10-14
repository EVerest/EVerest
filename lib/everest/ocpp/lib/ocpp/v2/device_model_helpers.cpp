// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/device_model_helpers.hpp>

#include <string>

namespace ocpp {
namespace v2 {

namespace {

/// Splits "Name" or "Name#Instance" into its two parts. The name must not be empty, and an instance
/// separator with nothing after it is malformed rather than an empty instance.
bool split_name_and_instance(const std::string& text, std::string& name, std::optional<std::string>& instance) {
    const auto separator = text.find('#');
    if (separator == std::string::npos) {
        name = text;
        instance.reset();
    } else {
        name = text.substr(0, separator);
        instance = text.substr(separator + 1);
        if (instance->empty()) {
            return false;
        }
    }
    return !name.empty();
}

bool parse_int(const std::string& text, std::int32_t& value) {
    if (text.empty()) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        value = std::stoi(text, &consumed);
        return consumed == text.size();
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace

std::optional<ComponentVariable> parse_component_variable_hierarchy(const std::string& entry) {
    if (entry.empty()) {
        return std::nullopt;
    }

    std::string remainder = entry;
    ComponentVariable component_variable;

    // Optional "<evse>[#<connector>]/" prefix.
    if (const auto slash = remainder.find('/'); slash != std::string::npos) {
        const auto evse_part = remainder.substr(0, slash);
        remainder = remainder.substr(slash + 1);

        EVSE evse;
        const auto hash = evse_part.find('#');
        if (hash == std::string::npos) {
            if (!parse_int(evse_part, evse.id)) {
                return std::nullopt;
            }
        } else {
            std::int32_t connector_id = 0;
            if (!parse_int(evse_part.substr(0, hash), evse.id) ||
                !parse_int(evse_part.substr(hash + 1), connector_id)) {
                return std::nullopt;
            }
            evse.connectorId = connector_id;
        }
        component_variable.component.evse = evse;
    }

    // Optional "&Variable[#Instance]" suffix.
    std::string component_part = remainder;
    if (const auto ampersand = remainder.find('&'); ampersand != std::string::npos) {
        component_part = remainder.substr(0, ampersand);

        std::string variable_name;
        std::optional<std::string> variable_instance;
        if (!split_name_and_instance(remainder.substr(ampersand + 1), variable_name, variable_instance)) {
            return std::nullopt;
        }
        Variable variable;
        variable.name = variable_name;
        if (variable_instance.has_value()) {
            variable.instance = variable_instance.value();
        }
        component_variable.variable = variable;
    }

    std::string component_name;
    std::optional<std::string> component_instance;
    if (!split_name_and_instance(component_part, component_name, component_instance)) {
        return std::nullopt;
    }
    component_variable.component.name = component_name;
    if (component_instance.has_value()) {
        component_variable.component.instance = component_instance.value();
    }

    return component_variable;
}

std::string to_component_variable_hierarchy(const Component& component, const std::optional<Variable>& variable) {
    std::string result;

    if (component.evse.has_value()) {
        result += std::to_string(component.evse->id);
        if (component.evse->connectorId.has_value()) {
            result += "#" + std::to_string(component.evse->connectorId.value());
        }
        result += "/";
    }

    result += component.name.get();
    if (component.instance.has_value()) {
        result += "#" + component.instance.value().get();
    }

    if (variable.has_value()) {
        result += "&" + variable->name.get();
        if (variable->instance.has_value()) {
            result += "#" + variable->instance.value().get();
        }
    }

    return result;
}

} // namespace v2
} // namespace ocpp
