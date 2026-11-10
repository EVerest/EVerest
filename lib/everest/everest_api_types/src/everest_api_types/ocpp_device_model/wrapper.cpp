// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ocpp_device_model/wrapper.hpp"
#include "ocpp/wrapper.hpp"
#include "ocpp_device_model/API.hpp"

#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace everest::lib::API::V1_0::types::ocpp_device_model {

#define enum_case(A)                                                                                                   \
    case SrcT::A:                                                                                                      \
        return TarT::A

using ocpp::to_external_api;
using ocpp::to_internal_api;

namespace {

template <class SrcT, class ConvT> auto convert_optional(std::optional<SrcT> const& src, ConvT const& converter) {
    using TarT = decltype(converter(src.value()));
    std::optional<TarT> result;
    if (src) {
        result = converter(src.value());
    }
    return result;
}

template <class SrcT, class ConvT> auto convert_vector(std::vector<SrcT> const& src, ConvT const& converter) {
    using TarT = decltype(converter(std::declval<SrcT const&>()));
    std::vector<TarT> result;
    result.reserve(src.size());
    for (SrcT const& elem : src) {
        result.push_back(converter(elem));
    }
    return result;
}

template <class SrcT> auto vec_to_internal(std::vector<SrcT> const& src) {
    return convert_vector(src, [](SrcT const& val) { return to_internal_api(val); });
}

template <class SrcT> auto vec_to_external(std::vector<SrcT> const& src) {
    return convert_vector(src, [](SrcT const& val) { return to_external_api(val); });
}

template <class SrcT> auto opt_to_internal(std::optional<SrcT> const& src) {
    return convert_optional(src, [](SrcT const& val) { return to_internal_api(val); });
}

template <class SrcT> auto opt_to_external(std::optional<SrcT> const& src) {
    return convert_optional(src, [](SrcT const& val) { return to_external_api(val); });
}

template <class SrcT> auto opt_vec_to_internal(std::optional<std::vector<SrcT>> const& src) {
    return convert_optional(src, [](std::vector<SrcT> const& val) { return vec_to_internal(val); });
}

template <class SrcT> auto opt_vec_to_external(std::optional<std::vector<SrcT>> const& src) {
    return convert_optional(src, [](std::vector<SrcT> const& val) { return vec_to_external(val); });
}

} // namespace

DataEnum_Internal to_internal_api(DataEnum_External const& val) {
    using SrcT = DataEnum_External;
    using TarT = DataEnum_Internal;
    switch (val) {
        enum_case(string);
        enum_case(decimal);
        enum_case(integer);
        enum_case(dateTime);
        enum_case(boolean);
        enum_case(OptionList);
        enum_case(SequenceList);
        enum_case(MemberList);
    }
    throw std::out_of_range("Unexpected value for ocpp_device_model::DataEnum_External");
}

DataEnum_External to_external_api(DataEnum_Internal const& val) {
    using SrcT = DataEnum_Internal;
    using TarT = DataEnum_External;
    switch (val) {
        enum_case(string);
        enum_case(decimal);
        enum_case(integer);
        enum_case(dateTime);
        enum_case(boolean);
        enum_case(OptionList);
        enum_case(SequenceList);
        enum_case(MemberList);
    }
    throw std::out_of_range("Unexpected value for ocpp_device_model::DataEnum_Internal");
}

MutabilityEnum_Internal to_internal_api(MutabilityEnum_External const& val) {
    using SrcT = MutabilityEnum_External;
    using TarT = MutabilityEnum_Internal;
    switch (val) {
        enum_case(ReadOnly);
        enum_case(WriteOnly);
        enum_case(ReadWrite);
    }
    throw std::out_of_range("Unexpected value for ocpp_device_model::MutabilityEnum_External");
}

MutabilityEnum_External to_external_api(MutabilityEnum_Internal const& val) {
    using SrcT = MutabilityEnum_Internal;
    using TarT = MutabilityEnum_External;
    switch (val) {
        enum_case(ReadOnly);
        enum_case(WriteOnly);
        enum_case(ReadWrite);
    }
    throw std::out_of_range("Unexpected value for ocpp_device_model::MutabilityEnum_Internal");
}

MonitorEnum_Internal to_internal_api(MonitorEnum_External const& val) {
    using SrcT = MonitorEnum_External;
    using TarT = MonitorEnum_Internal;
    switch (val) {
        enum_case(UpperThreshold);
        enum_case(LowerThreshold);
        enum_case(Delta);
        enum_case(Periodic);
        enum_case(PeriodicClockAligned);
        enum_case(TargetDelta);
        enum_case(TargetDeltaRelative);
    }
    throw std::out_of_range("Unexpected value for ocpp_device_model::MonitorEnum_External");
}

