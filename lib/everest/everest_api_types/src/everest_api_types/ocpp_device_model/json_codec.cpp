// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ocpp_device_model/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "ocpp/json_codec.hpp"
#include "ocpp_device_model/API.hpp"
#include "utilities/json_codec_helpers.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace everest::lib::API::V1_0::types::ocpp_device_model {

using json = nlohmann::json;
using utilities::json_number;

namespace {

void check_properties(const json& j, std::initializer_list<std::string_view> allowed, std::string_view type) {
    if (not j.is_object()) {
        throw std::invalid_argument(std::string(type) + ": not an object");
    }
    for (const auto& item : j.items()) {
        if (std::find(allowed.begin(), allowed.end(), item.key()) == allowed.end()) {
            throw std::invalid_argument(std::string(type) + ": unknown property " + item.key());
        }
    }
}

constexpr std::size_t max_name_length = 50;
constexpr std::size_t max_unit_length = 16;
constexpr std::size_t max_values_list_length = 1000;
constexpr std::size_t max_value_length = 2500;

void check_length(std::string const& value, std::size_t max_length, std::string_view key) {
    if (value.size() > max_length) {
        throw std::invalid_argument(std::string(key) + ": longer than " + std::to_string(max_length) + " characters");
    }
}

std::string string_value(const json& j, std::string_view key, std::size_t max_length = std::string::npos) {
    auto value = j.at(key).get<std::string>();
    check_length(value, max_length, key);
    return value;
}

std::optional<std::string> optional_string(const json& j, std::string_view key,
                                           std::size_t max_length = std::string::npos) {
    if (not j.contains(key)) {
        return std::nullopt;
    }
    return string_value(j, key, max_length);
}

// The length limits of Component and Variable in types/ocpp.yaml, which the ocpp codec does not check.
ocpp::Component component_value(const json& j) {
    auto component = j.at("component").get<ocpp::Component>();
    check_length(component.name, max_name_length, "component.name");
    check_length(component.instance.value_or(""), max_name_length, "component.instance");
    return component;
}

ocpp::Variable variable_value(const json& j) {
    auto variable = j.at("variable").get<ocpp::Variable>();
    check_length(variable.name, max_name_length, "variable.name");
    check_length(variable.instance.value_or(""), max_name_length, "variable.instance");
    return variable;
}

std::optional<std::string> optional_owner(const json& j) {
    auto owner = optional_string(j, "owner");
    if (owner and owner->empty()) {
        throw std::invalid_argument("owner must not be empty");
    }
    return owner;
}

std::optional<bool> optional_bool(const json& j, std::string_view key) {
    if (not j.contains(key)) {
        return std::nullopt;
    }
    return j.at(key).get<bool>();
}

template <class T> std::optional<T> optional_object(const json& j, std::string_view key) {
    if (not j.contains(key)) {
        return std::nullopt;
    }
    return j.at(key).get<T>();
}

template <class E, std::size_t N> using EnumNames = std::array<std::pair<E, std::string_view>, N>;

template <class E, std::size_t N> void enum_to_json(json& j, E value, EnumNames<E, N> const& names) noexcept {
    for (const auto& [e, name] : names) {
        if (e == value) {
            j = name;
            return;
        }
    }
    j = "INVALID_VALUE";
}

template <class E, std::size_t N> void enum_from_json(const json& j, E& value, EnumNames<E, N> const& names) {
    const auto text = j.get<std::string>();
    for (const auto& [e, name] : names) {
        if (name == text) {
            value = e;
            return;
        }
    }
    throw std::out_of_range("Unknown enum value: " + text);
}

constexpr int32_t max_severity = 9;

const EnumNames<DataEnum, 8> data_enum_names{{
    {DataEnum::string, "string"},
    {DataEnum::decimal, "decimal"},
    {DataEnum::integer, "integer"},
    {DataEnum::dateTime, "dateTime"},
    {DataEnum::boolean, "boolean"},
    {DataEnum::OptionList, "OptionList"},
    {DataEnum::SequenceList, "SequenceList"},
    {DataEnum::MemberList, "MemberList"},
}};

const EnumNames<MutabilityEnum, 3> mutability_enum_names{{
    {MutabilityEnum::ReadOnly, "ReadOnly"},
    {MutabilityEnum::WriteOnly, "WriteOnly"},
    {MutabilityEnum::ReadWrite, "ReadWrite"},
}};

const EnumNames<MonitorEnum, 7> monitor_enum_names{{
    {MonitorEnum::UpperThreshold, "UpperThreshold"},
    {MonitorEnum::LowerThreshold, "LowerThreshold"},
    {MonitorEnum::Delta, "Delta"},
    {MonitorEnum::Periodic, "Periodic"},
    {MonitorEnum::PeriodicClockAligned, "PeriodicClockAligned"},
    {MonitorEnum::TargetDelta, "TargetDelta"},
    {MonitorEnum::TargetDeltaRelative, "TargetDeltaRelative"},
}};

const EnumNames<RegisteredMonitorType, 2> registered_monitor_type_names{{
    {RegisteredMonitorType::HardWiredMonitor, "HardWiredMonitor"},
    {RegisteredMonitorType::PreconfiguredMonitor, "PreconfiguredMonitor"},
}};

const EnumNames<RegistrationStatus, 2> registration_status_names{{
    {RegistrationStatus::Accepted, "Accepted"},
    {RegistrationStatus::OwnerInUse, "OwnerInUse"},
}};

const EnumNames<RegistrationRejectionReason, 5> registration_rejection_reason_names{{
    {RegistrationRejectionReason::AlreadyDefined, "AlreadyDefined"},
    {RegistrationRejectionReason::NoAttributes, "NoAttributes"},
    {RegistrationRejectionReason::ReservedOwner, "ReservedOwner"},
    {RegistrationRejectionReason::InvalidNameKey, "InvalidNameKey"},
    {RegistrationRejectionReason::ReservedVariable, "ReservedVariable"},
}};

} // namespace

