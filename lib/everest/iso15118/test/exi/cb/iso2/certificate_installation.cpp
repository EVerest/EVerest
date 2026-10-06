// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include <iso15118/message_2/certificate_installation.hpp>
#include <iso15118/message_2/variant.hpp>

#include <cbv2g/common/exi_basetypes.h>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

namespace {

message_2::CertificateInstallationRequest request_with_serial(const std::vector<uint8_t>& serial) {
    message_2::CertificateInstallationRequest req;
    req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    req.oem_provisioning_cert = {0x30, 0x82, 0x01, 0x00};
    req.root_certificate_ids.push_back({"CN=V2GRootCA,O=EVerest,C=DE", serial});
    return req;
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

        const auto doc = decode_helper(serialize_helper(request_with_serial(serial)));

        THEN("It survives the round trip whole, rather than being truncated to 64 bits") {
            REQUIRE(doc.V2G_Message.Body.CertificateInstallationReq_isUsed);
            const auto& list = doc.V2G_Message.Body.CertificateInstallationReq.ListOfRootCertificateIDs;
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

SCENARIO("Se/Deserialize ISO-2 certificate installation messages") {

    // The SECC forwards the request to the backend as raw EXI, so the decoder marks the type but does
    // not decode a message struct. Encoding it is the EV side of the pair.
    GIVEN("Serialize certificate_installation_req") {
        message_2::CertificateInstallationRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.id = "id1";
        req.oem_provisioning_cert = {0x30, 0x82, 0x01, 0x02};
        req.root_certificate_ids.push_back({"CN=V2G Root CA", {0x30, 0x39}});

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("The type is recognised but the body stays undecoded (relay-only)") {
            REQUIRE(variant.get_type() == message_2::Type::CertificateInstallationReq);
            REQUIRE(variant.get_if<message_2::CertificateInstallationRequest>() == nullptr);
            REQUIRE_FALSE(variant.get_exi_payload().empty());
        }
    }

    // The SECC relays a successful response from the backend as raw EXI but builds this one itself for
    // an out-of-sequence request, so the schema-mandatory placeholders have to survive a round trip.
    GIVEN("Round-trip certificate_installation_res with FAILED_SequenceError placeholders") {
        message_2::CertificateInstallationResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::FAILED_SequenceError;
        res.sa_provisioning_chain.certificate = {0x00};
        res.contract_chain.id = "contractSignatureCertChain";
        res.contract_chain.certificate = {0x00};
        res.encrypted_private_key = {0x00};
        res.dh_public_key = {0x00};
        res.emaid = "00000000000000";

        const auto serialized = serialize_helper(res);
        const auto doc = decode_helper(serialized);

        THEN("The encoded response converts back field for field") {
            REQUIRE(doc.V2G_Message.Body.CertificateInstallationRes_isUsed);
            const auto msg = to_response<message_2::CertificateInstallationResponse>(
                doc, doc.V2G_Message.Body.CertificateInstallationRes);
            REQUIRE(msg.response_code == ResponseCode::FAILED_SequenceError);
            REQUIRE(msg.contract_chain.id.has_value());
            REQUIRE(msg.contract_chain.id.value() == "contractSignatureCertChain");
            REQUIRE(msg.emaid == "00000000000000");
        }
    }

    GIVEN("Round-trip certificate_installation_res with a certificate chain") {
        message_2::CertificateInstallationResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.sa_provisioning_chain.certificate = {0x30, 0x82, 0x01, 0x02};
        res.contract_chain.id = "chain";
        res.contract_chain.certificate = {0x30, 0x82, 0x03, 0x04};
        res.contract_chain.sub_certificates.push_back({0x30, 0x82, 0x05, 0x06});
        res.encrypted_private_key = {0xAA, 0xBB, 0xCC};
        res.dh_public_key = {0x04, 0x01, 0x02};
        res.emaid = "DEPNX123456789";

        const auto serialized = serialize_helper(res);
        const auto doc = decode_helper(serialized);

        THEN("The encoded response converts back field for field") {
            REQUIRE(doc.V2G_Message.Body.CertificateInstallationRes_isUsed);
            const auto msg = to_response<message_2::CertificateInstallationResponse>(
                doc, doc.V2G_Message.Body.CertificateInstallationRes);
            REQUIRE(msg.response_code == ResponseCode::OK);
            REQUIRE(msg.sa_provisioning_chain.certificate == res.sa_provisioning_chain.certificate);
            REQUIRE(msg.contract_chain.certificate == res.contract_chain.certificate);
            REQUIRE(msg.contract_chain.sub_certificates.size() == 1);
            REQUIRE(msg.contract_chain.sub_certificates.front() == res.contract_chain.sub_certificates.front());
            REQUIRE(msg.encrypted_private_key == res.encrypted_private_key);
            REQUIRE(msg.dh_public_key == res.dh_public_key);
            REQUIRE(msg.emaid == "DEPNX123456789");
        }
    }
}
