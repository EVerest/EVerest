// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message/certificate_installation.hpp>
#include <iso15118/message/variant.hpp>

#include "helper.hpp"

using namespace iso15118;

namespace dt = message_20::datatypes;

namespace {

template <typename Message> std::vector<uint8_t> serialize_large(const Message& message) {
    uint8_t buffer[8192];
    io::StreamOutputView out({buffer, sizeof(buffer)});
    const auto size = message_20::serialize(message, out);
    return std::vector<uint8_t>(buffer, buffer + size);
}

} // namespace

SCENARIO("Se/Deserialize certificate installation messages") {

    GIVEN("Round trip of a certificate_installation_req") {

        message_20::CertificateInstallationRequest req;
        req.header = message_20::Header{{0xF2, 0x19, 0x15, 0xB9, 0xDD, 0xDC, 0x12, 0xD1}, 1691411798};
        req.oem_provisioning_certificate_chain.id = "oemcert";
        req.oem_provisioning_certificate_chain.certificate = {0x30, 0x82, 0x01};
        req.oem_provisioning_certificate_chain.sub_certificates.emplace_back(std::vector<uint8_t>{0x30, 0x11});
        req.oem_provisioning_certificate_chain.sub_certificates.emplace_back(std::vector<uint8_t>{0x30, 0x12});
        auto& root = req.list_of_root_certificate_ids.root_certificate_id.emplace_back();
        root.issuer_name = "CN=V2G Root CA,O=EVerest";
        root.serial_number = 12345;
        req.maximum_contract_certificate_chains = 3;
        auto& emaids = req.prioritized_emaids.emplace();
        emaids.emplace_back("DE-ABC-C123ABC56-X");
        emaids.emplace_back("DE-DEF-C987ZYX12-Y");

        const auto encoded = serialize_large(req);
        const io::StreamInputView stream_view{encoded.data(), encoded.size()};
        message_20::Variant variant(io::v2gtp::PayloadType::Part20Main, stream_view);

        THEN("Every field survives") {
            REQUIRE(variant.get_type() == message_20::Type::CertificateInstallationReq);
            REQUIRE(variant.get_exi_payload() == encoded);

            const auto& msg = variant.get<message_20::CertificateInstallationRequest>();
            REQUIRE(msg.header.session_id == req.header.session_id);
            REQUIRE(msg.oem_provisioning_certificate_chain.id == "oemcert");
            REQUIRE(msg.oem_provisioning_certificate_chain.certificate == std::vector<uint8_t>{0x30, 0x82, 0x01});
            REQUIRE(msg.oem_provisioning_certificate_chain.sub_certificates.size() == 2);
            REQUIRE(msg.oem_provisioning_certificate_chain.sub_certificates[1] == std::vector<uint8_t>{0x30, 0x12});
            REQUIRE(msg.list_of_root_certificate_ids.root_certificate_id.size() == 1);
            REQUIRE(msg.list_of_root_certificate_ids.root_certificate_id[0].issuer_name == "CN=V2G Root CA,O=EVerest");
            REQUIRE(msg.list_of_root_certificate_ids.root_certificate_id[0].serial_number == 12345);
            REQUIRE(msg.maximum_contract_certificate_chains == 3);
            REQUIRE(msg.prioritized_emaids.has_value());
            REQUIRE(msg.prioritized_emaids->size() == 2);
            REQUIRE((*msg.prioritized_emaids)[0] == "DE-ABC-C123ABC56-X");
        }
    }

    GIVEN("A certificate_installation_req without optional elements") {

        message_20::CertificateInstallationRequest req;
        req.header = message_20::Header{{0xF2, 0x19, 0x15, 0xB9, 0xDD, 0xDC, 0x12, 0xD1}, 1691411798};
        req.oem_provisioning_certificate_chain.id = "id1";
        req.oem_provisioning_certificate_chain.certificate = {0x30};
        req.list_of_root_certificate_ids.root_certificate_id.emplace_back(dt::X509IssuerSerial{"CN=Root", 1});
        req.maximum_contract_certificate_chains = 1;

        const auto encoded = serialize_large(req);
        const io::StreamInputView stream_view{encoded.data(), encoded.size()};
        message_20::Variant variant(io::v2gtp::PayloadType::Part20Main, stream_view);

        THEN("Sub certificates and prioritized emaids stay absent") {
            REQUIRE(variant.get_type() == message_20::Type::CertificateInstallationReq);
            const auto& msg = variant.get<message_20::CertificateInstallationRequest>();
            REQUIRE(msg.oem_provisioning_certificate_chain.sub_certificates.empty());
            REQUIRE_FALSE(msg.prioritized_emaids.has_value());
        }
    }

    GIVEN("Round trip of a complete certificate_installation_res") {

        message_20::CertificateInstallationResponse res;
        res.header = message_20::Header{{0xF2, 0x19, 0x15, 0xB9, 0xDD, 0xDC, 0x12, 0xD1}, 1691411798};
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::Processing::Finished;
        res.cps_certificate_chain.certificate = {0x30, 0x01};
        res.cps_certificate_chain.sub_certificates.emplace_back(std::vector<uint8_t>{0x30, 0x02});
        res.signed_installation_data.id = "sid";
        res.signed_installation_data.contract_certificate_chain.certificate = {0x30, 0x03};
        res.signed_installation_data.contract_certificate_chain.sub_certificates.emplace_back(
            std::vector<uint8_t>{0x30, 0x04});
        res.signed_installation_data.ecdh_curve = dt::EcdhCurve::X448;
        res.signed_installation_data.dh_public_key = std::vector<uint8_t>(57, 0x04);
        res.signed_installation_data.x448_encrypted_private_key = std::vector<uint8_t>(84, 0xaa);
        res.remaining_contract_certificate_chains = 2;

        const auto encoded = serialize_large(res);
        const io::StreamInputView stream_view{encoded.data(), encoded.size()};
        message_20::Variant variant(io::v2gtp::PayloadType::Part20Main, stream_view);

        THEN("Every field survives") {
            REQUIRE(variant.get_type() == message_20::Type::CertificateInstallationRes);
            const auto& msg = variant.get<message_20::CertificateInstallationResponse>();
            REQUIRE(msg.response_code == dt::ResponseCode::OK);
            REQUIRE(msg.evse_processing == dt::Processing::Finished);
            REQUIRE(msg.cps_certificate_chain.certificate == std::vector<uint8_t>{0x30, 0x01});
            REQUIRE(msg.cps_certificate_chain.sub_certificates.size() == 1);
            REQUIRE(msg.signed_installation_data.id == "sid");
            REQUIRE(msg.signed_installation_data.contract_certificate_chain.certificate ==
                    std::vector<uint8_t>{0x30, 0x03});
            REQUIRE(msg.signed_installation_data.ecdh_curve == dt::EcdhCurve::X448);
            REQUIRE(msg.signed_installation_data.dh_public_key.size() == 57);
            REQUIRE_FALSE(msg.signed_installation_data.secp521_encrypted_private_key.has_value());
            REQUIRE(msg.signed_installation_data.x448_encrypted_private_key.has_value());
            REQUIRE(msg.signed_installation_data.x448_encrypted_private_key->size() == 84);
            REQUIRE_FALSE(msg.signed_installation_data.tpm_encrypted_private_key.has_value());
            REQUIRE(msg.remaining_contract_certificate_chains == 2);
        }
    }

    GIVEN("A SECC built placeholder certificate_installation_res") {

        // [V2G20-2202]: mandatory parameters filled minimally for an Ongoing or WARNING response.
        message_20::CertificateInstallationResponse res;
        res.header = message_20::Header{{0xF2, 0x19, 0x15, 0xB9, 0xDD, 0xDC, 0x12, 0xD1}, 1691411798};
        res.response_code = dt::ResponseCode::WARNING_NoCertificateAvailable;
        res.evse_processing = dt::Processing::Finished;
        res.signed_installation_data.id = "id";
        // SubCertificates is mandatory (1..3) inside ContractCertificateChain: one empty entry is the minimum.
        res.signed_installation_data.contract_certificate_chain.sub_certificates.emplace_back();

        const auto encoded = serialize_large(res);
        const io::StreamInputView stream_view{encoded.data(), encoded.size()};
        message_20::Variant variant(io::v2gtp::PayloadType::Part20Main, stream_view);

        THEN("It encodes and decodes with empty chains") {
            REQUIRE(variant.get_type() == message_20::Type::CertificateInstallationRes);
            const auto& msg = variant.get<message_20::CertificateInstallationResponse>();
            REQUIRE(msg.response_code == dt::ResponseCode::WARNING_NoCertificateAvailable);
            REQUIRE(msg.evse_processing == dt::Processing::Finished);
            REQUIRE(msg.cps_certificate_chain.certificate.empty());
            REQUIRE(msg.signed_installation_data.contract_certificate_chain.certificate.empty());
            REQUIRE(msg.signed_installation_data.contract_certificate_chain.sub_certificates.size() == 1);
            REQUIRE(msg.signed_installation_data.contract_certificate_chain.sub_certificates[0].empty());
            REQUIRE(msg.signed_installation_data.dh_public_key.empty());
            REQUIRE(msg.remaining_contract_certificate_chains == 0);
        }
    }
}