MonitorEnum_External to_external_api(MonitorEnum_Internal const& val) {
    using SrcT = MonitorEnum_Internal;
    using TarT = MonitorEnum_External;
    switch (val) {
        enum_case(UpperThreshold);
        enum_case(LowerThreshold);
        enum_case(Delta);
        enum_case(Periodic);
        enum_case(PeriodicClockAligned);
        enum_case(TargetDelta);
        enum_case(TargetDeltaRelative);
    }
    throw std::out_of_range("Unexpected value for ocpp_device_model::MonitorEnum_Internal");
}

RegisteredMonitorType_Internal to_internal_api(RegisteredMonitorType_External const& val) {
    using SrcT = RegisteredMonitorType_External;
    using TarT = RegisteredMonitorType_Internal;
    switch (val) {
        enum_case(HardWiredMonitor);
        enum_case(PreconfiguredMonitor);
    }
    throw std::out_of_range("Unexpected value for ocpp_device_model::RegisteredMonitorType_External");
}

RegisteredMonitorType_External to_external_api(RegisteredMonitorType_Internal const& val) {
    using SrcT = RegisteredMonitorType_Internal;
    using TarT = RegisteredMonitorType_External;
    switch (val) {
        enum_case(HardWiredMonitor);
        enum_case(PreconfiguredMonitor);
    }
    throw std::out_of_range("Unexpected value for ocpp_device_model::RegisteredMonitorType_Internal");
}

RegistrationStatus_Internal to_internal_api(RegistrationStatus_External const& val) {
    using SrcT = RegistrationStatus_External;
    using TarT = RegistrationStatus_Internal;
    switch (val) {
        enum_case(Accepted);
        enum_case(OwnerInUse);
    }
    throw std::out_of_range("Unexpected value for ocpp_device_model::RegistrationStatus_External");
}

RegistrationStatus_External to_external_api(RegistrationStatus_Internal const& val) {
    using SrcT = RegistrationStatus_Internal;
    using TarT = RegistrationStatus_External;
    switch (val) {
        enum_case(Accepted);
        enum_case(OwnerInUse);
    }
    throw std::out_of_range("Unexpected value for ocpp_device_model::RegistrationStatus_Internal");
}

RegistrationRejectionReason_Internal to_internal_api(RegistrationRejectionReason_External const& val) {
    using SrcT = RegistrationRejectionReason_External;
    using TarT = RegistrationRejectionReason_Internal;
    switch (val) {
        enum_case(AlreadyDefined);
        enum_case(NoAttributes);
        enum_case(ReservedOwner);
        enum_case(InvalidNameKey);
        enum_case(ReservedVariable);
    }
    throw std::out_of_range("Unexpected value for ocpp_device_model::RegistrationRejectionReason_External");
}

RegistrationRejectionReason_External to_external_api(RegistrationRejectionReason_Internal const& val) {
    using SrcT = RegistrationRejectionReason_Internal;
    using TarT = RegistrationRejectionReason_External;
    switch (val) {
        enum_case(AlreadyDefined);
        enum_case(NoAttributes);
        enum_case(ReservedOwner);
        enum_case(InvalidNameKey);
        enum_case(ReservedVariable);
    }
    throw std::out_of_range("Unexpected value for ocpp_device_model::RegistrationRejectionReason_Internal");
}

VariableCharacteristics_Internal to_internal_api(VariableCharacteristics_External const& val) {
    VariableCharacteristics_Internal result;
    result.data_type = to_internal_api(val.data_type);
    result.supports_monitoring = val.supports_monitoring;
    result.unit = val.unit;
    result.min_limit = val.min_limit;
    result.max_limit = val.max_limit;
    result.max_elements = val.max_elements;
    result.values_list = val.values_list;
    return result;
}

VariableCharacteristics_External to_external_api(VariableCharacteristics_Internal const& val) {
    VariableCharacteristics_External result;
    result.data_type = to_external_api(val.data_type);
    result.supports_monitoring = val.supports_monitoring;
    result.unit = val.unit;
    result.min_limit = val.min_limit;
    result.max_limit = val.max_limit;
    result.max_elements = val.max_elements;
    result.values_list = val.values_list;
    return result;
}

VariableAttribute_Internal to_internal_api(VariableAttribute_External const& val) {
    VariableAttribute_Internal result;
    result.type = opt_to_internal(val.type);
    result.value = val.value;
    result.mutability = opt_to_internal(val.mutability);
    result.persistent = val.persistent;
    result.constant = val.constant;
    return result;
}

VariableAttribute_External to_external_api(VariableAttribute_Internal const& val) {
    VariableAttribute_External result;
    result.type = opt_to_external(val.type);
    result.value = val.value;
    result.mutability = opt_to_external(val.mutability);
    result.persistent = val.persistent;
    result.constant = val.constant;
    return result;
}

