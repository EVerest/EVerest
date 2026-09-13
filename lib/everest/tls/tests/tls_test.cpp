// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest

#include "extensions/trusted_ca_keys.hpp"
#include <everest/tls/openssl_util.hpp>
#include <everest/tls/tls.hpp>
#include <gtest/gtest.h>
#include <iterator>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <cstring>
#include <memory>
#include <utility>
#include <vector>

std::string to_string(const std::uint8_t* const ptr, const std::size_t len) {
    std::stringstream string_stream;
    string_stream << std::hex;

    for (int idx = 0; idx < len; ++idx)
        string_stream << std::setw(2) << std::setfill('0') << (int)ptr[idx];

    return string_stream.str();
}

std::string to_string(const tls::OcspCache::digest_t& digest) {
    return to_string(reinterpret_cast<const std::uint8_t*>(&digest), sizeof(digest));
}

namespace {

TEST(strdup, usage) {
    // auto* r1 = strdup(nullptr); need to ensure non-nullptr
    auto* r2 = strdup("");
    auto* r3 = strdup("hello");
    // free(r1);
    free(r2);
    free(r3);
    free(nullptr);
}

TEST(string, use) {
    // was hoping to use std::string for config, but ...
    std::string empty;
    std::string space{""};
    std::string value{"something"};

    EXPECT_TRUE(empty.empty());
    // EXPECT_FALSE(space.empty()); was hoping it would be true
    EXPECT_FALSE(value.empty());

    // EXPECT_EQ(empty.c_str(), nullptr); was hoping it would be nullptr
    EXPECT_NE(space.c_str(), nullptr);
    EXPECT_NE(value.c_str(), nullptr);
}

TEST(ConfigItem, test) {
    // tests reduced with new ConfigStore implementation
    tls::ConfigItem i1;
    tls::ConfigItem i2{nullptr};
    tls::ConfigItem i3{"Hello"};
    tls::ConfigItem i4 = nullptr;
    tls::ConfigItem i5(nullptr);
    tls::ConfigItem i6("Hello");

    EXPECT_EQ(i1, nullptr);
    EXPECT_EQ(i4, nullptr);
    EXPECT_EQ(i5, nullptr);

    EXPECT_EQ(i2, i5);
    EXPECT_STREQ(i3, i6);

    EXPECT_EQ(i1, i2);
    EXPECT_NE(i1, i3);
    EXPECT_EQ(i1, i5);
    EXPECT_NE(i1, i6);

    tls::ConfigItem j2{""};

    auto j1(std::move(i3));
    j2 = std::move(i6);
    EXPECT_STREQ(i6, i3);
    EXPECT_STREQ(j1, j2);
    EXPECT_STREQ(j1, "Hello");
    EXPECT_NE(j1, i6);

    EXPECT_NE(j1, nullptr);
    EXPECT_NE(j2, nullptr);

    // EXPECT_EQ(i3, nullptr);
    // EXPECT_EQ(i6, nullptr);
    // EXPECT_EQ(i6, i3);

    std::vector<tls::ConfigItem> j3 = {"one", "two", nullptr};
    EXPECT_STREQ(j3[0], "one");
    EXPECT_STREQ(j3[1], "two");
    EXPECT_EQ(j3[2], nullptr);

    const char* p = j1;
    EXPECT_STREQ(p, "Hello");
    j1 = "Goodbye";
    EXPECT_STRNE(j1, "Hello");
    j1 = j2;
    EXPECT_STREQ(j1, j2);
}

TEST(ConfigItem, testB) {
    tls::ConfigItem i1;
    tls::ConfigItem i2{nullptr};
    tls::ConfigItem i3{""};
    tls::ConfigItem i4{"Hello"};

    EXPECT_EQ(i1, nullptr);
    EXPECT_EQ(i2, nullptr);
    EXPECT_STREQ(i3, "");
    EXPECT_STREQ(i4, "Hello");
    EXPECT_STREQ("Hello", i4);
}

using namespace tls::trusted_ca_keys;
using namespace openssl;

TEST(OcspCache, initEmpty) {
    tls::OcspCache cache;
    tls::OcspCache::digest_t digest{};
    auto res = cache.lookup(digest);
    EXPECT_EQ(res.get(), nullptr);
}

TEST(OcspCache, init) {
    tls::OcspCache cache;

    auto chain = openssl::load_certificates("client_chain.pem");
    std::vector<tls::OcspCache::ocsp_entry_t> entries;

    tls::OcspCache::digest_t digest{};
    for (const auto& cert : chain) {
        ASSERT_TRUE(tls::OcspCache::digest(digest, cert.get()));
        // std::cout << "digest: " << to_string(digest) << std::endl;
        entries.emplace_back(digest, "ocsp_response.der");
    }

    EXPECT_TRUE(cache.load(entries));
    // std::cout << "digest: " << to_string(digest) << std::endl;
    auto res = cache.lookup(digest);
    EXPECT_NE(res.get(), nullptr);
}

TEST(TrustedCaKeys, parseAudi) {

    /*
     * Audi
     * 0069 trusted_authorities_list length
     *   03 identifier_type cert_sha1_hash d8367e861f5807f8141fea572d676dbf58bb5f7c SHA1Hash
     *   03 identifier_type cert_sha1_hash b491ddd08fafe72d9f6f9bafc68eb04da84cc09a SHA1Hash
     *   03 identifier_type cert_sha1_hash 30aaaab25b1cc8a09a7b32652c33cc5a973c13f3 SHA1Hash
     *   03 identifier_type cert_sha1_hash 700bf78ad58e0819dac6fcaead5ed20f7bb0554f SHA1Hash
     *   03 identifier_type cert_sha1_hash 8c821f41604ed4c3431cf6d19f2ae107cf1f50e1 SHA1Hash
     */

    using trusted_authority = tls::trusted_ca_keys::trusted_authority;

    std::uint8_t extension[] = {
        0x00, 0x69, 0x03, 0xd8, 0x36, 0x7e, 0x86, 0x1f, 0x58, 0x07, 0xf8, 0x14, 0x1f, 0xea, 0x57, 0x2d, 0x67, 0x6d,
        0xbf, 0x58, 0xbb, 0x5f, 0x7c, 0x03, 0xb4, 0x91, 0xdd, 0xd0, 0x8f, 0xaf, 0xe7, 0x2d, 0x9f, 0x6f, 0x9b, 0xaf,
        0xc6, 0x8e, 0xb0, 0x4d, 0xa8, 0x4c, 0xc0, 0x9a, 0x03, 0x30, 0xaa, 0xaa, 0xb2, 0x5b, 0x1c, 0xc8, 0xa0, 0x9a,
        0x7b, 0x32, 0x65, 0x2c, 0x33, 0xcc, 0x5a, 0x97, 0x3c, 0x13, 0xf3, 0x03, 0x70, 0x0b, 0xf7, 0x8a, 0xd5, 0x8e,
        0x08, 0x19, 0xda, 0xc6, 0xfc, 0xae, 0xad, 0x5e, 0xd2, 0x0f, 0x7b, 0xb0, 0x55, 0x4f, 0x03, 0x8c, 0x82, 0x1f,
        0x41, 0x60, 0x4e, 0xd4, 0xc3, 0x43, 0x1c, 0xf6, 0xd1, 0x9f, 0x2a, 0xe1, 0x07, 0xcf, 0x1f, 0x50, 0xe1,
    };

    trusted_authority ext{&extension[0], sizeof(extension)};
    auto res = tls::trusted_ca_keys::convert(ext);
    EXPECT_EQ(res.cert_sha1_hash.size(), 5);
    EXPECT_EQ(res.key_sha1_hash.size(), 0);
    EXPECT_EQ(res.x509_name.size(), 0);
    EXPECT_FALSE(res.pre_agreed);
}

TEST(TrustedCaKeys, parseBuzz) {

    /*
     * Buzz
     * 002a trusted_authorities_list length
     *   03 identifier_type cert_sha1_hash d8367e861f5807f8141fea572d676dbf58bb5f7c SHA1Hash
     *   03 identifier_type cert_sha1_hash 8c821f41604ed4c3431cf6d19f2ae107cf1f50e1 SHA1Hash
     */

    using trusted_authority = tls::trusted_ca_keys::trusted_authority;

    std::uint8_t extension[] = {
        0x00, 0x2a, 0x03, 0xd8, 0x36, 0x7e, 0x86, 0x1f, 0x58, 0x07, 0xf8, 0x14, 0x1f, 0xea, 0x57,
        0x2d, 0x67, 0x6d, 0xbf, 0x58, 0xbb, 0x5f, 0x7c, 0x03, 0x8c, 0x82, 0x1f, 0x41, 0x60, 0x4e,
        0xd4, 0xc3, 0x43, 0x1c, 0xf6, 0xd1, 0x9f, 0x2a, 0xe1, 0x07, 0xcf, 0x1f, 0x50, 0xe1,
    };

    trusted_authority ext{&extension[0], sizeof(extension)};
    auto res = tls::trusted_ca_keys::convert(ext);
    EXPECT_EQ(res.cert_sha1_hash.size(), 2);
    EXPECT_EQ(res.key_sha1_hash.size(), 0);
    EXPECT_EQ(res.x509_name.size(), 0);
    EXPECT_FALSE(res.pre_agreed);
}

TEST(TrustedCaKeys, parseIoniq6) {

    /*
     * Ioniq 6 (invalid missing the size of trusted_authorities_list)
     *   01 identifier_type key_sha1_hash 4cd7290bf592d2c1ba90f56e08946d4c8e99dc38 SHA1Hash
     *   01 identifier_type key_sha1_hash 00fae3900795c888a4d4d7bd9fdffa60418ac19f SHA1Hash
     */

    using trusted_authority = tls::trusted_ca_keys::trusted_authority;

    std::uint8_t extension[] = {
        0x00, 0x2a, 0x01, 0x4c, 0xd7, 0x29, 0x0b, 0xf5, 0x92, 0xd2, 0xc1, 0xba, 0x90, 0xf5, 0x6e,
        0x08, 0x94, 0x6d, 0x4c, 0x8e, 0x99, 0xdc, 0x38, 0x01, 0x00, 0xfa, 0xe3, 0x90, 0x07, 0x95,
        0xc8, 0x88, 0xa4, 0xd4, 0xd7, 0xbd, 0x9f, 0xdf, 0xfa, 0x60, 0x41, 0x8a, 0xc1, 0x9f,
    };

    trusted_authority ext{&extension[2], sizeof(extension) - 2};
    auto res = tls::trusted_ca_keys::convert(ext);
    EXPECT_EQ(res.cert_sha1_hash.size(), 0);
    EXPECT_EQ(res.key_sha1_hash.size(), 0);
    EXPECT_EQ(res.x509_name.size(), 0);
    EXPECT_FALSE(res.pre_agreed);

    ext = trusted_authority{&extension[0], sizeof(extension)};
    res = tls::trusted_ca_keys::convert(ext);
    EXPECT_EQ(res.cert_sha1_hash.size(), 0);
    EXPECT_EQ(res.key_sha1_hash.size(), 2);
    EXPECT_EQ(res.x509_name.size(), 0);
    EXPECT_FALSE(res.pre_agreed);
}

TEST(TrustedCaKeys, generateBuzz) {

    /*
     * Buzz
     * 002a trusted_authorities_list length
     *   03 identifier_type cert_sha1_hash d8367e861f5807f8141fea572d676dbf58bb5f7c SHA1Hash
     *   03 identifier_type cert_sha1_hash 8c821f41604ed4c3431cf6d19f2ae107cf1f50e1 SHA1Hash
     */

    using trusted_ca_keys_t = tls::trusted_ca_keys::trusted_ca_keys_t;

    openssl::sha_1_digest_t hash1 = {
        0xd8, 0x36, 0x7e, 0x86, 0x1f, 0x58, 0x07, 0xf8, 0x14, 0x1f,
        0xea, 0x57, 0x2d, 0x67, 0x6d, 0xbf, 0x58, 0xbb, 0x5f, 0x7c,
    };
    openssl::sha_1_digest_t hash2 = {
        0x8c, 0x82, 0x1f, 0x41, 0x60, 0x4e, 0xd4, 0xc3, 0x43, 0x1c,
        0xf6, 0xd1, 0x9f, 0x2a, 0xe1, 0x07, 0xcf, 0x1f, 0x50, 0xe1,
    };

    std::uint8_t extension[] = {
        0x00, 0x2a, 0x03, 0xd8, 0x36, 0x7e, 0x86, 0x1f, 0x58, 0x07, 0xf8, 0x14, 0x1f, 0xea, 0x57,
        0x2d, 0x67, 0x6d, 0xbf, 0x58, 0xbb, 0x5f, 0x7c, 0x03, 0x8c, 0x82, 0x1f, 0x41, 0x60, 0x4e,
        0xd4, 0xc3, 0x43, 0x1c, 0xf6, 0xd1, 0x9f, 0x2a, 0xe1, 0x07, 0xcf, 0x1f, 0x50, 0xe1,
    };

    trusted_ca_keys_t tck;
    tck.cert_sha1_hash.push_back(hash1);
    tck.cert_sha1_hash.push_back(hash2);

    auto res = tls::trusted_ca_keys::convert(tck);

    EXPECT_EQ(res.size(), sizeof(extension));
    EXPECT_EQ(std::memcmp(res.get(), &extension, sizeof(extension)), 0);
    // std::cout << "A: " << to_string(std::get<TrustedAuthority_ptr>(res).get(), sizeof(extension)) << std::endl;
    // std::cout << "B: " << to_string(&extension[0], sizeof(extension)) << std::endl;
}

TEST(TrustedCaKeys, CertChain) {
    // match server certificate to trust anchors
    auto server_root = openssl::load_certificates("server_root_cert.pem");
    auto server_ca = openssl::load_certificates("server_ca_cert.pem");
    auto server = openssl::load_certificates("server_cert.pem");
    auto alt_server_root = openssl::load_certificates("alt_server_root_cert.pem");
    auto alt_server_ca = openssl::load_certificates("alt_server_ca_cert.pem");
    auto alt_server = openssl::load_certificates("alt_server_cert.pem");

    openssl::certificate_list chain;
    std::move(server_ca.begin(), server_ca.end(), std::back_inserter(chain));
    std::move(alt_server_ca.begin(), alt_server_ca.end(), std::back_inserter(chain));

    ASSERT_EQ(server_root.size(), 1);
    ASSERT_EQ(alt_server_root.size(), 1);

    EXPECT_EQ(openssl::verify_certificate(server[0].get(), server_root, chain), openssl::verify_result_t::Verified);
    EXPECT_EQ(openssl::verify_certificate(alt_server[0].get(), alt_server_root, chain),
              openssl::verify_result_t::Verified);

    EXPECT_EQ(openssl::verify_certificate(server[0].get(), alt_server_root, chain),
              openssl::verify_result_t::CertificateNotAllowed);
    EXPECT_EQ(openssl::verify_certificate(alt_server[0].get(), server_root, chain),
              openssl::verify_result_t::CertificateNotAllowed);
}

TEST(TrustedCaKeys, matchNone) {

    trusted_ca_keys_t keys;
    chain_t chain;

    EXPECT_FALSE(match(keys, chain));

    auto root = load_certificates("server_root_cert.pem");
    const auto* root_cert = root[0].get();

    sha_1_digest_t digest;

    keys.x509_name.emplace_back(certificate_subject_der(root_cert));
    EXPECT_TRUE(certificate_sha_1(digest, root_cert));
    keys.cert_sha1_hash.push_back(digest);
    EXPECT_TRUE(certificate_subject_public_key_sha_1(digest, root_cert));
    keys.key_sha1_hash.push_back(digest);

    EXPECT_FALSE(match(keys, chain));

    auto alt_root = load_certificates("client_root_cert.pem");
    chain.chain.trust_anchors = std::move(alt_root);
    EXPECT_FALSE(match(keys, chain));
}

TEST(TrustedCaKeys, matchName) {

    trusted_ca_keys_t keys;
    chain_t chain;

    auto root = load_certificates("server_root_cert.pem");
    chain.chain.trust_anchors = std::move(root);

    const auto* root_cert = chain.chain.trust_anchors[0].get();

    keys.x509_name.emplace_back(certificate_subject_der(root_cert));
    EXPECT_TRUE(match(keys, chain));

    sha_1_digest_t digest;

    EXPECT_TRUE(certificate_sha_1(digest, root_cert));
    keys.cert_sha1_hash.push_back(digest);
    EXPECT_TRUE(match(keys, chain));
    EXPECT_TRUE(certificate_subject_public_key_sha_1(digest, root_cert));
    keys.key_sha1_hash.push_back(digest);
    EXPECT_TRUE(match(keys, chain));
}

TEST(TrustedCaKeys, matchCertHash) {

    trusted_ca_keys_t keys;
    chain_t chain;

    auto root = load_certificates("server_root_cert.pem");
    chain.chain.trust_anchors.emplace_back(std::move(root[0]));
    root = load_certificates("client_root_cert.pem");
    chain.chain.trust_anchors.emplace_back(std::move(root[0]));
    ASSERT_EQ(chain.chain.trust_anchors.size(), 2);

    const auto* server_root_cert = chain.chain.trust_anchors[0].get();
    const auto* client_root_cert = chain.chain.trust_anchors[1].get();

    sha_1_digest_t digest;
    EXPECT_TRUE(certificate_sha_1(digest, client_root_cert));
    keys.cert_sha1_hash.push_back(digest);
    EXPECT_TRUE(match(keys, chain));

    EXPECT_TRUE(certificate_sha_1(digest, server_root_cert));
    keys.cert_sha1_hash.clear();
    keys.cert_sha1_hash.push_back(digest);
    EXPECT_TRUE(match(keys, chain));
}

TEST(TrustedCaKeys, matchKeyHash) {

    trusted_ca_keys_t keys;
    chain_t chain;

    auto root = load_certificates("server_root_cert.pem");
    chain.chain.trust_anchors.emplace_back(std::move(root[0]));
    root = load_certificates("client_root_cert.pem");
    chain.chain.trust_anchors.emplace_back(std::move(root[0]));
    ASSERT_EQ(chain.chain.trust_anchors.size(), 2);

    const auto* server_root_cert = chain.chain.trust_anchors[0].get();
    const auto* client_root_cert = chain.chain.trust_anchors[1].get();

    sha_1_digest_t digest;
    EXPECT_TRUE(certificate_subject_public_key_sha_1(digest, client_root_cert));
    keys.key_sha1_hash.push_back(digest);
    EXPECT_TRUE(match(keys, chain));

    EXPECT_TRUE(certificate_subject_public_key_sha_1(digest, server_root_cert));
    keys.key_sha1_hash.clear();
    keys.key_sha1_hash.push_back(digest);
    EXPECT_TRUE(match(keys, chain));
}

TEST(TrustedCaKeys, selectNone) {

    trusted_ca_keys_t keys;
    chain_list chains;
    EXPECT_EQ(select(keys, chains), nullptr);

    chains.emplace_back();
    auto root = load_certificates("server_root_cert.pem");
    chains[0].chain.trust_anchors.emplace_back(std::move(root[0]));
    EXPECT_EQ(select(keys, chains), nullptr);

    chains.emplace_back();
    root = load_certificates("alt_server_root_cert.pem");
    chains[1].chain.trust_anchors.emplace_back(std::move(root[0]));
    EXPECT_EQ(select(keys, chains), nullptr);

    sha_1_digest_t digest;
    root = load_certificates("client_root_cert.pem");
    auto* client_root_cert = root[0].get();
    EXPECT_TRUE(certificate_sha_1(digest, client_root_cert));
    keys.cert_sha1_hash.push_back(digest);
    EXPECT_TRUE(certificate_subject_public_key_sha_1(digest, client_root_cert));
    keys.key_sha1_hash.push_back(digest);
}

TEST(TrustedCaKeys, select) {

    trusted_ca_keys_t keys;
    chain_list chains;
    EXPECT_EQ(select(keys, chains), nullptr);

    chains.emplace_back();
    auto root = load_certificates("server_root_cert.pem");
    chains[0].chain.trust_anchors.emplace_back(std::move(root[0]));
    EXPECT_EQ(select(keys, chains), nullptr);

    chains.emplace_back();
    root = load_certificates("alt_server_root_cert.pem");
    chains[1].chain.trust_anchors.emplace_back(std::move(root[0]));
    EXPECT_EQ(select(keys, chains), nullptr);

    sha_1_digest_t digest;
    root = load_certificates("client_root_cert.pem");
    auto* client_root_cert = root[0].get();
    EXPECT_TRUE(certificate_sha_1(digest, client_root_cert));
    keys.cert_sha1_hash.push_back(digest);
    EXPECT_TRUE(certificate_subject_public_key_sha_1(digest, client_root_cert));
    keys.key_sha1_hash.push_back(digest);

    chains.emplace_back();
    root = load_certificates("client_root_cert.pem");
    chains[2].chain.trust_anchors.emplace_back(std::move(root[0]));
    auto result = select(keys, chains);
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result, &chains[2]);
}