void to_json(json& j, DataEnum const& k) noexcept {
    enum_to_json(j, k, data_enum_names);
}
void from_json(const json& j, DataEnum& k) {
    enum_from_json(j, k, data_enum_names);
}

void to_json(json& j, MutabilityEnum const& k) noexcept {
    enum_to_json(j, k, mutability_enum_names);
}
void from_json(const json& j, MutabilityEnum& k) {
    enum_from_json(j, k, mutability_enum_names);
}

void to_json(json& j, MonitorEnum const& k) noexcept {
    enum_to_json(j, k, monitor_enum_names);
}
void from_json(const json& j, MonitorEnum& k) {
    enum_from_json(j, k, monitor_enum_names);
}

void to_json(json& j, RegisteredMonitorType const& k) noexcept {
    enum_to_json(j, k, registered_monitor_type_names);
}
void from_json(const json& j, RegisteredMonitorType& k) {
    enum_from_json(j, k, registered_monitor_type_names);
}

void to_json(json& j, RegistrationStatus const& k) noexcept {
    enum_to_json(j, k, registration_status_names);
}
void from_json(const json& j, RegistrationStatus& k) {
    enum_from_json(j, k, registration_status_names);
}

void to_json(json& j, RegistrationRejectionReason const& k) noexcept {
    enum_to_json(j, k, registration_rejection_reason_names);
}
void from_json(const json& j, RegistrationRejectionReason& k) {
    enum_from_json(j, k, registration_rejection_reason_names);
}

void to_json(json& j, VariableCharacteristics const& k) noexcept {
    j = json{{"data_type", k.data_type}, {"supports_monitoring", k.supports_monitoring}};
    if (k.unit) {
        j["unit"] = k.unit.value();
    }
    if (k.min_limit) {
        j["min_limit"] = k.min_limit.value();
    }
    if (k.max_limit) {
        j["max_limit"] = k.max_limit.value();
    }
    if (k.max_elements) {
        j["max_elements"] = k.max_elements.value();
    }
    if (k.values_list) {
        j["values_list"] = k.values_list.value();
    }
}
void from_json(const json& j, VariableCharacteristics& k) {
    check_properties(
        j, {"data_type", "supports_monitoring", "unit", "min_limit", "max_limit", "max_elements", "values_list"},
        "VariableCharacteristics");
    k.data_type = j.at("data_type");
    k.supports_monitoring = j.at("supports_monitoring").get<bool>();
    k.unit = optional_string(j, "unit", max_unit_length);
    k.min_limit.reset();
    if (j.contains("min_limit")) {
        k.min_limit = json_number<float>(j.at("min_limit"));
    }
    k.max_limit.reset();
    if (j.contains("max_limit")) {
        k.max_limit = json_number<float>(j.at("max_limit"));
    }
    k.max_elements.reset();
    if (j.contains("max_elements")) {
        k.max_elements = json_number<int32_t>(j.at("max_elements"));
        if (k.max_elements.value() < 1) {
            throw std::invalid_argument("VariableCharacteristics: max_elements must be at least 1");
        }
    }
    k.values_list = optional_string(j, "values_list", max_values_list_length);
}

void to_json(json& j, VariableAttribute const& k) noexcept {
    j = json::object();
    if (k.type) {
        j["type"] = k.type.value();
    }
    if (k.value) {
        j["value"] = k.value.value();
    }
    if (k.mutability) {
        j["mutability"] = k.mutability.value();
    }
    if (k.persistent) {
        j["persistent"] = k.persistent.value();
    }
    if (k.constant) {
        j["constant"] = k.constant.value();
    }
}
void from_json(const json& j, VariableAttribute& k) {
    check_properties(j, {"type", "value", "mutability", "persistent", "constant"}, "VariableAttribute");
    k.type = optional_object<ocpp::AttributeEnum>(j, "type");
    k.value = optional_string(j, "value", max_value_length);
    k.mutability = optional_object<MutabilityEnum>(j, "mutability");
    k.persistent = optional_bool(j, "persistent");
    k.constant = optional_bool(j, "constant");
}

