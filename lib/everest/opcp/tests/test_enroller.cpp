// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <everest/opcp/enroller.hpp>

#include "test_support.hpp"

using namespace opcp;
using namespace opcp::test;

class EnrollerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ensure_test_pki();
        env = Environment::preset("hubject-eu-qa").value();
        est = std::make_unique<EstClient>(http, env);
        rcp = std::make_unique<RcpClient>(http, env);
        enroller = std::make_unique<Enroller>(store, *est, *rcp);
        store.csr_to_return = pki("leaf2.csr");
        subject = CsrSubject{"DE*PNX*E12345*1", "EVerest Test", "DE", false};
    }

    void script_happy_path() {
        http.on("POST", env.simpleenroll_url(IsoVersion::ISO15118_2), 200, pki("leaf2.p7b64"));
        http.on("POST", env.simplereenroll_url(IsoVersion::ISO15118_2), 200, pki("leaf2.p7b64"));
        http.on("GET", env.cacerts_url(IsoVersion::ISO15118_2), 200, pki("cacerts.p7b64"));
    }

    FakeHttpClient http;
    FakeSecurityStore store;
    Environment env;
    std::unique_ptr<EstClient> est;
    std::unique_ptr<RcpClient> rcp;
    std::unique_ptr<Enroller> enroller;
    CsrSubject subject;
};

TEST_F(EnrollerTest, enroll_installs_full_chain) {
    script_happy_path();
    const auto result = enroller->enroll_leaf(IsoVersion::ISO15118_2, subject, BearerToken{"tok"}, false);
    ASSERT_TRUE(result.ok) << enroll_step_name(result.failed_step) << ": " << result.error;
    EXPECT_EQ(result.chain_length, 3u);
    ASSERT_TRUE(result.leaf.has_value());
    EXPECT_EQ(result.leaf->common_name, "DE*PNX*E12345*1");
    ASSERT_EQ(store.installed_leaf_chains.size(), 1u);
    EXPECT_EQ(count_pem_certificates(store.installed_leaf_chains[0]), 3u);
    EXPECT_TRUE(store.failed_csrs.empty());
    ASSERT_EQ(store.generated_common_names.size(), 1u);
    EXPECT_EQ(store.generated_common_names[0], "DE*PNX*E12345*1");

    // enroll then cacerts, both with the token
    ASSERT_EQ(http.requests.size(), 2u);
    EXPECT_NE(http.requests[0].url.find("simpleenroll"), std::string::npos);
    EXPECT_NE(http.requests[1].url.find("cacerts"), std::string::npos);
}

TEST_F(EnrollerTest, reenroll_uses_simplereenroll_with_client_certificate) {
    script_happy_path();
    const ClientCertificate cert{"/c.pem", "/k.key", std::string("pw")};
    const auto result = enroller->enroll_leaf(IsoVersion::ISO15118_2, subject, cert, true);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(http.requests.size(), 2u);
    EXPECT_NE(http.requests[0].url.find("simplereenroll"), std::string::npos);
    EXPECT_TRUE(std::holds_alternative<ClientCertificate>(http.requests[0].auth));
    EXPECT_TRUE(std::holds_alternative<ClientCertificate>(http.requests[1].auth)) << "cacerts also via mTLS";
}

TEST_F(EnrollerTest, refuses_without_v2g_root) {
    script_happy_path();
    store.v2g_root_installed = false;
    const auto result = enroller->enroll_leaf(IsoVersion::ISO15118_2, subject, BearerToken{"tok"}, false);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.failed_step, EnrollStep::CheckRoots);
    EXPECT_TRUE(http.requests.empty()) << "no network before the precondition holds";
    EXPECT_TRUE(store.generated_common_names.empty()) << "no key generated";
}

TEST_F(EnrollerTest, csr_generation_failure) {
    store.csr_status = evse_security::GetCertificateSignRequestStatus::KeyGenError;
    const auto result = enroller->enroll_leaf(IsoVersion::ISO15118_2, subject, BearerToken{"tok"}, false);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.failed_step, EnrollStep::GenerateCsr);
    EXPECT_NE(result.error.find("KeyGenError"), std::string::npos);
}

TEST_F(EnrollerTest, auth_rejected_cleans_up_key) {
    http.on("POST", env.simpleenroll_url(IsoVersion::ISO15118_2), 403, "Forbidden");
    const auto result = enroller->enroll_leaf(IsoVersion::ISO15118_2, subject, BearerToken{"tok"}, false);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.auth_rejected);
    EXPECT_EQ(result.failed_step, EnrollStep::Enroll);
    EXPECT_EQ(result.http_status, 403);
    ASSERT_EQ(store.failed_csrs.size(), 1u) << "orphan key must be dropped";
    EXPECT_TRUE(store.installed_leaf_chains.empty());
}

