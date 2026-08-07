// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/certificate_installation.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 certificate installation messages") {

    // The SECC forwards the request to the backend as raw EXI, so the decoder marks the type but does not
    // decode a message struct (see message_2::Variant). Encoding it is the EV side of the pair.
    GIVEN("Serialize certificate_installation_req") {
        message_2::CertificateInstallationRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.id = "id1";
        req.oem_provisioning_cert = {0x30, 0x82, 0x01, 0x02};
        req.root_certificate_ids.push_back({"CN=V2G Root CA", 12345});

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("The type is recognised but the body stays undecoded (relay-only)") {
            REQUIRE(variant.get_type() == message_2::Type::CertificateInstallationReq);
            REQUIRE(variant.get_if<message_2::CertificateInstallationRequest>() == nullptr);
            REQUIRE_FALSE(variant.get_exi_payload().empty());
        }
    }

    // The SECC relays a successful CertificateInstallationRes from the backend as raw EXI, but it builds
    // this response itself for an out-of-sequence request [V2G2-538]. Every element is schema-mandatory,
    // so the placeholders it fills in have to survive a round trip.
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
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::CertificateInstallationRes);
            const auto& msg = variant.get<message_2::CertificateInstallationResponse>();
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
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::CertificateInstallationRes);
            const auto& msg = variant.get<message_2::CertificateInstallationResponse>();
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
