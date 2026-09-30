// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "stubs/v2_chargepoint_stub.hpp"

#include <ModuleAdapterStub.hpp>
#include <v16_chargepoint.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

namespace v16_error_test {

constexpr const char* ERROR_UUID = "mrec-error-uuid-1";

// expose the protected test seam
struct TestChargePointV16 : public ocpp_multi::ChargePointV16 {
    using ocpp_multi::ChargePointV16::ChargePointV16;
    using ocpp_multi::ChargePointV16::convert_error;
    using ocpp_multi::ChargePointV16::dispatch_error_event;
};

inline Everest::error::Error
make_error(std::string_view type, const std::string& message = {}, const std::string& sub_type = {},
           const ImplementationIdentifier& origin = ImplementationIdentifier("bsp_1", "main", Mapping(1, 1)),
           const std::string& description = "a description", const std::string& vendor_id = "error-vendor") {
    Everest::error::Error error;
    error.type = std::string(type);
    error.sub_type = sub_type;
    error.message = message;
    error.description = description;
    error.origin = origin;
    error.vendor_id = vendor_id;
    error.timestamp = date::utc_clock::now();
    error.uuid = Everest::error::UUID(ERROR_UUID);
    return error;
}

inline std::string opt_str(const std::optional<ocpp::CiString<50>>& value) {
    return value.has_value() ? value->get() : std::string("<unset>");
}

inline std::string opt_str(const std::optional<ocpp::CiString<255>>& value) {
    return value.has_value() ? value->get() : std::string("<unset>");
}

class ChargePointV16ErrorTest : public testing::Test {
protected:
    module::stub::QuietModuleAdapterStub m_adapter;
    Requirement m_requirement{"ocpp", 0};
    evse_securityIntf m_security{&m_adapter, m_requirement, "security", std::nullopt};
    testing::NiceMock<stubs::GenericChargePointCallbacksMock> m_callbacks;
    TestChargePointV16 m_chargepoint{m_callbacks, m_security};
};

} // namespace v16_error_test