namespace {

// Build a STACK_OF(X509_NAME) from the subject names of the given certs.
// Caller owns the returned stack and must release it with sk_X509_NAME_pop_free.
STACK_OF(X509_NAME) * make_dn_stack(const std::vector<X509*>& certs) {
    auto* stack = sk_X509_NAME_new_null();
    for (auto* cert : certs) {
        auto* name = X509_get_subject_name(cert);
        sk_X509_NAME_push(stack, X509_NAME_dup(name));
    }
    return stack;
}

} // namespace

TEST(TrustedCaKeys, selectByDnListEmpty) {
    chain_list chains;
    chains.emplace_back();
    auto root = load_certificates("server_root_cert.pem");
    chains[0].chain.trust_anchors.emplace_back(std::move(root[0]));

    chains.emplace_back();
    auto alt_root = load_certificates("alt_server_root_cert.pem");
    chains[1].chain.trust_anchors.emplace_back(std::move(alt_root[0]));

    auto* stack = sk_X509_NAME_new_null();
    EXPECT_EQ(select_by_dn_list(stack, chains), nullptr);
    sk_X509_NAME_pop_free(stack, X509_NAME_free);
}

TEST(TrustedCaKeys, selectByDnListNullNames) {
    chain_list chains;
    chains.emplace_back();
    auto root = load_certificates("server_root_cert.pem");
    chains[0].chain.trust_anchors.emplace_back(std::move(root[0]));

    chains.emplace_back();
    auto alt_root = load_certificates("alt_server_root_cert.pem");
    chains[1].chain.trust_anchors.emplace_back(std::move(alt_root[0]));

    EXPECT_EQ(select_by_dn_list(nullptr, chains), nullptr);
}

