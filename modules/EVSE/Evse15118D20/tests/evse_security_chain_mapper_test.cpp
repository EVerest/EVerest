// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Unit tests for the pure helpers in evse_security_chain_mapper (module::charger):
//
//  - map_valid_chains: maps the multi-chain result of
//    evse_security::get_all_valid_certificates_info into a
//    std::vector<iso15118::config::ChainConfig> (one ChainConfig per accepted
//    CertificateInfo), including propagation of certificate_root into the
//    trust_anchor_pem field.
//  - is_relevant_certificate_store_update: the cheap relevance predicate that
//    gates whether a certificate_store_update event affects the V2G SSL config.
//  - decide_certificate_store_update: the apply/preserve decision applied to a
//    freshly rebuilt SSL config.
//
// Trust-anchor roots and module-level TLS flags are assembled elsewhere; these
// helpers are concerned only with the per-chain mapping and the store-update
// gating/decision. The end-to-end TLS handshake assertions (peer-CA-driven chain
// selection, OCSP staple delivery) are covered at the libeverest-tls layer in
// lib/everest/tls/tests; here we only verify the helper shapes.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include <generated/types/evse_security.hpp>

#include <iso15118/config.hpp>

#include "../charger/evse_security_chain_mapper.hpp"

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

TEST(MapValidChains, MultiChainMappingProducesOneChainPerCertificateInfo) {
    GetCertificateFullInfoResult certs{};
    certs.status = GetCertificateInfoStatus::Accepted;
    certs.info.push_back(make_cert_info("/tmp/iso/a/chain.pem", "/tmp/iso/a/key.pem"));
    certs.info.push_back(make_cert_info("/tmp/iso/b/chain.pem", "/tmp/iso/b/key.pem", std::string{"secret-b"}));

    auto chains = module::charger::map_valid_chains(certs);

    ASSERT_EQ(chains.size(), 2u);

    EXPECT_EQ(chains[0].path_certificate_chain, "/tmp/iso/a/chain.pem");
    EXPECT_EQ(chains[0].path_certificate_key, "/tmp/iso/a/key.pem");
    EXPECT_FALSE(chains[0].private_key_password.has_value());

    EXPECT_EQ(chains[1].path_certificate_chain, "/tmp/iso/b/chain.pem");
    EXPECT_EQ(chains[1].path_certificate_key, "/tmp/iso/b/key.pem");
    ASSERT_TRUE(chains[1].private_key_password.has_value());
    EXPECT_EQ(*chains[1].private_key_password, "secret-b");
}

TEST(MapValidChains, EmptyInfoReturnsEmptyVector) {
    GetCertificateFullInfoResult certs{};
    certs.status = GetCertificateInfoStatus::Accepted;
    // info is intentionally empty

    EXPECT_TRUE(module::charger::map_valid_chains(certs).empty());
}

TEST(MapValidChains, NonAcceptedStatusReturnsEmptyVector) {
    GetCertificateFullInfoResult certs{};
    certs.status = GetCertificateInfoStatus::NotFoundValid;
    certs.info.push_back(make_cert_info("/tmp/iso/a/chain.pem", "/tmp/iso/a/key.pem"));

    EXPECT_TRUE(module::charger::map_valid_chains(certs).empty());
}

TEST(MapValidChains, CertificateSingleIsUsedWhenCertificateAbsent) {
    GetCertificateFullInfoResult certs{};
    certs.status = GetCertificateInfoStatus::Accepted;

    CertificateInfo info{};
    info.key = "/tmp/iso/single/key.pem";
    info.certificate_count = 1;
    info.certificate_single = "/tmp/iso/single/leaf.pem";
    certs.info.push_back(std::move(info));

    auto chains = module::charger::map_valid_chains(certs);

    ASSERT_EQ(chains.size(), 1u);
    EXPECT_EQ(chains[0].path_certificate_chain, "/tmp/iso/single/leaf.pem");
    EXPECT_EQ(chains[0].path_certificate_key, "/tmp/iso/single/key.pem");
}

TEST(MapValidChains, CertificateInfoWithoutAnyCertPathIsSkipped) {
    GetCertificateFullInfoResult certs{};
    certs.status = GetCertificateInfoStatus::Accepted;

    CertificateInfo bad{};
    bad.key = "/tmp/iso/bad/key.pem";
    bad.certificate_count = 0;
    // neither certificate nor certificate_single set
    certs.info.push_back(std::move(bad));

    certs.info.push_back(make_cert_info("/tmp/iso/good/chain.pem", "/tmp/iso/good/key.pem"));

    auto chains = module::charger::map_valid_chains(certs);

    ASSERT_EQ(chains.size(), 1u);
    EXPECT_EQ(chains[0].path_certificate_chain, "/tmp/iso/good/chain.pem");
}

