// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ocpp_device_model/codec.hpp"
#include "nlohmann/json.hpp"
#include "ocpp_device_model/API.hpp"
#include "ocpp_device_model/json_codec.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::ocpp_device_model {

#define create_serialize_impl(A)                                                                                       \
    std::string serialize(A const& val) noexcept { return utilities::dump_json(val); }                                 \
    std::ostream& operator<<(std::ostream& os, A const& val) {                                                         \
        os << serialize(val);                                                                                          \
        return os;                                                                                                     \
    }                                                                                                                  \
    template <> A deserialize(std::string_view val) { return utilities::parse_json<A>(val); }

create_serialize_impl(DataEnum);
create_serialize_impl(MutabilityEnum);
create_serialize_impl(MonitorEnum);
create_serialize_impl(RegisteredMonitorType);
create_serialize_impl(RegistrationStatus);
create_serialize_impl(RegistrationRejectionReason);
create_serialize_impl(VariableCharacteristics);
create_serialize_impl(VariableAttribute);
create_serialize_impl(RegisteredMonitor);
create_serialize_impl(RegisteredVariable);
create_serialize_impl(RegisteredComponent);
create_serialize_impl(Registration);
create_serialize_impl(RejectedVariable);
create_serialize_impl(RegistrationResult);
create_serialize_impl(WriteRequest);
create_serialize_impl(ValueChange);
create_serialize_impl(GetRegistrations);

#undef create_serialize_impl

} // namespace everest::lib::API::V1_0::types::ocpp_device_model