TEST(TrustedCaKeys, selectByDnListMatch) {
    chain_list chains;
    chains.emplace_back();
    auto server_root = load_certificates("server_root_cert.pem");
    chains[0].chain.trust_anchors.emplace_back(std::move(server_root[0]));

    chains.emplace_back();
    auto alt_root = load_certificates("alt_server_root_cert.pem");
    chains[1].chain.trust_anchors.emplace_back(std::move(alt_root[0]));

    // The DN list contains the alt root's subject; selection should return chains[1].
    auto* stack = make_dn_stack({chains[1].chain.trust_anchors[0].get()});
    auto* result = select_by_dn_list(stack, chains);
    EXPECT_EQ(result, &chains[1]);
    sk_X509_NAME_pop_free(stack, X509_NAME_free);
}

TEST(TrustedCaKeys, selectByDnListNoMatch) {
    chain_list chains;
    chains.emplace_back();
    auto server_root = load_certificates("server_root_cert.pem");
    chains[0].chain.trust_anchors.emplace_back(std::move(server_root[0]));

    chains.emplace_back();
    auto alt_root = load_certificates("alt_server_root_cert.pem");
    chains[1].chain.trust_anchors.emplace_back(std::move(alt_root[0]));

    // The DN list contains a name that matches no chain's trust anchor.
    auto unrelated = load_certificates("client_root_cert.pem");
    auto* stack = make_dn_stack({unrelated[0].get()});
    EXPECT_EQ(select_by_dn_list(stack, chains), nullptr);
    sk_X509_NAME_pop_free(stack, X509_NAME_free);
}