TEST(MapValidChains, PropagatesCertificateRootToTrustAnchorPem) {
    types::evse_security::GetCertificateFullInfoResult certs{};
    certs.status = types::evse_security::GetCertificateInfoStatus::Accepted;
    types::evse_security::CertificateInfo info{};
    info.certificate = "/tmp/iso/a/chain.pem";
    info.key = "/tmp/iso/a/key.pem";
    info.certificate_count = 2;
    info.certificate_root = "----ROOT A PEM----";
    certs.info.push_back(info);

    const auto chains = module::charger::map_valid_chains(certs);
    ASSERT_EQ(chains.size(), 1u);
    ASSERT_TRUE(chains[0].trust_anchor_pem.has_value());
    EXPECT_EQ(chains[0].trust_anchor_pem.value(), "----ROOT A PEM----");
}

TEST(MapValidChains, OcspEntriesPreserveChainOrderIncludingNullopt) {
    using types::evse_security::CertificateOCSP;
    using types::evse_security::HashAlgorithm;

    GetCertificateFullInfoResult certs{};
    certs.status = GetCertificateInfoStatus::Accepted;

    auto info = make_cert_info("/tmp/iso/a/chain.pem", "/tmp/iso/a/key.pem");

    types::evse_security::CertificateHashData hash{};
    hash.hash_algorithm = HashAlgorithm::SHA256;

    CertificateOCSP ocsp0{};
    ocsp0.hash = hash;
    ocsp0.ocsp_path = "/p0";
    CertificateOCSP ocsp1{};
    ocsp1.hash = hash;
    ocsp1.ocsp_path = std::nullopt;
    CertificateOCSP ocsp2{};
    ocsp2.hash = hash;
    ocsp2.ocsp_path = "/p2";
    info.ocsp = std::vector<CertificateOCSP>{ocsp0, ocsp1, ocsp2};

    certs.info.push_back(std::move(info));

    const auto chains = module::charger::map_valid_chains(certs);

    ASSERT_EQ(chains.size(), 1u);
    const std::vector<std::optional<std::string>> expected{"/p0", std::nullopt, "/p2"};
    EXPECT_EQ(chains[0].ocsp_response_files, expected);
}

TEST(MapValidChains, RootlessChainIsStillMapped) {
    GetCertificateFullInfoResult certs{};
    certs.status = GetCertificateInfoStatus::Accepted;
    auto info = make_cert_info("/tmp/iso/a/chain.pem", "/tmp/iso/a/key.pem");
    // certificate_root intentionally left nullopt: the chain cannot participate in
    // TLS 1.3 certificate_authorities / trusted_ca_keys selection but is still
    // usable as the default chain, so it must remain mapped (a warning is logged).
    certs.info.push_back(std::move(info));

    const auto chains = module::charger::map_valid_chains(certs);

    ASSERT_EQ(chains.size(), 1u);
    EXPECT_EQ(chains[0].path_certificate_chain, "/tmp/iso/a/chain.pem");
    EXPECT_FALSE(chains[0].trust_anchor_pem.has_value());
}

TEST(CertStoreUpdateFilter, RelevantForV2gLeaf) {
    types::evse_security::CertificateStoreUpdate ev{};
    ev.leaf_certificate_type = types::evse_security::LeafCertificateType::V2G;
    EXPECT_TRUE(module::charger::is_relevant_certificate_store_update(ev));
}
TEST(CertStoreUpdateFilter, RelevantForV2gAndMoCa) {
    types::evse_security::CertificateStoreUpdate v2g{};
    v2g.ca_certificate_type = types::evse_security::CaCertificateType::V2G;
    EXPECT_TRUE(module::charger::is_relevant_certificate_store_update(v2g));
    types::evse_security::CertificateStoreUpdate mo{};
    mo.ca_certificate_type = types::evse_security::CaCertificateType::MO;
    EXPECT_TRUE(module::charger::is_relevant_certificate_store_update(mo));
}
TEST(CertStoreUpdateFilter, IrrelevantForNonV2gAndEmpty) {
    types::evse_security::CertificateStoreUpdate non_v2g_leaf{};
    non_v2g_leaf.leaf_certificate_type = types::evse_security::LeafCertificateType::CSMS;
    EXPECT_FALSE(module::charger::is_relevant_certificate_store_update(non_v2g_leaf));
    types::evse_security::CertificateStoreUpdate other_ca{};
    other_ca.ca_certificate_type = types::evse_security::CaCertificateType::CSMS;
    EXPECT_FALSE(module::charger::is_relevant_certificate_store_update(other_ca));
    types::evse_security::CertificateStoreUpdate empty{};
    EXPECT_FALSE(module::charger::is_relevant_certificate_store_update(empty));
}

TEST(CertUpdateDecision, ApplyWhenChainsPresent) {
    iso15118::config::SSLConfig cfg{};
    cfg.chains.push_back(iso15118::config::ChainConfig{});
    EXPECT_EQ(module::charger::decide_certificate_store_update(cfg), module::charger::CertUpdateAction::Apply);
}
TEST(CertUpdateDecision, PreserveWhenChainsEmpty) {
    iso15118::config::SSLConfig cfg{};
    EXPECT_EQ(module::charger::decide_certificate_store_update(cfg),
              module::charger::CertUpdateAction::PreserveLastGood);
}
