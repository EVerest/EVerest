// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "nlohmann/json_fwd.hpp"
#include <everest_api_types/ocpp_device_model/API.hpp>

namespace everest::lib::API::V1_0::types::ocpp_device_model {

using json = nlohmann::json;

#define create_json_interface(A)                                                                                       \
    void to_json(json& j, A const& k) noexcept;                                                                        \
    void from_json(const json& j, A& k)

create_json_interface(DataEnum);
create_json_interface(MutabilityEnum);
create_json_interface(MonitorEnum);
create_json_interface(RegisteredMonitorType);
create_json_interface(RegistrationStatus);
create_json_interface(RegistrationRejectionReason);
create_json_interface(VariableCharacteristics);
create_json_interface(VariableAttribute);
create_json_interface(RegisteredMonitor);
create_json_interface(RegisteredVariable);
create_json_interface(RegisteredComponent);
create_json_interface(Registration);
create_json_interface(RejectedVariable);
create_json_interface(RegistrationResult);
create_json_interface(WriteRequest);
create_json_interface(ValueChange);
create_json_interface(GetRegistrations);

#undef create_json_interface

} // namespace everest::lib::API::V1_0::types::ocpp_device_model