TEST(TrustedCaKeys, selectByDnListMatchFirstChain) {
    chain_list chains;
    chains.emplace_back();
    auto server_root = load_certificates("server_root_cert.pem");
    chains[0].chain.trust_anchors.emplace_back(std::move(server_root[0]));

    chains.emplace_back();
    auto alt_root = load_certificates("alt_server_root_cert.pem");
    chains[1].chain.trust_anchors.emplace_back(std::move(alt_root[0]));

    // The DN list contains the server root's subject; selection should return chains[0].
    auto* stack = make_dn_stack({chains[0].chain.trust_anchors[0].get()});
    EXPECT_EQ(select_by_dn_list(stack, chains), &chains[0]);
    sk_X509_NAME_pop_free(stack, X509_NAME_free);
}

TEST(TrustedCaKeys, selectByDnListFirstChainWins) {
    chain_list chains;
    chains.emplace_back();
    auto server_root = load_certificates("server_root_cert.pem");
    chains[0].chain.trust_anchors.emplace_back(std::move(server_root[0]));

    chains.emplace_back();
    auto alt_root = load_certificates("alt_server_root_cert.pem");
    chains[1].chain.trust_anchors.emplace_back(std::move(alt_root[0]));

    // Both chains' DNs are advertised (alt root first in the stack); chain
    // iteration order wins, not DN stack order.
    auto* stack = make_dn_stack({chains[1].chain.trust_anchors[0].get(), chains[0].chain.trust_anchors[0].get()});
    EXPECT_EQ(select_by_dn_list(stack, chains), &chains[0]);
    sk_X509_NAME_pop_free(stack, X509_NAME_free);
}

