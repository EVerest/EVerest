// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Pins the OCPP 1.6 conversion of errors that are not MREC errors: the OCPP error map, the
// EvseManager Inoperative error and the default. The expected values are typed out by hand, never
// taken from the production mapping tables.

#include "v16_error_test_helpers.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace ocpp_multi;
using namespace v16_error_test;

constexpr const char* UNSET = "<unset>";

struct Expected {
    std::string_view error_code;
    bool is_fault;
    std::string info;
    std::string vendor_id;
    std::string vendor_error_code;
};

class ChargePointV16ErrorConvertFreeze : public ChargePointV16ErrorTest {
protected:
    // compares every field of the ErrorInfo handed to libocpp
    void expect_converted(const Everest::error::Error& error, const Expected& expected) {
        const auto info = m_chargepoint.convert_error(error);
        EXPECT_EQ(info.uuid, ERROR_UUID);
        EXPECT_EQ(ocpp::v16::conversions::charge_point_error_code_to_string(info.error_code), expected.error_code);
        EXPECT_EQ(info.is_fault, expected.is_fault);
        EXPECT_EQ(opt_str(info.info), expected.info);
        EXPECT_EQ(opt_str(info.vendor_id), expected.vendor_id);
        EXPECT_EQ(opt_str(info.vendor_error_code), expected.vendor_error_code);
        EXPECT_EQ(info.timestamp.to_rfc3339(), ocpp::DateTime(error.timestamp).to_rfc3339());
    }
};

TEST_F(ChargePointV16ErrorConvertFreeze, OcppMappedPowermeterFault) {
    expect_converted(make_error("powermeter/CommunicationFault", "meter offline"),
                     {"PowerMeterFailure", false, UNSET, "meter offline", UNSET});
}

// the message is sent as vendorId, which is set even when the message is empty
TEST_F(ChargePointV16ErrorConvertFreeze, OcppMappedEmptyMessageSetsEmptyVendorId) {
    expect_converted(make_error("powermeter/CommunicationFault"), {"PowerMeterFailure", false, UNSET, "", UNSET});
}

TEST_F(ChargePointV16ErrorConvertFreeze, OcppMappedMatchIsSubstring) {
    expect_converted(make_error("vendor/powermeter/CommunicationFault_x", "meter offline"),
                     {"PowerMeterFailure", false, UNSET, "meter offline", UNSET});
}

TEST_F(ChargePointV16ErrorConvertFreeze, OcppMappedVendorIdTruncatedTo255) {
    expect_converted(make_error("powermeter/CommunicationFault", std::string(300, 'x')),
                     {"PowerMeterFailure", false, UNSET, std::string(255, 'x'), UNSET});
}

TEST_F(ChargePointV16ErrorConvertFreeze, InoperativeIsFaultWithCausedBy) {
    expect_converted(make_error("evse_manager/Inoperative", "m"),
                     {"OtherError", true, "caused_by:m", "error-vendor", "a description"});
}

TEST_F(ChargePointV16ErrorConvertFreeze, InoperativeEmptyMessage) {
    expect_converted(make_error("evse_manager/Inoperative"),
                     {"OtherError", true, "caused_by:", "error-vendor", "a description"});
}

TEST_F(ChargePointV16ErrorConvertFreeze, InoperativeTruncation) {
    const auto error = make_error("evse_manager/Inoperative", std::string(60, 'm'), {},
                                  ImplementationIdentifier("bsp_1", "main", Mapping(1, 1)), std::string(60, 'd'));
    expect_converted(error,
                     {"OtherError", true, "caused_by:" + std::string(40, 'm'), "error-vendor", std::string(50, 'd')});
}

// unlike the MREC and OCPP maps, Inoperative is matched exactly
TEST_F(ChargePointV16ErrorConvertFreeze, InoperativeMatchIsExact) {
    expect_converted(make_error("evse_manager/Inoperative_x", "m"),
                     {"OtherError", false, "bsp_1->main", "m", "Inoperative_x/"});
}

TEST_F(ChargePointV16ErrorConvertFreeze, DefaultUsesOriginMessageAndTypeSubType) {
    expect_converted(make_error("evse_board_support/VendorError", "msg", "sub"),
                     {"OtherError", false, "bsp_1->main", "msg", "VendorError/sub"});
}

TEST_F(ChargePointV16ErrorConvertFreeze, DefaultEmptyMessageSetsEmptyVendorId) {
    expect_converted(make_error("evse_board_support/VendorError", "", "sub"),
                     {"OtherError", false, "bsp_1->main", "", "VendorError/sub"});
}

TEST_F(ChargePointV16ErrorConvertFreeze, DefaultOriginMappingDoesNotChangeInfo) {
    for (const auto& mapping :
         {std::optional<Mapping>{}, std::optional<Mapping>{Mapping(2)}, std::optional<Mapping>{Mapping(1, 1)}}) {
        SCOPED_TRACE(mapping.has_value() ? std::to_string(mapping->evse) : std::string("no mapping"));
        expect_converted(make_error("evse_board_support/VendorError", "msg", "sub",
                                    ImplementationIdentifier("bsp_1", "main", mapping)),
                         {"OtherError", false, "bsp_1->main", "msg", "VendorError/sub"});
    }
}

TEST_F(ChargePointV16ErrorConvertFreeze, DefaultTruncation) {
    const auto error = make_error("evse_board_support/VendorError", std::string(300, 'x'), std::string(60, 's'),
                                  ImplementationIdentifier(std::string(60, 'o'), "main", Mapping(1, 1)));
    expect_converted(error, {"OtherError", false, std::string(50, 'o'), std::string(255, 'x'),
                             "VendorError/" + std::string(38, 's')});
}

// The MREC map is tried before the OCPP map. Reordering the branches must change this test.
TEST_F(ChargePointV16ErrorConvertFreeze, MrecTakesPrecedenceOverOcppMap) {
    expect_converted(make_error("powermeter/CommunicationFault/evse_manager/MREC5OverVoltage"),
                     {"OverVoltage", false, UNSET, "https://chargex.inl.gov", "CX005"});
}

} // namespace