RegisteredMonitor_Internal to_internal_api(RegisteredMonitor_External const& val) {
    RegisteredMonitor_Internal result;
    result.type = to_internal_api(val.type);
    result.value = val.value;
    result.severity = val.severity;
    result.transaction = val.transaction;
    result.monitor_type = to_internal_api(val.monitor_type);
    return result;
}

RegisteredMonitor_External to_external_api(RegisteredMonitor_Internal const& val) {
    RegisteredMonitor_External result;
    result.type = to_external_api(val.type);
    result.value = val.value;
    result.severity = val.severity;
    result.transaction = val.transaction;
    result.monitor_type = to_external_api(val.monitor_type);
    return result;
}

RegisteredVariable_Internal to_internal_api(RegisteredVariable_External const& val) {
    RegisteredVariable_Internal result;
    result.variable = to_internal_api(val.variable);
    result.characteristics = to_internal_api(val.characteristics);
    result.attributes = vec_to_internal(val.attributes);
    result.monitors = opt_vec_to_internal(val.monitors);
    return result;
}

RegisteredVariable_External to_external_api(RegisteredVariable_Internal const& val) {
    RegisteredVariable_External result;
    result.variable = to_external_api(val.variable);
    result.characteristics = to_external_api(val.characteristics);
    result.attributes = vec_to_external(val.attributes);
    result.monitors = opt_vec_to_external(val.monitors);
    return result;
}

RegisteredComponent_Internal to_internal_api(RegisteredComponent_External const& val) {
    RegisteredComponent_Internal result;
    result.component = to_internal_api(val.component);
    result.variables = vec_to_internal(val.variables);
    return result;
}

RegisteredComponent_External to_external_api(RegisteredComponent_Internal const& val) {
    RegisteredComponent_External result;
    result.component = to_external_api(val.component);
    result.variables = vec_to_external(val.variables);
    return result;
}

Registration_Internal to_internal_api(Registration_External const& val) {
    Registration_Internal result;
    result.owner = val.owner;
    result.components = vec_to_internal(val.components);
    return result;
}

Registration_External to_external_api(Registration_Internal const& val) {
    Registration_External result;
    result.owner = val.owner;
    result.components = vec_to_external(val.components);
    return result;
}

RejectedVariable_Internal to_internal_api(RejectedVariable_External const& val) {
    RejectedVariable_Internal result;
    result.component = to_internal_api(val.component);
    result.variable = to_internal_api(val.variable);
    result.reason = to_internal_api(val.reason);
    result.defined_by = val.defined_by;
    return result;
}

RejectedVariable_External to_external_api(RejectedVariable_Internal const& val) {
    RejectedVariable_External result;
    result.component = to_external_api(val.component);
    result.variable = to_external_api(val.variable);
    result.reason = to_external_api(val.reason);
    result.defined_by = val.defined_by;
    return result;
}

RegistrationResult_Internal to_internal_api(RegistrationResult_External const& val) {
    RegistrationResult_Internal result;
    result.owner = val.owner;
    result.status = to_internal_api(val.status);
    result.rejected = vec_to_internal(val.rejected);
    return result;
}

RegistrationResult_External to_external_api(RegistrationResult_Internal const& val) {
    RegistrationResult_External result;
    result.owner = val.owner;
    result.status = to_external_api(val.status);
    result.rejected = vec_to_external(val.rejected);
    return result;
}

WriteRequest_Internal to_internal_api(WriteRequest_External const& val) {
    WriteRequest_Internal result;
    result.owner = val.owner;
    result.component = to_internal_api(val.component);
    result.variable = to_internal_api(val.variable);
    result.attribute_type = to_internal_api(val.attribute_type);
    result.value = val.value;
    result.source = val.source;
    return result;
}

WriteRequest_External to_external_api(WriteRequest_Internal const& val) {
    WriteRequest_External result;
    result.owner = val.owner;
    result.component = to_external_api(val.component);
    result.variable = to_external_api(val.variable);
    result.attribute_type = to_external_api(val.attribute_type);
    result.value = val.value;
    result.source = val.source;
    return result;
}

ValueChange_Internal to_internal_api(ValueChange_External const& val) {
    ValueChange_Internal result;
    result.owner = val.owner;
    result.component = to_internal_api(val.component);
    result.variable = to_internal_api(val.variable);
    result.attribute_type = opt_to_internal(val.attribute_type);
    result.value = val.value;
    return result;
}

ValueChange_External to_external_api(ValueChange_Internal const& val) {
    ValueChange_External result;
    result.owner = val.owner;
    result.component = to_external_api(val.component);
    result.variable = to_external_api(val.variable);
    result.attribute_type = opt_to_external(val.attribute_type);
    result.value = val.value;
    return result;
}

#undef enum_case

} // namespace everest::lib::API::V1_0::types::ocpp_device_model
