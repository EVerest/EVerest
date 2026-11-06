// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include <iso15118/detail/d2/crypto.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/payment_details.hpp>
#include <iso15118/message_2/variant.hpp>

#include "../captured_messages.hpp"
#include "../test_pki.hpp"

using namespace iso15118;

namespace {

std::string normalized_emaid(std::string in) {
    std::string out;
    for (const char c : in) {
        if (c != '-') {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

// An ISO 15118-2 chain (Annex F): key identifiers and AuthorityInfoAccess non-critical, unlike the -20 test PKI.
test_pki::Pki iso2_pki(test_pki::CertSpec leaf, test_pki::CertSpec sub1, test_pki::CertSpec sub2) {
    leaf.critical_ids = false;
    return test_pki::make_pki("prime256v1", leaf, sub1, sub2);
}

test_pki::Pki iso2_mo_pki(test_pki::CertSpec leaf) {
    return iso2_pki(std::move(leaf), {"MO Sub-CA1"}, {"MO Sub-CA2"});
}

} // namespace

SCENARIO("ISO 15118-2 PnC AuthorizationReq captured from an EV") {

    GIVEN("The captured PaymentDetails exchange and the signed AuthorizationReq that follows it") {
        const auto& details_exi = captured::ISO2_PAYMENT_DETAILS_REQ;
        const message_2::Variant details_variant(io::StreamInputView{details_exi.data(), details_exi.size()});
        REQUIRE(details_variant.get_type() == message_2::Type::PaymentDetailsReq);
        const auto& details = details_variant.get<message_2::PaymentDetailsRequest>();
        const auto& leaf = details.contract_certificate;
        REQUIRE_FALSE(leaf.empty());

        const auto& auth_exi = captured::ISO2_PNC_AUTHORIZATION_REQ;
        const message_2::Variant auth_variant(io::StreamInputView{auth_exi.data(), auth_exi.size()});
        REQUIRE(auth_variant.get_type() == message_2::Type::AuthorizationReq);
        const auto& auth = auth_variant.get<message_2::AuthorizationRequest>();

        THEN("The header signature verifies with the contract leaf from PaymentDetailsReq") {
            REQUIRE(d2::crypto::verify_authorization_signature(auth_exi, leaf));
        }

        THEN("The request carries the GenChallenge and the Id the signature references") {
            REQUIRE(auth.gen_challenge.has_value());
            REQUIRE(auth.id.has_value());
        }

        THEN("The contract leaf names the eMAID the EV claimed") {
            REQUIRE(details.sub_certificates.size() == 2);
            REQUIRE(d2::crypto::emaid_from_contract_der(leaf) == "DEHUBC02TSTVL16");
            REQUIRE(normalized_emaid(details.emaid) == "DEHUBC02TSTVL16");
        }

        THEN("The key of another chain does not verify it") {
            const auto other = test_pki::make_pki("prime256v1");
            REQUIRE_FALSE(d2::crypto::verify_authorization_signature(auth_exi, other.leaf_der()));
        }

        THEN("A flipped bit in the request fails") {
            auto tampered = auth_exi;
            tampered[tampered.size() / 2] ^= 0x01;
            REQUIRE_FALSE(d2::crypto::verify_authorization_signature(tampered, leaf));
        }
    }
}

SCENARIO("ISO 15118-2 PaymentDetails accepts only a contract certificate") {
    namespace m2dt = message_2::datatypes;
    const std::string emaid{"DE-ABC-C123ABC56-X"};
    // The test root is passed as the V2G root: deployed PKIs issue the MO sub-CAs below it.
    const auto validate = [](const test_pki::Pki& pki, const std::vector<std::vector<uint8_t>>& subs,
                             const std::string& req_emaid) {
        return d2::crypto::validate_contract_chain(pki.leaf_der(), subs, req_emaid, "", pki.root_bundle_path);
    };

    GIVEN("A contract chain below the V2G root") {
        const auto pki = iso2_mo_pki(test_pki::CertSpec{emaid});
        THEN("It is accepted and the eMAID is taken from the certificate") {
            const auto result = validate(pki, pki.subs_der(), emaid);
            REQUIRE(result.response_code == m2dt::ResponseCode::OK);
            REQUIRE(result.emaid == "DEABCC123ABC56X");
        }
        THEN("A third SubCertificate is rejected [V2G2-656]") {
            auto subs = pki.subs_der();
            subs.push_back(test_pki::to_der(pki.root.get()));
            REQUIRE(validate(pki, subs, emaid).response_code == m2dt::ResponseCode::FAILED);
        }
    }

    GIVEN("A contract certificate with the optional DomainComponent MO") {
        test_pki::CertSpec leaf{emaid};
        leaf.dc = "MO";
        const auto pki = iso2_mo_pki(leaf);
        THEN("It is accepted") {
            REQUIRE(validate(pki, pki.subs_der(), emaid).response_code == m2dt::ResponseCode::OK);
        }
    }

    GIVEN("An SECC certificate below the same V2G root (Table F.2: DomainComponent CPO, CommonName CPID)") {
        test_pki::CertSpec leaf{"DE*PNX*E12345*1"};
        leaf.dc = "CPO";
        const auto pki = iso2_pki(leaf, {"CPO Sub-CA1"}, {"CPO Sub-CA2"});
        THEN("It is rejected although its chain verifies and the EV echoes its CommonName as eMAID") {
            REQUIRE(validate(pki, pki.subs_der(), "DE*PNX*E12345*1").response_code == m2dt::ResponseCode::FAILED);
        }
    }

    GIVEN("A leaf with an EMAID CommonName but the DomainComponent of another profile") {
        test_pki::CertSpec leaf{emaid};
        leaf.dc = "CPS";
        const auto pki = iso2_mo_pki(leaf);
        THEN("It is rejected") {
            REQUIRE(validate(pki, pki.subs_der(), emaid).response_code == m2dt::ResponseCode::FAILED);
        }
    }

    GIVEN("A leaf whose CommonName does not follow the Annex H EMAID syntax") {
        for (const auto* cn : {"DEABCC123ABC56XY", "D1ABCC123ABC56X", "DEABCC123ABC5"}) {
            const auto pki = iso2_mo_pki(test_pki::CertSpec{cn});
            THEN(std::string("'") + cn + "' is rejected even when the request echoes it") {
                REQUIRE(validate(pki, pki.subs_der(), cn).response_code == m2dt::ResponseCode::FAILED);
            }
        }
    }

    GIVEN("An expired contract certificate") {
        test_pki::CertSpec leaf{emaid};
        leaf.not_after_offset_s = -60;
        const auto pki = iso2_mo_pki(leaf);
        THEN("It is answered with FAILED_CertificateExpired [V2G2-474]") {
            REQUIRE(validate(pki, pki.subs_der(), emaid).response_code ==
                    m2dt::ResponseCode::FAILED_CertificateExpired);
        }
    }
}
