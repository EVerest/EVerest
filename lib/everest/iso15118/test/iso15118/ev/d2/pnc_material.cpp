// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include <openssl/x509.h>

#include <everest/tls/openssl_util.hpp>
#include <iso15118/ev/d2/pnc_material.hpp>
#include <iso15118/message_2/payment_details.hpp>
#include <iso15118/message_2/variant.hpp>

#include "../../captured_messages.hpp"
#include "../../test_pki.hpp"

using namespace iso15118;

SCENARIO("EV public PnC material conversions preserve the chain and contract eMAID") {
    const auto& exi = captured::ISO2_PAYMENT_DETAILS_REQ;
    const message_2::Variant message(io::StreamInputView{exi.data(), exi.size()});
    const auto& details = message.get<message_2::PaymentDetailsRequest>();
    std::vector<std::vector<uint8_t>> chain{details.contract_certificate};
    chain.insert(chain.end(), details.sub_certificates.begin(), details.sub_certificates.end());
    std::string pem;
    for (const auto& der : chain) {
        const unsigned char* data = der.data();
        std::unique_ptr<X509, decltype(&X509_free)> cert{d2i_X509(nullptr, &data, der.size()), X509_free};
        REQUIRE(cert != nullptr);
        pem += openssl::certificate_to_pem(cert.get());
    }
    REQUIRE(ev::d2::pnc_material::pem_chain_to_der(pem) == chain);
    REQUIRE(ev::d2::pnc_material::emaid_from_contract_der(chain.front()) == "DEHUBC02TSTVL16");
    REQUIRE(ev::d2::pnc_material::pem_chain_to_der("not PEM").empty());
    REQUIRE(ev::d2::pnc_material::pem_chain_to_der("").empty());
    REQUIRE(ev::d2::pnc_material::emaid_from_contract_der({}).empty());
    REQUIRE(ev::d2::pnc_material::emaid_from_contract_der({0x30, 0x01}).empty());
}

SCENARIO("EV public PnC material tells whether a contract key belongs to its leaf") {
    const auto pki = test_pki::make_pki("prime256v1");
    const auto other = test_pki::make_pki("prime256v1");
    const auto leaf_key = test_pki::key_to_pem(pki.leaf_key.get());
    const auto leaf = ev::d2::pnc_material::pem_chain_to_der(test_pki::to_pem(pki.leaf.get()));
    REQUIRE(leaf.size() == 1);
    REQUIRE(ev::d2::pnc_material::contract_key_matches(leaf.front(), leaf_key));
    REQUIRE_FALSE(ev::d2::pnc_material::contract_key_matches(leaf.front(), test_pki::key_to_pem(other.leaf_key.get())));
    const auto leaf_sec1_key = test_pki::key_to_sec1_pem(pki.leaf_key.get());
    REQUIRE(leaf_sec1_key.find("BEGIN EC PRIVATE KEY") != std::string::npos);
    REQUIRE(ev::d2::pnc_material::contract_key_matches(leaf.front(), leaf_sec1_key));
    REQUIRE_FALSE(
        ev::d2::pnc_material::contract_key_matches(leaf.front(), test_pki::key_to_sec1_pem(other.leaf_key.get())));
    REQUIRE_FALSE(ev::d2::pnc_material::contract_key_matches(leaf.front(), "not PEM"));
    REQUIRE_FALSE(ev::d2::pnc_material::contract_key_matches({}, leaf_key));
}
