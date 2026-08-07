// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <set>

#include "helper.hpp"

namespace {

using types::powermeter::OCMFIdentificationFlags;
using types::powermeter::OCMFIdentificationType;
using types::powermeter::OCMFUserIdentificationStatus;

TEST(Sdm630EvHelper, ToFloat32DecodesIeee754MsRegisterFirst) {
    // 240.5 = 0x43708000
    const transport::DataVector data{0x43, 0x70, 0x80, 0x00};
    EXPECT_FLOAT_EQ(modbus_utils::to_float32(data, 0), 240.5F);

    // -100.0 = 0xC2C80000
    const transport::DataVector negative{0xC2, 0xC8, 0x00, 0x00};
    EXPECT_FLOAT_EQ(modbus_utils::to_float32(negative, 0), -100.0F);

    const transport::DataVector zero{0x00, 0x00, 0x00, 0x00};
    EXPECT_FLOAT_EQ(modbus_utils::to_float32(zero, 0), 0.0F);
}

TEST(Sdm630EvHelper, ToFloat32ThrowsOnShortData) {
    const transport::DataVector data{0x43, 0x70};
    EXPECT_THROW(modbus_utils::to_float32(data, 0), std::out_of_range);
}

TEST(Sdm630EvHelper, FloatToRegistersRoundTrips) {
    const auto registers = modbus_utils::float_to_registers(240.5F);
    ASSERT_EQ(registers.size(), 2U);
    EXPECT_EQ(registers[0], 0x4370);
    EXPECT_EQ(registers[1], 0x8000);
}

TEST(Sdm630EvHelper, Uint32ToRegistersIsMsRegisterFirst) {
    const auto registers = modbus_utils::uint32_to_registers(0x0001FFFFU);
    ASSERT_EQ(registers.size(), 2U);
    EXPECT_EQ(registers[0], 0x0001);
    EXPECT_EQ(registers[1], 0xFFFF);
}

TEST(Sdm630EvHelper, BcdTimeEncoding) {
    // 2026-08-07 13:22:04 is a Friday (tm_wday = 5).
    std::tm t{};
    t.tm_sec = 4;
    t.tm_min = 22;
    t.tm_hour = 13;
    t.tm_wday = 5;
    t.tm_mday = 7;
    t.tm_mon = 7; // August
    t.tm_year = 126;

    const auto registers = bcd_utils::time_to_registers(t);
    ASSERT_EQ(registers.size(), 4U);
    EXPECT_EQ(registers[0], 0x0422); // sec | min
    EXPECT_EQ(registers[1], 0x1305); // hour | weekday
    EXPECT_EQ(registers[2], 0x0708); // day | month
    EXPECT_EQ(registers[3], 0x2620); // year | century
}

TEST(Sdm630EvHelper, BcdSundayMapsToSeven) {
    std::tm t{};
    t.tm_wday = 0;
    const auto registers = bcd_utils::time_to_registers(t);
    EXPECT_EQ(registers[1] & 0xFF, 0x07);
}

TEST(Sdm630EvHelper, FixedRegistersAlwaysEmitFullField) {
    const auto registers = modbus_utils::string_to_fixed_registers("AB", 20);
    ASSERT_EQ(registers.size(), 20U);
    EXPECT_EQ(registers[0], 0x4142);
    for (std::size_t i = 1; i < registers.size(); ++i) {
        EXPECT_EQ(registers[i], 0) << "register " << i << " not zero padded";
    }
}

TEST(Sdm630EvHelper, FixedRegistersTruncateOversizeInput) {
    const std::string long_string(45, 'X');
    const auto registers = modbus_utils::string_to_fixed_registers(long_string, 20);
    ASSERT_EQ(registers.size(), 20U);
    EXPECT_EQ(registers[19], 0x5858);
}

TEST(Sdm630EvHelper, IdentificationStatusIsInverted) {
    EXPECT_EQ(ocmf::is_to_value(OCMFUserIdentificationStatus::ASSIGNED), 0x0000);
    EXPECT_EQ(ocmf::is_to_value(OCMFUserIdentificationStatus::NOT_ASSIGNED), 0x0001);
}

TEST(Sdm630EvHelper, AllIdentificationFlagsMapToDistinctBits) {
    const std::vector<OCMFIdentificationFlags> all_flags{
        OCMFIdentificationFlags::RFID_NONE,      OCMFIdentificationFlags::RFID_PLAIN,
        OCMFIdentificationFlags::RFID_RELATED,   OCMFIdentificationFlags::RFID_PSK,
        OCMFIdentificationFlags::OCPP_NONE,      OCMFIdentificationFlags::OCPP_RS,
        OCMFIdentificationFlags::OCPP_AUTH,      OCMFIdentificationFlags::OCPP_RS_TLS,
        OCMFIdentificationFlags::OCPP_AUTH_TLS,  OCMFIdentificationFlags::OCPP_CACHE,
        OCMFIdentificationFlags::OCPP_WHITELIST, OCMFIdentificationFlags::OCPP_CERTIFIED,
        OCMFIdentificationFlags::ISO15118_NONE,  OCMFIdentificationFlags::ISO15118_PNC,
        OCMFIdentificationFlags::PLMN_NONE,      OCMFIdentificationFlags::PLMN_RING,
        OCMFIdentificationFlags::PLMN_SMS,
    };
    std::set<std::uint32_t> seen_bits;
    for (const auto flag : all_flags) {
        const std::uint32_t bit = ocmf::flag_to_bit(flag);
        EXPECT_NE(bit, 0U);
        EXPECT_TRUE(seen_bits.insert(bit).second) << "duplicate bit for flag";
    }
    EXPECT_EQ(ocmf::flags_to_bitfield(all_flags), 0x0001FFFFU);
}

TEST(Sdm630EvHelper, IdentificationTypeUsesDeviceValues) {
    EXPECT_EQ(ocmf::type_to_value(OCMFIdentificationType::NONE), 0x00);
    EXPECT_EQ(ocmf::type_to_value(OCMFIdentificationType::ISO14443), 0x03);
    EXPECT_EQ(ocmf::type_to_value(OCMFIdentificationType::ISO15693), 0x04);
    EXPECT_EQ(ocmf::type_to_value(OCMFIdentificationType::EMAID), 0x05);
    EXPECT_EQ(ocmf::type_to_value(OCMFIdentificationType::CENTRAL), 0x0A);
    EXPECT_EQ(ocmf::type_to_value(OCMFIdentificationType::PHONE_NUMBER), 0x10);
    EXPECT_EQ(ocmf::type_to_value(OCMFIdentificationType::KEY_CODE), 0x11);
}

TEST(Sdm630EvHelper, ChargePointIdTypeMapping) {
    EXPECT_EQ(ocmf::charge_point_id_type_to_value("EVSEID"), 1);
    EXPECT_EQ(ocmf::charge_point_id_type_to_value("CBIDC"), 2);
}

TEST(Sdm630EvHelper, AssembleOcmfKeepsCompleteDocument) {
    const std::string complete = R"(OCMF|{"FV":"1.0"}|{"SD":"AA"})";
    EXPECT_EQ(ocmf::assemble_ocmf(complete, "BB"), complete);
}

TEST(Sdm630EvHelper, AssembleOcmfAppendsSignatureSection) {
    const std::string payload = R"(OCMF|{"FV":"1.0"})";
    const std::string document = ocmf::assemble_ocmf(payload, "DEADBEEF");
    EXPECT_EQ(document, R"(OCMF|{"FV":"1.0"}|{"SA":"ECDSA-secp256r1-SHA256","SD":"DEADBEEF"})");
}

TEST(Sdm630EvHelper, AssembleOcmfAddsMissingPrefix) {
    const std::string document = ocmf::assemble_ocmf(R"({"FV":"1.0"})", "AA");
    EXPECT_EQ(document.rfind("OCMF|", 0), 0U);
}

TEST(Sdm630EvHelper, SignatureStatusStrings) {
    EXPECT_EQ(ocmf::signature_status_to_string(0x03), "signature OK");
    EXPECT_EQ(ocmf::signature_status_to_string(0x05), "invalid measurement");
    // Undocumented gap 0x0A..0x0F must not crash.
    EXPECT_EQ(ocmf::signature_status_to_string(0x0A), "unknown (10)");
}

TEST(Sdm630EvHelper, PublicKeyPrefixesDoNotDoubleMarker) {
    const std::string der_prefix = ocmf::PUBLIC_KEY_DER_PREFIX;
    EXPECT_EQ(der_prefix.substr(der_prefix.size() - 2), "04");
    EXPECT_STREQ(ocmf::PUBLIC_KEY_RAW_PREFIX, "04");
}

} // namespace