TEST(TrustedCaKeys, selectByDnListSecondAnchorMatch) {
    chain_list chains;
    chains.emplace_back();
    auto server_root = load_certificates("server_root_cert.pem");
    chains[0].chain.trust_anchors.emplace_back(std::move(server_root[0]));

    // A chain with two trust anchors where only the second matches the
    // advertised DN.
    chains.emplace_back();
    auto client_root = load_certificates("client_root_cert.pem");
    auto alt_root = load_certificates("alt_server_root_cert.pem");
    chains[1].chain.trust_anchors.emplace_back(std::move(client_root[0]));
    chains[1].chain.trust_anchors.emplace_back(std::move(alt_root[0]));

    auto* stack = make_dn_stack({chains[1].chain.trust_anchors[1].get()});
    EXPECT_EQ(select_by_dn_list(stack, chains), &chains[1]);
    sk_X509_NAME_pop_free(stack, X509_NAME_free);
}

// ----------------------------------------------------------------------------
// ServerTrustedCaKeys apply path via handle_certificate_cb (TLS 1.2 leg)

std::vector<std::pair<openssl::log_level_t, std::string>> captured_logs;

void capture_log(openssl::log_level_t level, const std::string& str) {
    captured_logs.emplace_back(level, str);
}

// Captures library log output for the lifetime of the object; restores the
// previous handler on destruction.
class LogCapture {
public:
    LogCapture() : previous(openssl::set_log_handler(&capture_log)) {
        captured_logs.clear();
    }
    ~LogCapture() {
        openssl::set_log_handler(previous);
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    static bool contains(openssl::log_level_t level, const std::string& needle) {
        for (const auto& [entry_level, entry_str] : captured_logs) {
            if ((entry_level == level) && (entry_str.find(needle) != std::string::npos)) {
                return true;
            }
        }
        return false;
    }

private:
    openssl::log_handler_t previous;
};

chain_t make_test_chain(const char* leaf, const char* ca, const char* root, const char* key) {
    chain_t result;
    result.chain = load_certificates(leaf, ca, root);
    result.private_key = load_private_key(key, nullptr);
    return result;
}

trusted_ca_keys_t make_cert_hash_keys(const char* root_file) {
    trusted_ca_keys_t keys;
    auto root = load_certificates(root_file);
    sha_1_digest_t digest{};
    if (!root.empty() && certificate_sha_1(digest, root[0].get())) {
        keys.cert_sha1_hash.push_back(digest);
    }
    return keys;
}

// Drives ServerTrustedCaKeys::handle_certificate_cb on a raw server SSL*
// pinned below TLS 1.3 so the legacy trusted_ca_keys leg is taken.
class ServerTrustedCaKeysApply : public testing::Test {
protected:
    void SetUp() override {
        // TLSv1_2_server_method (rather than TLS_server_method + a version
        // clamp) so that SSL_version() on the unconnected SSL reports
        // TLS1_2_VERSION and handle_certificate_cb takes the legacy leg.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        ctx = SSL_CTX_new(TLSv1_2_server_method());
#pragma GCC diagnostic pop
        ASSERT_NE(ctx, nullptr);
        ssl = SSL_new(ctx);
        ASSERT_NE(ssl, nullptr);
        ASSERT_EQ(SSL_version(ssl), TLS1_2_VERSION);
        flags.trusted_ca_keys_received();
    }

