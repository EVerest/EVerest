// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <everest/opcp/pkcs.hpp>

#include "test_support.hpp"

using namespace opcp;
using namespace opcp::test;

class Pkcs : public ::testing::Test {
protected:
    void SetUp() override {
        ensure_test_pki();
    }
};

TEST_F(Pkcs, csr_pem_to_base64_der) {
    const std::string csr = pki("leaf2.csr");
    const std::string b64 = pem_csr_to_base64_der(csr);
    ASSERT_FALSE(b64.empty());
    EXPECT_EQ(b64.find('\n'), std::string::npos);
    EXPECT_EQ(b64.find("-----"), std::string::npos);
    EXPECT_EQ(b64.rfind("MII", 0), 0u) << "DER of a SEQUENCE starts with 0x30 0x82 -> 'MII'";

    EXPECT_TRUE(pem_csr_to_base64_der(pki("leaf2.pem")).empty()) << "a certificate is not a CSR";
    EXPECT_TRUE(pem_csr_to_base64_der("garbage").empty());
}

TEST_F(Pkcs, pkcs7_single_leaf) {
    const auto certs = pkcs7_base64_to_pem_certificates(pki("leaf2.p7b64"));
    ASSERT_EQ(certs.size(), 1u);
    const auto summary = describe_certificate(certs[0]);
    ASSERT_TRUE(summary.has_value());
    EXPECT_EQ(summary->common_name, "DE*PNX*E12345*1");
    EXPECT_EQ(summary->public_key_algorithm, "prime256v1");
    EXPECT_FALSE(summary->self_signed);
    EXPECT_FALSE(summary->is_ca);
    EXPECT_NE(summary->issuer.find("CPO Sub2 CA Test"), std::string::npos);
    EXPECT_EQ(summary->not_after.size(), 20u) << summary->not_after;
}

TEST_F(Pkcs, pkcs7_chain_and_line_wrapped_input) {
    std::string wrapped;
    const std::string raw = pki("cacerts_with_root.p7b64");
    for (std::size_t i = 0; i < raw.size(); i += 64) {
        wrapped += raw.substr(i, 64) + "\r\n";
    }
    const auto certs = pkcs7_base64_to_pem_certificates(wrapped);
    ASSERT_EQ(certs.size(), 3u);
    EXPECT_TRUE(describe_certificate(certs[2])->self_signed);
    EXPECT_TRUE(describe_certificate(certs[0])->is_ca);
}

TEST_F(Pkcs, pkcs7_accepts_pem_certificates_and_rejects_garbage) {
    const auto certs = pkcs7_base64_to_pem_certificates(pki("sub2.pem") + pki("sub1.pem"));
    ASSERT_EQ(certs.size(), 2u);

    EXPECT_TRUE(pkcs7_base64_to_pem_certificates("bm90IGEgcGtjczcgYmxvYg==").empty());
    EXPECT_TRUE(pkcs7_base64_to_pem_certificates("").empty());
    EXPECT_TRUE(pkcs7_base64_to_pem_certificates("{\"error\":\"nope\"}").empty());
}

TEST_F(Pkcs, der_base64_root_to_pem) {
    const std::string pem = der_base64_to_pem(pki("root.derb64"));
    ASSERT_FALSE(pem.empty());
    const auto summary = describe_certificate(pem);
    ASSERT_TRUE(summary.has_value());
    EXPECT_EQ(summary->common_name, "V2G Root CA Test");
    EXPECT_TRUE(summary->self_signed);
    EXPECT_TRUE(der_base64_to_pem("AAAA").empty());
}

TEST_F(Pkcs, build_chain_leaf_plus_two_intermediates) {
    const auto leaf = pkcs7_base64_to_pem_certificates(pki("leaf2.p7b64"));
    const auto cas = pkcs7_base64_to_pem_certificates(pki("cacerts.p7b64"));
    std::string error;
    std::optional<std::string> top;
    const auto chain = build_leaf_chain(pki("leaf2.csr"), leaf, cas, error, &top);
    ASSERT_TRUE(chain.has_value()) << error;
    EXPECT_EQ(count_pem_certificates(*chain), 3u);
    EXPECT_FALSE(top.has_value()) << "no root in the inputs";

    // Order: leaf, sub2, sub1
    const auto first = chain->find("-----END CERTIFICATE-----");
    const auto leaf_pem = chain->substr(0, first + 25);
    EXPECT_EQ(describe_certificate(leaf_pem)->common_name, "DE*PNX*E12345*1");
    const auto second_start = chain->find("-----BEGIN CERTIFICATE-----", first);
    const auto second_end = chain->find("-----END CERTIFICATE-----", second_start);
    EXPECT_EQ(describe_certificate(chain->substr(second_start, second_end + 25 - second_start))->common_name,
              "CPO Sub2 CA Test");
}

TEST_F(Pkcs, build_chain_strips_root_and_reports_it) {
    const auto leaf = pkcs7_base64_to_pem_certificates(pki("leaf20.p7b64"));
    const auto cas = pkcs7_base64_to_pem_certificates(pki("cacerts_with_root.p7b64"));
    std::string error;
    std::optional<std::string> top;
    const auto chain = build_leaf_chain(pki("leaf20.csr"), leaf, cas, error, &top);
    ASSERT_TRUE(chain.has_value()) << error;
    EXPECT_EQ(count_pem_certificates(*chain), 3u) << "root must not be part of the stored chain";
    ASSERT_TRUE(top.has_value());
    EXPECT_EQ(describe_certificate(*top)->common_name, "V2G Root CA Test");
    EXPECT_EQ(describe_certificate(*chain)->public_key_algorithm, "secp521r1");
}

TEST_F(Pkcs, build_chain_handles_full_response_without_cacerts) {
    const auto full = pkcs7_base64_to_pem_certificates(pki("leaf2_full.p7b64"));
    std::string error;
    const auto chain = build_leaf_chain(pki("leaf2.csr"), full, {}, error);
    ASSERT_TRUE(chain.has_value()) << error;
    EXPECT_EQ(count_pem_certificates(*chain), 3u);
}

TEST_F(Pkcs, build_chain_failures) {
    const auto leaf = pkcs7_base64_to_pem_certificates(pki("leaf2.p7b64"));
    const auto cas = pkcs7_base64_to_pem_certificates(pki("cacerts.p7b64"));
    std::string error;

    // wrong key: CSR of the -20 leaf, response holds the -2 leaf
    EXPECT_FALSE(build_leaf_chain(pki("leaf20.csr"), leaf, cas, error).has_value());
    EXPECT_NE(error.find("public key"), std::string::npos) << error;

    // missing intermediates
    EXPECT_FALSE(build_leaf_chain(pki("leaf2.csr"), leaf, {}, error).has_value());
    EXPECT_NE(error.find("sub-CA"), std::string::npos) << error;

    // only sub1 present (gap in the chain)
    const auto only_sub1 = pkcs7_base64_to_pem_certificates(pki("sub1.pem"));
    EXPECT_FALSE(build_leaf_chain(pki("leaf2.csr"), leaf, only_sub1, error).has_value());

    // broken CSR
    EXPECT_FALSE(build_leaf_chain("nope", leaf, cas, error).has_value());
    EXPECT_FALSE(build_leaf_chain(pki("leaf2.csr"), {}, {}, error).has_value());
}
