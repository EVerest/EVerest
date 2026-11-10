// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest_api_types/ocpp_device_model/API.hpp>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "generated/types/ocpp_device_model.hpp"
#pragma GCC diagnostic pop

namespace everest::lib::API::V1_0::types::ocpp_device_model {

#define create_interface(A)                                                                                            \
    using A##_Internal = ::types::ocpp_device_model::A;                                                                \
    using A##_External = A;                                                                                            \
    A##_Internal to_internal_api(A##_External const& val);                                                             \
    A##_External to_external_api(A##_Internal const& val)

create_interface(DataEnum);
create_interface(MutabilityEnum);
create_interface(MonitorEnum);
create_interface(RegisteredMonitorType);
create_interface(RegistrationStatus);
create_interface(RegistrationRejectionReason);
create_interface(VariableCharacteristics);
create_interface(VariableAttribute);
create_interface(RegisteredMonitor);
create_interface(RegisteredVariable);
create_interface(RegisteredComponent);
create_interface(Registration);
create_interface(RejectedVariable);
create_interface(RegistrationResult);
create_interface(WriteRequest);
create_interface(ValueChange);

#undef create_interface

} // namespace everest::lib::API::V1_0::types::ocpp_device_model
