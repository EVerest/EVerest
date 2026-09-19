// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <iso15118/detail/d20/crypto.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/certificate_installation.hpp>
#include <iso15118/message/variant.hpp>

#include "../test_pki.hpp"

using namespace iso15118;

using namespace iso15118::test_pki;

SCENARIO("ISO 15118-20 AuthorizationReq signature verification") {

    const dt::GenChallenge challenge{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    GIVEN("A secp521r1 contract chain") {
        const auto pki = make_pki("secp521r1");
        const auto unsigned_req = unsigned_authorization_req(pki, challenge);
        const auto signed_req = crypto::sign_document(unsigned_req, crypto::SignedElement::PnC_AReqAuthorizationMode,
                                                      "id1", pki.leaf_private_key());
        REQUIRE_FALSE(signed_req.empty());

        THEN("The signature verifies with the leaf") {
            REQUIRE(crypto::verify_signature(signed_req, pki.leaf_der(),
                                             crypto::SignedElement::PnC_AReqAuthorizationMode) ==
                    crypto::SignatureVerdict::Ok);
        }

        THEN("The decoded header carries ecdsa-sha512 with a 132 byte r||s") {
            const io::StreamInputView view{signed_req.data(), signed_req.size()};
            message_20::Variant variant(io::v2gtp::PayloadType::Part20Main, view);
            const auto& msg = variant.get<message_20::AuthorizationRequest>();
            REQUIRE(msg.header.signature.has_value());
            REQUIRE(msg.header.signature->signed_info.signature_method ==
                    "http://www.w3.org/2001/04/xmldsig-more#ecdsa-sha512");
            REQUIRE(msg.header.signature->signed_info.references[0].digest_method ==
                    "http://www.w3.org/2001/04/xmlenc#sha512");
            REQUIRE(msg.header.signature->signed_info.references[0].uri == "#id1");
            REQUIRE(msg.header.signature->signature.value.size() == 132);
        }

        THEN("An unsigned request is reported as such") {
            REQUIRE(crypto::verify_signature(unsigned_req, pki.leaf_der(),
                                             crypto::SignedElement::PnC_AReqAuthorizationMode) ==
                    crypto::SignatureVerdict::NoSignature);
        }

        THEN("Another key does not verify") {
            const auto other = make_pki("secp521r1");
            REQUIRE(crypto::verify_signature(signed_req, other.leaf_der(),
                                             crypto::SignedElement::PnC_AReqAuthorizationMode) ==
                    crypto::SignatureVerdict::SignatureInvalid);
        }

        THEN("A tampered element digest is rejected") {
            const auto tampered = mutate_document(signed_req, [](iso20_exiDocument& doc) {
                doc.AuthorizationReq.Header.Signature.SignedInfo.Reference.array[0].DigestValue.bytes[0] ^= 0xff;
            });
            REQUIRE(
                crypto::verify_signature(tampered, pki.leaf_der(), crypto::SignedElement::PnC_AReqAuthorizationMode) ==
                crypto::SignatureVerdict::DigestMismatch);
        }

        THEN("A modified challenge no longer matches the digest") {
            const auto tampered = mutate_document(signed_req, [](iso20_exiDocument& doc) {
                doc.AuthorizationReq.PnC_AReqAuthorizationMode.GenChallenge.bytes[3] ^= 0x01;
            });
            REQUIRE(
                crypto::verify_signature(tampered, pki.leaf_der(), crypto::SignedElement::PnC_AReqAuthorizationMode) ==
                crypto::SignatureVerdict::DigestMismatch);
        }

        THEN("The -2 signature method is not accepted") {
            const auto tampered = mutate_document(signed_req, [](iso20_exiDocument& doc) {
                const char method[] = "http://www.w3.org/2001/04/xmldsig-more#ecdsa-sha256";
                auto& field = doc.AuthorizationReq.Header.Signature.SignedInfo.SignatureMethod.Algorithm;
                std::memcpy(field.characters, method, sizeof(method) - 1);
                field.charactersLen = sizeof(method) - 1;
            });
            REQUIRE(
                crypto::verify_signature(tampered, pki.leaf_der(), crypto::SignedElement::PnC_AReqAuthorizationMode) ==
                crypto::SignatureVerdict::UnsupportedAlgorithm);
        }

        THEN("A truncated r||s is rejected") {
            const auto tampered = mutate_document(signed_req, [](iso20_exiDocument& doc) {
                doc.AuthorizationReq.Header.Signature.SignatureValue.CONTENT.bytesLen = 64;
            });
            REQUIRE(
                crypto::verify_signature(tampered, pki.leaf_der(), crypto::SignedElement::PnC_AReqAuthorizationMode) ==
                crypto::SignatureVerdict::SignatureInvalid);
        }
    }

    GIVEN("An Ed448 contract chain") {
        const auto pki = make_pki("ED448");
        const auto unsigned_req = unsigned_authorization_req(pki, challenge);
        const auto signed_req = crypto::sign_document(unsigned_req, crypto::SignedElement::PnC_AReqAuthorizationMode,
                                                      "id1", pki.leaf_private_key());
        REQUIRE_FALSE(signed_req.empty());

        THEN("The Ed448 signature with a SHAKE256 digest verifies") {
            const io::StreamInputView view{signed_req.data(), signed_req.size()};
            message_20::Variant variant(io::v2gtp::PayloadType::Part20Main, view);
            const auto& msg = variant.get<message_20::AuthorizationRequest>();
            REQUIRE(msg.header.signature->signed_info.signature_method ==
                    "urn:iso:std:iso:15118:-20:Security:xmldsig#Ed448");
            REQUIRE(msg.header.signature->signed_info.references[0].digest_method ==
                    "urn:iso:std:iso:15118:-20:Security:xmlenc#SHAKE256");
            REQUIRE(msg.header.signature->signature.value.size() == 114);
            REQUIRE(crypto::verify_signature(signed_req, pki.leaf_der(),
                                             crypto::SignedElement::PnC_AReqAuthorizationMode) ==
                    crypto::SignatureVerdict::Ok);
        }

        THEN("Another Ed448 key does not verify") {
            const auto other = make_pki("ED448");
            REQUIRE(crypto::verify_signature(signed_req, other.leaf_der(),
                                             crypto::SignedElement::PnC_AReqAuthorizationMode) ==
                    crypto::SignatureVerdict::SignatureInvalid);
        }
    }
}

SCENARIO("ISO 15118-20 contract certificate chain validation") {

    GIVEN("A valid secp521r1 chain and its root") {
        const auto pki = make_pki("secp521r1");
        const auto result = crypto::validate_contract_chain(pki.leaf_der(), pki.subs_der(), pki.root_bundle_path, "");

        THEN("It validates and exposes the eMAID and chain") {
            REQUIRE(result.response_code == dt::ResponseCode::OK);
            REQUIRE(result.emaid == "DEABCC123ABC56X");
            REQUIRE_FALSE(result.forwardable);
            REQUIRE_FALSE(result.expires_within_14_days);
            REQUIRE(result.chain_pem.find("BEGIN CERTIFICATE") != std::string::npos);
        }
    }

    GIVEN("A valid chain whose root is only in the V2G bundle") {
        const auto pki = make_pki("secp521r1");
        const auto result = crypto::validate_contract_chain(pki.leaf_der(), pki.subs_der(), "", pki.root_bundle_path);
        THEN("It validates") {
            REQUIRE(result.response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("A valid Ed448 chain") {
        const auto pki = make_pki("ED448");
        const auto result = crypto::validate_contract_chain(pki.leaf_der(), pki.subs_der(), pki.root_bundle_path, "");
        THEN("It validates") {
            REQUIRE(result.response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("A leaf that expires in ten days") {
        CertSpec leaf{"DE-ABC-C123ABC56-X"};
        leaf.not_after_offset_s = 10L * 24 * 3600;
        const auto pki = make_pki("secp521r1", leaf);
        const auto result = crypto::validate_contract_chain(pki.leaf_der(), pki.subs_der(), pki.root_bundle_path, "");
        THEN("It validates and flags the upcoming expiry") {
            REQUIRE(result.response_code == dt::ResponseCode::OK);
            REQUIRE(result.expires_within_14_days);
        }
    }

    GIVEN("An expired leaf") {
        CertSpec leaf{"DE-ABC-C123ABC56-X"};
        leaf.not_before_offset_s = -2L * 365 * 24 * 3600;
        leaf.not_after_offset_s = -24L * 3600;
        const auto pki = make_pki("secp521r1", leaf);
        const auto result = crypto::validate_contract_chain(pki.leaf_der(), pki.subs_der(), pki.root_bundle_path, "");
        THEN("WARNING_CertificateExpired") {
            REQUIRE(result.response_code == dt::ResponseCode::WARNING_CertificateExpired);
            REQUIRE(result.emaid == "DEABCC123ABC56X");
        }
    }

    GIVEN("An expired sub-CA1") {
        CertSpec sub1{"eMSP Sub-CA1"};
        sub1.not_before_offset_s = -2L * 365 * 24 * 3600;
        sub1.not_after_offset_s = -24L * 3600;
        const auto pki = make_pki("secp521r1", {"DE-ABC-C123ABC56-X"}, sub1);
        const auto result = crypto::validate_contract_chain(pki.leaf_der(), pki.subs_der(), pki.root_bundle_path, "");
        THEN("WARNING_CertificateExpired") {
            REQUIRE(result.response_code == dt::ResponseCode::WARNING_CertificateExpired);
        }
    }

    GIVEN("A not yet valid sub-CA2") {
        CertSpec sub2{"eMSP Sub-CA2"};
        sub2.not_before_offset_s = 24L * 3600;
        sub2.not_after_offset_s = 365L * 24 * 3600;
        const auto pki = make_pki("secp521r1", {"DE-ABC-C123ABC56-X"}, {"eMSP Sub-CA1"}, sub2);
        const auto result = crypto::validate_contract_chain(pki.leaf_der(), pki.subs_der(), pki.root_bundle_path, "");
        THEN("WARNING_CertificateNotYetValid") {
            REQUIRE(result.response_code == dt::ResponseCode::WARNING_CertificateNotYetValid);
        }
    }

    GIVEN("A prime256v1 chain") {
        const auto pki = make_pki("prime256v1");
        const auto result = crypto::validate_contract_chain(pki.leaf_der(), pki.subs_der(), pki.root_bundle_path, "");
        THEN("The Annex B profile rejects it") {
            REQUIRE(result.response_code == dt::ResponseCode::WARNING_CertificateValidationError);
            REQUIRE_FALSE(result.forwardable);
        }
    }

    GIVEN("A leaf with an ExtendedKeyUsage extension") {
        CertSpec leaf{"DE-ABC-C123ABC56-X"};
        leaf.extra_ext = {"extendedKeyUsage=clientAuth"};
        const auto pki = make_pki("secp521r1", leaf);
        const auto result = crypto::validate_contract_chain(pki.leaf_der(), pki.subs_der(), pki.root_bundle_path, "");
        THEN("The Annex B profile rejects it") {
            REQUIRE(result.response_code == dt::ResponseCode::WARNING_CertificateValidationError);
        }
    }

    GIVEN("A leaf without revocation information") {
        CertSpec leaf{"DE-ABC-C123ABC56-X"};
        leaf.omit_aia = true;
        const auto pki = make_pki("secp521r1", leaf);
        const auto result = crypto::validate_contract_chain(pki.leaf_der(), pki.subs_der(), pki.root_bundle_path, "");
        THEN("The Annex B profile rejects it") {
            REQUIRE(result.response_code == dt::ResponseCode::WARNING_CertificateValidationError);
        }
    }

    GIVEN("A chain whose root is not installed") {
        const auto pki = make_pki("secp521r1");
        const auto other = make_pki("secp521r1");
        const auto result = crypto::validate_contract_chain(pki.leaf_der(), pki.subs_der(), other.root_bundle_path, "");
        THEN("The eMSP is unknown and the chain may go to the backend") {
            REQUIRE(result.response_code == dt::ResponseCode::WARNING_eMSPUnknown);
            REQUIRE(result.forwardable);
            REQUIRE(result.emaid == "DEABCC123ABC56X");
        }
    }

    GIVEN("No root bundle at all") {
        const auto pki = make_pki("secp521r1");
        const auto result = crypto::validate_contract_chain(pki.leaf_der(), pki.subs_der(), "", "");
        THEN("The eMSP is unknown and the chain may go to the backend") {
            REQUIRE(result.response_code == dt::ResponseCode::WARNING_eMSPUnknown);
            REQUIRE(result.forwardable);
        }
    }
}

SCENARIO("ISO 15118-20 chain validity") {

    GIVEN("A valid chain") {
        const auto pki = make_pki("secp521r1", {"DE8PAA00003C4D58Y2"});
        THEN("No fault") {
            REQUIRE_FALSE(crypto::chain_validity_fault(pki.leaf_der(), pki.subs_der()).has_value());
        }
    }

    GIVEN("A not yet valid leaf and an expired sub-CA2") {
        CertSpec leaf{"DE8PAA00003C4D58Y2"};
        leaf.not_before_offset_s = 24L * 3600;
        leaf.not_after_offset_s = 365L * 24 * 3600;
        CertSpec sub2{"OEM Sub-CA2"};
        sub2.not_before_offset_s = -2L * 365 * 24 * 3600;
        sub2.not_after_offset_s = -24L * 3600;
        const auto pki = make_pki("secp521r1", leaf, {"OEM Sub-CA1"}, sub2);
        THEN("Expired wins") {
            REQUIRE(crypto::chain_validity_fault(pki.leaf_der(), pki.subs_der()) ==
                    dt::ResponseCode::WARNING_CertificateExpired);
        }
    }

    GIVEN("A not yet valid sub-CA1") {
        CertSpec sub1{"OEM Sub-CA1"};
        sub1.not_before_offset_s = 24L * 3600;
        sub1.not_after_offset_s = 365L * 24 * 3600;
        const auto pki = make_pki("secp521r1", {"DE8PAA00003C4D58Y2"}, sub1);
        THEN("WARNING_CertificateNotYetValid") {
            REQUIRE(crypto::chain_validity_fault(pki.leaf_der(), pki.subs_der()) ==
                    dt::ResponseCode::WARNING_CertificateNotYetValid);
        }
    }

    GIVEN("A leaf that is not a certificate") {
        THEN("WARNING_CertificateValidationError") {
            REQUIRE(crypto::chain_validity_fault({0x30, 0x00}, {}) ==
                    dt::ResponseCode::WARNING_CertificateValidationError);
        }
    }
}

SCENARIO("ISO 15118-20 CertificateInstallationReq signature") {

    GIVEN("A signed CertificateInstallationReq") {
        const auto pki = make_pki("secp521r1", {"DE8PAA00003C4D58Y2"});

        message_20::CertificateInstallationRequest req;
        req.header.session_id = {1, 2, 3, 4, 5, 6, 7, 8};
        req.header.timestamp = 1691411798;
        req.oem_provisioning_certificate_chain.id = "oem1";
        req.oem_provisioning_certificate_chain.certificate = pki.leaf_der();
        for (const auto& sub : pki.subs_der()) {
            req.oem_provisioning_certificate_chain.sub_certificates.emplace_back(sub);
        }
        req.list_of_root_certificate_ids.root_certificate_id.emplace_back(dt::X509IssuerSerial{"CN=V2G Root", 7});
        req.maximum_contract_certificate_chains = 1;
        std::vector<uint8_t> buffer(16384);
        io::StreamOutputView out({buffer.data(), buffer.size()});
        buffer.resize(message_20::serialize(req, out));

        const auto signed_req = crypto::sign_document(buffer, crypto::SignedElement::CertificateInstallationReq, "oem1",
                                                      pki.leaf_private_key());
        REQUIRE_FALSE(signed_req.empty());

        THEN("The OEM provisioning leaf verifies it") {
            REQUIRE(crypto::verify_signature(signed_req, pki.leaf_der(),
                                             crypto::SignedElement::CertificateInstallationReq) ==
                    crypto::SignatureVerdict::Ok);
        }

        THEN("The contract leaf of another chain does not") {
            const auto other = make_pki("secp521r1");
            REQUIRE(crypto::verify_signature(signed_req, other.leaf_der(),
                                             crypto::SignedElement::CertificateInstallationReq) ==
                    crypto::SignatureVerdict::SignatureInvalid);
        }
    }
}
