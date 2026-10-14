// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/variable_permission.hpp>

#include <everest/logging.hpp>

#include <ocpp/common/utils.hpp>
#include <ocpp/v2/device_model_helpers.hpp>

namespace ocpp {
namespace v2 {

namespace {

/// Component names are compared case insensitively, as everywhere else in the device model.
bool same_name(const CiString<50>& lhs, const CiString<50>& rhs) {
    return lhs == rhs;
}

/// An instance is matched exactly, including its absence: an entry that names no instance grants
/// the instance-less component or variable only, never every instance of it.
bool same_instance(const std::optional<CiString<50>>& lhs, const std::optional<CiString<50>>& rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    return !lhs.has_value() or same_name(lhs.value(), rhs.value());
}

bool same_evse(const std::optional<EVSE>& lhs, const std::optional<EVSE>& rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    if (!lhs.has_value()) {
        return true;
    }
    return lhs->id == rhs->id and lhs->connectorId == rhs->connectorId;
}

} // namespace

VariablePermissionList VariablePermissionList::parse(const std::string& csv) {
    VariablePermissionList list;

    for (const auto& entry : ocpp::split_string(csv, ',', true)) {
        if (entry.empty()) {
            continue;
        }
        const auto parsed = parse_component_variable_hierarchy(entry);
        if (!parsed.has_value()) {
            EVLOG_warning << "Ignoring malformed entry '" << entry << "' in a connection's AllowSetVariables";
            continue;
        }
        list.entries.push_back(parsed.value());
    }

    return list;
}

bool VariablePermissionList::allows(const Component& component, const Variable& variable) const {
    for (const auto& entry : this->entries) {
        if (!same_name(entry.component.name, component.name) or
            !same_instance(entry.component.instance, component.instance) or
            !same_evse(entry.component.evse, component.evse)) {
            continue;
        }
        if (!entry.variable.has_value()) {
            // The entry names the whole component.
            return true;
        }
        if (same_name(entry.variable->name, variable.name) and
            same_instance(entry.variable->instance, variable.instance)) {
            return true;
        }
    }
    return false;
}

bool VariablePermissionList::empty() const {
    return this->entries.empty();
}

} // namespace v2
} // namespace ocpp
