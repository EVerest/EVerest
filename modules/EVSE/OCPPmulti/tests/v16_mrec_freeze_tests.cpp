// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Pins the OCPP 1.6 MREC error reporting. The expected values come from the hand-typed table in
// mrec_fixture.hpp, never from the production mapping tables.

#include "mrec_fixture.hpp"
#include "stubs/v2_chargepoint_stub.hpp"

#include <ModuleAdapterStub.hpp>
#include <v16_chargepoint.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <set>
#include <string>

namespace {

using namespace ocpp_multi;
using testing::NiceMock;

constexpr const char* ERROR_UUID = "mrec-error-uuid-1";

// expose the protected test seam
struct TestChargePointV16 : public ChargePointV16 {
    using ChargePointV16::ChargePointV16;
    using ChargePointV16::convert_error;
};

Everest::error::Error make_error(std::string_view type, const std::string& message = {},
                                 const std::string& sub_type = {}) {
    Everest::error::Error error;
    error.type = std::string(type);
    error.sub_type = sub_type;
    error.message = message;
    error.description = "a description";
    error.origin = ImplementationIdentifier("bsp_1", "main", Mapping(1, 1));
    error.vendor_id = "error-vendor";
    error.timestamp = date::utc_clock::now();
    error.uuid = Everest::error::UUID(ERROR_UUID);
    return error;
}

std::string opt_str(const std::optional<ocpp::CiString<50>>& value) {
    return value.has_value() ? value->get() : std::string("<unset>");
}

std::string opt_str(const std::optional<ocpp::CiString<255>>& value) {
    return value.has_value() ? value->get() : std::string("<unset>");
}

class ChargePointV16MrecFreeze : public testing::Test {
protected:
    module::stub::QuietModuleAdapterStub m_adapter;
    Requirement m_requirement{"ocpp", 0};
    evse_securityIntf m_security{&m_adapter, m_requirement, "security", std::nullopt};
    NiceMock<stubs::GenericChargePointCallbacksMock> m_callbacks;
    TestChargePointV16 m_chargepoint{m_callbacks, m_security};

    // compares every field of the ErrorInfo handed to libocpp
    static void expect_mrec_error_info(const ocpp::v16::ErrorInfo& info, const Everest::error::Error& error,
                                       const mrec_fixture::Entry& entry, const std::string& expected_info) {
        EXPECT_EQ(info.uuid, ERROR_UUID);
        EXPECT_EQ(ocpp::v16::conversions::charge_point_error_code_to_string(info.error_code), entry.v16_error_code);
        EXPECT_FALSE(info.is_fault);
        EXPECT_EQ(opt_str(info.info), expected_info);
        EXPECT_EQ(opt_str(info.vendor_id), mrec_fixture::V16_VENDOR_ID);
        EXPECT_EQ(opt_str(info.vendor_error_code), entry.v16_vendor_code);
        EXPECT_EQ(info.timestamp.to_rfc3339(), ocpp::DateTime(error.timestamp).to_rfc3339());
    }
};

TEST(MrecFixture, HasExactlyTheFrozenKeys) {
    std::set<std::string_view> keys;
    for (const auto& entry : mrec_fixture::ENTRIES) {
        EXPECT_TRUE(keys.insert(entry.type).second) << "duplicate key " << entry.type;
    }
    EXPECT_EQ(keys.size(), 23U);
}

TEST_F(ChargePointV16MrecFreeze, EmptyMessageLeavesInfoUnset) {
    for (const auto& entry : mrec_fixture::ENTRIES) {
        SCOPED_TRACE(std::string(entry.type));
        const auto error = make_error(entry.type);
        expect_mrec_error_info(m_chargepoint.convert_error(error), error, entry, "<unset>");
    }
}

TEST_F(ChargePointV16MrecFreeze, MessageIsSentAsInfo) {
    for (const auto& entry : mrec_fixture::ENTRIES) {
        SCOPED_TRACE(std::string(entry.type));
        const auto error = make_error(entry.type, "sensor reports fault");
        expect_mrec_error_info(m_chargepoint.convert_error(error), error, entry, "sensor reports fault");
    }
}

TEST_F(ChargePointV16MrecFreeze, OverlongMessageIsTruncatedTo50) {
    const std::string message(60, 'm');
    for (const auto& entry : mrec_fixture::ENTRIES) {
        SCOPED_TRACE(std::string(entry.type));
        const auto error = make_error(entry.type, message);
        expect_mrec_error_info(m_chargepoint.convert_error(error), error, entry, std::string(50, 'm'));
    }
}

TEST_F(ChargePointV16MrecFreeze, SubTypeDoesNotChangeTheResult) {
    for (const auto& entry : mrec_fixture::ENTRIES) {
        SCOPED_TRACE(std::string(entry.type));
        const auto error = make_error(entry.type, "sensor reports fault", "some_sub_type");
        expect_mrec_error_info(m_chargepoint.convert_error(error), error, entry, "sensor reports fault");
    }
}

// 1.6 matches MREC keys by substring: a type that merely contains a key is still mapped,
// unlike 2.x (see V2MrecMatchIsExact). Moving 1.6 to exact matching must change this test.
TEST_F(ChargePointV16MrecFreeze, V16MrecMatchIsSubstring) {
    const auto error = make_error("vendor/evse_manager/MREC5OverVoltage_ext");
    const auto info = m_chargepoint.convert_error(error);
    EXPECT_EQ(ocpp::v16::conversions::charge_point_error_code_to_string(info.error_code), "OverVoltage");
    EXPECT_EQ(opt_str(info.vendor_id), mrec_fixture::V16_VENDOR_ID);
    EXPECT_EQ(opt_str(info.vendor_error_code), "CX005");
}

} // namespace