void to_json(json& j, RegisteredMonitor const& k) noexcept {
    j = json{{"type", k.type}, {"value", k.value}, {"severity", k.severity}, {"monitor_type", k.monitor_type}};
    if (k.transaction) {
        j["transaction"] = k.transaction.value();
    }
}
void from_json(const json& j, RegisteredMonitor& k) {
    check_properties(j, {"type", "value", "severity", "transaction", "monitor_type"}, "RegisteredMonitor");
    k.type = j.at("type");
    k.value = json_number<float>(j.at("value"));
    k.severity = json_number<int32_t>(j.at("severity"));
    if (k.severity < 0 or k.severity > max_severity) {
        throw std::invalid_argument("RegisteredMonitor: severity out of range");
    }
    k.transaction = optional_bool(j, "transaction");
    k.monitor_type = j.at("monitor_type");
}

void to_json(json& j, RegisteredVariable const& k) noexcept {
    j = json{{"variable", k.variable}, {"characteristics", k.characteristics}, {"attributes", k.attributes}};
    if (k.monitors) {
        j["monitors"] = k.monitors.value();
    }
}
void from_json(const json& j, RegisteredVariable& k) {
    check_properties(j, {"variable", "characteristics", "attributes", "monitors"}, "RegisteredVariable");
    k.variable = variable_value(j);
    k.characteristics = j.at("characteristics");
    k.attributes = j.at("attributes").get<std::vector<VariableAttribute>>();
    k.monitors = optional_object<std::vector<RegisteredMonitor>>(j, "monitors");
}

void to_json(json& j, RegisteredComponent const& k) noexcept {
    j = json{{"component", k.component}, {"variables", k.variables}};
}
void from_json(const json& j, RegisteredComponent& k) {
    check_properties(j, {"component", "variables"}, "RegisteredComponent");
    k.component = component_value(j);
    k.variables = j.at("variables").get<std::vector<RegisteredVariable>>();
}

void to_json(json& j, Registration const& k) noexcept {
    j = json{{"components", k.components}};
    if (k.owner) {
        j["owner"] = k.owner.value();
    }
}
void from_json(const json& j, Registration& k) {
    check_properties(j, {"owner", "components"}, "Registration");
    k.owner = optional_owner(j);
    k.components = j.at("components").get<std::vector<RegisteredComponent>>();
}

void to_json(json& j, RejectedVariable const& k) noexcept {
    j = json{{"component", k.component}, {"variable", k.variable}, {"reason", k.reason}};
    if (k.defined_by) {
        j["defined_by"] = k.defined_by.value();
    }
}
void from_json(const json& j, RejectedVariable& k) {
    check_properties(j, {"component", "variable", "reason", "defined_by"}, "RejectedVariable");
    k.component = component_value(j);
    k.variable = variable_value(j);
    k.reason = j.at("reason");
    k.defined_by = optional_string(j, "defined_by");
}

void to_json(json& j, RegistrationResult const& k) noexcept {
    j = json{{"owner", k.owner}, {"status", k.status}, {"rejected", k.rejected}};
}
void from_json(const json& j, RegistrationResult& k) {
    check_properties(j, {"owner", "status", "rejected"}, "RegistrationResult");
    k.owner = string_value(j, "owner");
    k.status = j.at("status");
    k.rejected = j.at("rejected").get<std::vector<RejectedVariable>>();
}

void to_json(json& j, WriteRequest const& k) noexcept {
    j = json{{"owner", k.owner},       {"component", k.component},
             {"variable", k.variable}, {"attribute_type", k.attribute_type},
             {"value", k.value},       {"source", k.source}};
}
void from_json(const json& j, WriteRequest& k) {
    check_properties(j, {"owner", "component", "variable", "attribute_type", "value", "source"}, "WriteRequest");
    k.owner = string_value(j, "owner");
    k.component = component_value(j);
    k.variable = variable_value(j);
    k.attribute_type = j.at("attribute_type");
    k.value = string_value(j, "value", max_value_length);
    k.source = string_value(j, "source");
}

void to_json(json& j, ValueChange const& k) noexcept {
    j = json{{"component", k.component}, {"variable", k.variable}, {"value", k.value}};
    if (k.owner) {
        j["owner"] = k.owner.value();
    }
    if (k.attribute_type) {
        j["attribute_type"] = k.attribute_type.value();
    }
}
void from_json(const json& j, ValueChange& k) {
    check_properties(j, {"owner", "component", "variable", "attribute_type", "value"}, "ValueChange");
    k.owner = optional_owner(j);
    k.component = component_value(j);
    k.variable = variable_value(j);
    k.attribute_type = optional_object<ocpp::AttributeEnum>(j, "attribute_type");
    k.value = string_value(j, "value", max_value_length);
}

void to_json(json& j, GetRegistrations const& k) noexcept {
    j = json{{"expected_owners", k.expected_owners}};
}
void from_json(const json& j, GetRegistrations& k) {
    check_properties(j, {"expected_owners"}, "GetRegistrations");
    k.expected_owners = j.at("expected_owners").get<std::vector<std::string>>();
}

} // namespace everest::lib::API::V1_0::types::ocpp_device_model
