// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include <iso15118/message_2/certificate_installation.hpp>
#include <iso15118/message_2/type.hpp>

#include <cbv2g/common/exi_basetypes.h>
#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>

#include "helper.hpp"

using namespace iso15118;

namespace {

message_2::CertificateInstallationRequest request_with_serial(const std::vector<uint8_t>& serial) {
    message_2::CertificateInstallationRequest req;
    req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    req.oem_provisioning_cert = {0x30, 0x82, 0x01, 0x00};
    req.root_certificate_ids.push_back({"CN=V2GRootCA,O=EVerest,C=DE", serial});
    return req;
}

const struct iso2_ListOfRootCertificateIDsType& decode_list(const std::vector<uint8_t>& exi, iso2_exiDocument& doc) {
    doc = decode_raw(exi);
    REQUIRE(doc.V2G_Message.Body.CertificateInstallationReq_isUsed);
    return doc.V2G_Message.Body.CertificateInstallationReq.ListOfRootCertificateIDs;
}

std::vector<uint8_t> decoded_serial(const struct iso2_X509IssuerSerialType& entry) {
    uint8_t bytes[EXI_BASETYPES_MAX_OCTETS_SUPPORTED] = {0};
    size_t length = 0;
    REQUIRE(exi_basetypes_convert_bytes_from_unsigned(&entry.X509SerialNumber.data, bytes, &length, sizeof(bytes)) ==
            0);
    return std::vector<uint8_t>(bytes, bytes + length);
}

} // namespace

SCENARIO("Serialize the ISO-2 CertificateInstallationReq ListOfRootCertificateIDs") {

    GIVEN("A 20-octet serial number, the width RFC 5280 allows and production CAs emit") {
        const std::vector<uint8_t> serial{0x7F, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
                                          0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01, 0x02, 0x03, 0x04};

        const auto serialized = serialize_helper(request_with_serial(serial));
        iso2_exiDocument doc{};
        const auto& list = decode_list(serialized, doc);

        THEN("It survives the round trip whole, rather than being truncated to 64 bits") {
            REQUIRE(list.RootCertificateID.arrayLen == 1);
            const auto& entry = list.RootCertificateID.array[0];
            REQUIRE_FALSE(entry.X509SerialNumber.is_negative);
            REQUIRE(decoded_serial(entry) == serial);
        }
    }

    GIVEN("A serial wider than the EXI converter can hold") {
        const std::vector<uint8_t> serial(message_2::MAX_SERIAL_NUMBER_BYTES + 1, 0x5A);

        THEN("Serialization refuses it instead of overrunning the converter's octet buffer") {
            REQUIRE_THROWS_AS(serialize_helper(request_with_serial(serial)), std::runtime_error);
        }
    }
}