TEST_F(EnrollerTest, cacerts_failure_and_chain_failure) {
    http.on("POST", env.simpleenroll_url(IsoVersion::ISO15118_2), 200, pki("leaf2.p7b64"));
    http.on("GET", env.cacerts_url(IsoVersion::ISO15118_2), 500, "boom");
    auto result = enroller->enroll_leaf(IsoVersion::ISO15118_2, subject, BearerToken{"tok"}, false);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.failed_step, EnrollStep::FetchCaCertificates);

    http.scripts.clear();
    http.on("POST", env.simpleenroll_url(IsoVersion::ISO15118_2), 200, pki("leaf2.p7b64"));
    http.on("GET", env.cacerts_url(IsoVersion::ISO15118_2), 200, pki("sub1.pem")); // gap: sub2 missing
    result = enroller->enroll_leaf(IsoVersion::ISO15118_2, subject, BearerToken{"tok"}, false);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.failed_step, EnrollStep::BuildChain);
    EXPECT_EQ(store.failed_csrs.size(), 2u);
}

TEST_F(EnrollerTest, store_rejects_chain) {
    script_happy_path();
    store.leaf_install_result = evse_security::InstallCertificateResult::InvalidCertificateChain;
    const auto result = enroller->enroll_leaf(IsoVersion::ISO15118_2, subject, BearerToken{"tok"}, false);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.failed_step, EnrollStep::InstallLeaf);
    EXPECT_NE(result.error.find("V2G root"), std::string::npos) << result.error;
    EXPECT_EQ(store.failed_csrs.size(), 1u);
}

TEST_F(EnrollerTest, iso20_uses_v2g20_store_type) {
    store.csr_to_return = pki("leaf20.csr");
    http.on("POST", env.simpleenroll_url(IsoVersion::ISO15118_20), 200, pki("leaf20.p7b64"));
    http.on("GET", env.cacerts_url(IsoVersion::ISO15118_20), 200, pki("cacerts.p7b64"));
    const auto result = enroller->enroll_leaf(IsoVersion::ISO15118_20, subject, BearerToken{"tok"}, false);
    ASSERT_TRUE(result.ok) << enroll_step_name(result.failed_step) << ": " << result.error;
    EXPECT_EQ(result.leaf->public_key_algorithm, "secp521r1");
    EXPECT_NE(http.requests[1].url.find("secp521r1"), std::string::npos);
}

TEST_F(EnrollerTest, sync_roots_installs_selected_types_only) {
    const std::string body = R"({"RootCertificateCollection":{"rootCertificates":[
        {"caCertificate":")" +
                             pki("root.derb64") + R"(","rootType":"V2G","commonName":"V2G Root CA Test"},
        {"caCertificate":")" +
                             pki("root.derb64") + R"(","rootType":"MO","commonName":"MO Root"},
        {"caCertificate":")" +
                             pki("root.derb64") + R"(","rootType":"OEM","commonName":"OEM Root"}]}})";
    http.on("GET", env.root_certs_url(), 200, body);

    auto result = enroller->sync_roots(BearerToken{"tok"}, {RootType::V2G, RootType::MO});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.installed, 2);
    EXPECT_EQ(result.skipped, 0);
    ASSERT_EQ(store.installed_cas.size(), 2u);
    EXPECT_EQ(store.installed_cas[0].first, evse_security::CaCertificateType::V2G);
    EXPECT_EQ(store.installed_cas[1].first, evse_security::CaCertificateType::MO);

    store.installed_cas.clear();
    result = enroller->sync_roots(BearerToken{"tok"}, {RootType::OEM});
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.installed, 0);
    EXPECT_EQ(result.skipped, 1);
    EXPECT_TRUE(store.installed_cas.empty());

    store.ca_install_result = evse_security::InstallCertificateResult::WriteError;
    result = enroller->sync_roots(BearerToken{"tok"}, {RootType::V2G});
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.failed, 1);
}

TEST_F(EnrollerTest, sync_roots_reports_auth_rejection) {
    http.on("GET", env.root_certs_url(), 403, "");
    const auto result = enroller->sync_roots(ClientCertificate{"c", "k", std::nullopt}, {RootType::V2G});
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.auth_rejected);
    EXPECT_EQ(result.http_status, 403);
}