    void TearDown() override {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
    }

    void set_peer_keys(trusted_ca_keys_t&& peer_keys) {
        keys = std::move(peer_keys);
        data = std::make_unique<server_trusted_ca_keys_t>(server_trusted_ca_keys_t{keys, flags});
        ServerTrustedCaKeys::set_data(ssl, data.get());
    }

    static void expect_installed_leaf(SSL* ssl_ptr, const char* leaf_file) {
        auto expected = load_certificates(leaf_file);
        ASSERT_EQ(expected.size(), 1);
        auto* installed = SSL_get_certificate(ssl_ptr);
        ASSERT_NE(installed, nullptr);
        EXPECT_EQ(X509_cmp(installed, expected[0].get()), 0);
    }

    SSL_CTX* ctx{nullptr};
    SSL* ssl{nullptr};
    ServerTrustedCaKeys tck;
    trusted_ca_keys_t keys;
    tls::StatusFlags flags;
    std::unique_ptr<server_trusted_ca_keys_t> data;
};

TEST_F(ServerTrustedCaKeysApply, selectedChainApplies) {
    chain_list chains;
    chains.push_back(
        make_test_chain("server_cert.pem", "server_ca_cert.pem", "server_root_cert.pem", "server_priv.pem"));
    chains.push_back(make_test_chain("alt_server_cert.pem", "alt_server_ca_cert.pem", "alt_server_root_cert.pem",
                                     "alt_server_priv.pem"));
    tck.update(std::move(chains));

    set_peer_keys(make_cert_hash_keys("alt_server_root_cert.pem"));
    ASSERT_FALSE(keys.cert_sha1_hash.empty());

    LogCapture capture;
    EXPECT_EQ(ServerTrustedCaKeys::handle_certificate_cb(ssl, &tck), 1);
    expect_installed_leaf(ssl, "alt_server_cert.pem");
}

TEST_F(ServerTrustedCaKeysApply, selectedChainFailsFallsBackToDefault) {
    chain_list chains;
    chains.push_back(
        make_test_chain("server_cert.pem", "server_ca_cert.pem", "server_root_cert.pem", "server_priv.pem"));
    // The selected chain's private key does not match its leaf, so the apply
    // fails and the (different) default chain is served instead.
    chains.push_back(make_test_chain("alt_server_cert.pem", "alt_server_ca_cert.pem", "alt_server_root_cert.pem",
                                     "server_priv.pem"));
    tck.update(std::move(chains));

    set_peer_keys(make_cert_hash_keys("alt_server_root_cert.pem"));
    ASSERT_FALSE(keys.cert_sha1_hash.empty());

    LogCapture capture;
    EXPECT_EQ(ServerTrustedCaKeys::handle_certificate_cb(ssl, &tck), 1);
    expect_installed_leaf(ssl, "server_cert.pem");
    EXPECT_TRUE(LogCapture::contains(openssl::log_level_t::warning, "failed to apply; serving default chain"));
}

TEST_F(ServerTrustedCaKeysApply, onlyChainFailsAborts) {
    chain_list chains;
    // Single chain: selected AND default, with a mismatched private key so the
    // apply fails. There is no other chain to fall back to: abort.
    chains.push_back(make_test_chain("alt_server_cert.pem", "alt_server_ca_cert.pem", "alt_server_root_cert.pem",
                                     "server_priv.pem"));
    tck.update(std::move(chains));

    set_peer_keys(make_cert_hash_keys("alt_server_root_cert.pem"));
    ASSERT_FALSE(keys.cert_sha1_hash.empty());

    LogCapture capture;
    EXPECT_EQ(ServerTrustedCaKeys::handle_certificate_cb(ssl, &tck), 0);
    EXPECT_TRUE(LogCapture::contains(openssl::log_level_t::warning, "only/default chain"));
}

TEST_F(ServerTrustedCaKeysApply, noMatchKeepsDefaultAndWarns) {
    chain_list chains;
    chains.push_back(
        make_test_chain("server_cert.pem", "server_ca_cert.pem", "server_root_cert.pem", "server_priv.pem"));
    tck.update(std::move(chains));

    // The peer's trusted_ca_keys match no configured chain.
    set_peer_keys(make_cert_hash_keys("client_root_cert.pem"));
    ASSERT_FALSE(keys.cert_sha1_hash.empty());

    LogCapture capture;
    EXPECT_EQ(ServerTrustedCaKeys::handle_certificate_cb(ssl, &tck), 1);
    // No chain is installed on the SSL: the default certificate configured on
    // the SSL_CTX is kept.
    EXPECT_EQ(SSL_get_certificate(ssl), nullptr);
    EXPECT_TRUE(LogCapture::contains(openssl::log_level_t::warning, "no configured chain matched"));
}

} // namespace
