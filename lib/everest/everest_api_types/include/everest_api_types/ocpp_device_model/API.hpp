// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once
#include <everest_api_types/ocpp/API.hpp>
#include <optional>
#include <string>
#include <vector>

namespace everest::lib::API::V1_0::types::ocpp_device_model {

enum class DataEnum {
    string,
    decimal,
    integer,
    dateTime,
    boolean,
    OptionList,
    SequenceList,
    MemberList,
};

enum class MutabilityEnum {
    ReadOnly,
    WriteOnly,
    ReadWrite,
};

enum class MonitorEnum {
    UpperThreshold,
    LowerThreshold,
    Delta,
    Periodic,
    PeriodicClockAligned,
    TargetDelta,
    TargetDeltaRelative,
};

enum class RegisteredMonitorType {
    HardWiredMonitor,
    PreconfiguredMonitor,
};

enum class RegistrationStatus {
    Accepted,
    OwnerInUse,
};

enum class RegistrationRejectionReason {
    AlreadyDefined,
    NoAttributes,
    ReservedOwner,
    InvalidNameKey,
    ReservedVariable,
};

struct VariableCharacteristics {
    DataEnum data_type;
    bool supports_monitoring;
    std::optional<std::string> unit;
    std::optional<float> min_limit;
    std::optional<float> max_limit;
    std::optional<int32_t> max_elements;
    std::optional<std::string> values_list;
};

struct VariableAttribute {
    std::optional<ocpp::AttributeEnum> type;
    std::optional<std::string> value;
    std::optional<MutabilityEnum> mutability;
    std::optional<bool> persistent;
    std::optional<bool> constant;
};

struct RegisteredMonitor {
    MonitorEnum type;
    float value;
    int32_t severity;
    std::optional<bool> transaction;
    RegisteredMonitorType monitor_type;
};

struct RegisteredVariable {
    ocpp::Variable variable;
    VariableCharacteristics characteristics;
    std::vector<VariableAttribute> attributes;
    std::optional<std::vector<RegisteredMonitor>> monitors;
};

struct RegisteredComponent {
    ocpp::Component component;
    std::vector<RegisteredVariable> variables;
};

struct Registration {
    std::optional<std::string> owner;
    std::vector<RegisteredComponent> components;
};

struct RejectedVariable {
    ocpp::Component component;
    ocpp::Variable variable;
    RegistrationRejectionReason reason;
    std::optional<std::string> defined_by;
};

struct RegistrationResult {
    std::string owner;
    RegistrationStatus status;
    std::vector<RejectedVariable> rejected;
};

struct WriteRequest {
    std::string owner;
    ocpp::Component component;
    ocpp::Variable variable;
    ocpp::AttributeEnum attribute_type;
    std::string value;
    std::string source;
};

struct ValueChange {
    std::optional<std::string> owner;
    ocpp::Component component;
    ocpp::Variable variable;
    std::optional<ocpp::AttributeEnum> attribute_type;
    std::string value;
};

struct GetRegistrations {
    std::vector<std::string> expected_owners;
};

} // namespace everest::lib::API::V1_0::types::ocpp_device_model
