// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Unit tests for module::charger::build_ssl_config — the helper that maps the
// multi-chain result of evse_security::get_all_valid_certificates_info into an
// iso15118::config::SSLConfig snapshot suitable for TbdController::set_ssl_config.
//
// These tests cover the data-path bullets of the cert rotation flow: multi-chain
// mapping (chain A vs chain B), empty-result rejection (preserves last-good in
// the controller), non-Accepted status rejection, and certificate_single
// fallback. The end-to-end TLS handshake assertions (peer-CA-driven chain
// selection, OCSP staple delivery) are covered at the libeverest-tls layer in
// lib/everest/tls/tests; here we only verify that build_ssl_config produces an
// SSLConfig with the expected chain shape — the certificate_store_update
// subscriber then pipes that through controller->set_ssl_config().

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include <generated/types/evse_security.hpp>

#include <iso15118/config.hpp>

#include "../charger/build_ssl_config.hpp"

namespace {

using types::evse_security::CertificateInfo;
using types::evse_security::GetCertificateFullInfoResult;
using types::evse_security::GetCertificateInfoStatus;

CertificateInfo make_cert_info(const std::string& chain_path, const std::string& key_path,
                               std::optional<std::string> password = std::nullopt) {
    CertificateInfo info{};
    info.key = key_path;
    info.certificate_count = 2;
    info.certificate = chain_path;
    info.password = std::move(password);
    return info;
}

} // namespace

TEST(BuildSslConfig, MultiChainMappingProducesOneChainPerCertificateInfo) {
    GetCertificateFullInfoResult certs{};
    certs.status = GetCertificateInfoStatus::Accepted;
    certs.info.push_back(make_cert_info("/tmp/iso/a/chain.pem", "/tmp/iso/a/key.pem"));
    certs.info.push_back(make_cert_info("/tmp/iso/b/chain.pem", "/tmp/iso/b/key.pem", std::string{"secret-b"}));

    const auto cfg = module::charger::build_ssl_config(certs, "/tmp/iso/v2g_root.pem", "/tmp/iso/mo_root.pem");

    ASSERT_TRUE(cfg.has_value());
    ASSERT_EQ(cfg->chains.size(), 2u);

    EXPECT_EQ(cfg->chains[0].path_certificate_chain, "/tmp/iso/a/chain.pem");
    EXPECT_EQ(cfg->chains[0].path_certificate_key, "/tmp/iso/a/key.pem");
    EXPECT_FALSE(cfg->chains[0].private_key_password.has_value());

    EXPECT_EQ(cfg->chains[1].path_certificate_chain, "/tmp/iso/b/chain.pem");
    EXPECT_EQ(cfg->chains[1].path_certificate_key, "/tmp/iso/b/key.pem");
    ASSERT_TRUE(cfg->chains[1].private_key_password.has_value());
    EXPECT_EQ(*cfg->chains[1].private_key_password, "secret-b");

    EXPECT_EQ(cfg->path_certificate_v2g_root, "/tmp/iso/v2g_root.pem");
    EXPECT_EQ(cfg->path_certificate_mo_root, "/tmp/iso/mo_root.pem");
}

TEST(BuildSslConfig, EmptyInfoReturnsNulloptSoLastGoodIsPreserved) {
    GetCertificateFullInfoResult certs{};
    certs.status = GetCertificateInfoStatus::Accepted;
    // info is intentionally empty

    const auto cfg = module::charger::build_ssl_config(certs, "/tmp/iso/v2g_root.pem", "/tmp/iso/mo_root.pem");

    EXPECT_FALSE(cfg.has_value());
}

TEST(BuildSslConfig, NonAcceptedStatusReturnsNullopt) {
    GetCertificateFullInfoResult certs{};
    certs.status = GetCertificateInfoStatus::NotFoundValid;
    certs.info.push_back(make_cert_info("/tmp/iso/a/chain.pem", "/tmp/iso/a/key.pem"));

    const auto cfg = module::charger::build_ssl_config(certs, "/tmp/iso/v2g_root.pem", "/tmp/iso/mo_root.pem");

    EXPECT_FALSE(cfg.has_value());
}

TEST(BuildSslConfig, CertificateSingleIsUsedWhenCertificateAbsent) {
    GetCertificateFullInfoResult certs{};
    certs.status = GetCertificateInfoStatus::Accepted;

    CertificateInfo info{};
    info.key = "/tmp/iso/single/key.pem";
    info.certificate_count = 1;
    info.certificate_single = "/tmp/iso/single/leaf.pem";
    certs.info.push_back(std::move(info));

    const auto cfg = module::charger::build_ssl_config(certs, "/tmp/iso/v2g_root.pem", "/tmp/iso/mo_root.pem");

    ASSERT_TRUE(cfg.has_value());
    ASSERT_EQ(cfg->chains.size(), 1u);
    EXPECT_EQ(cfg->chains[0].path_certificate_chain, "/tmp/iso/single/leaf.pem");
    EXPECT_EQ(cfg->chains[0].path_certificate_key, "/tmp/iso/single/key.pem");
}

TEST(BuildSslConfig, CertificateInfoWithoutAnyCertPathIsSkipped) {
    GetCertificateFullInfoResult certs{};
    certs.status = GetCertificateInfoStatus::Accepted;

    CertificateInfo bad{};
    bad.key = "/tmp/iso/bad/key.pem";
    bad.certificate_count = 0;
    // neither certificate nor certificate_single set
    certs.info.push_back(std::move(bad));

    certs.info.push_back(make_cert_info("/tmp/iso/good/chain.pem", "/tmp/iso/good/key.pem"));

    const auto cfg = module::charger::build_ssl_config(certs, "/tmp/iso/v2g_root.pem", "/tmp/iso/mo_root.pem");

    ASSERT_TRUE(cfg.has_value());
    ASSERT_EQ(cfg->chains.size(), 1u);
    EXPECT_EQ(cfg->chains[0].path_certificate_chain, "/tmp/iso/good/chain.pem");
}
