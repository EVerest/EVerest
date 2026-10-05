// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <string>
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
