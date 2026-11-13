// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "API.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::ocpp_device_model {

#define create_serialize_interface(A)                                                                                  \
    std::string serialize(A const& val) noexcept;                                                                      \
    std::ostream& operator<<(std::ostream& os, A const& val)

create_serialize_interface(DataEnum);
create_serialize_interface(MutabilityEnum);
create_serialize_interface(MonitorEnum);
create_serialize_interface(RegisteredMonitorType);
create_serialize_interface(RegistrationStatus);
create_serialize_interface(RegistrationRejectionReason);
create_serialize_interface(VariableCharacteristics);
create_serialize_interface(VariableAttribute);
create_serialize_interface(RegisteredMonitor);
create_serialize_interface(RegisteredVariable);
create_serialize_interface(RegisteredComponent);
create_serialize_interface(Registration);
create_serialize_interface(RejectedVariable);
create_serialize_interface(RegistrationResult);
create_serialize_interface(WriteRequest);
create_serialize_interface(ValueChange);
create_serialize_interface(GetRegistrations);

#undef create_serialize_interface

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::ocpp_device_model
