// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Pins the OCPP 1.6 MREC error reporting. The expected values come from the hand-typed table in
// mrec_fixture.hpp, never from the production mapping tables.

#include "mrec_fixture.hpp"
#include "v16_error_test_helpers.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace {

using namespace ocpp_multi;
using namespace v16_error_test;

class ChargePointV16MrecFreeze : public ChargePointV16ErrorTest {
protected:
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
