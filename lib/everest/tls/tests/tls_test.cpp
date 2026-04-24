// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest

#include "extensions/trusted_ca_keys.hpp"
#include <everest/tls/openssl_util.hpp>
#include <everest/tls/tls.hpp>
#include <gtest/gtest.h>
#include <iterator>

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

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

// ----------------------------------------------------------------------------
// Signature-algorithm pinning tests
//
// Verify that openssl::pin_sigalgs_to_cert_curve():
//  - narrows the SSL_CTX sigalgs list to the single ECDSA+hash pair matching
//    the leaf certificate's EC curve (P-256 -> SHA256, P-384 -> SHA384,
//    P-521 -> SHA512);
//  - is a no-op for non-EC (e.g. RSA) leaf certificates, leaving OpenSSL's
//    default sigalgs list intact.
//
// Inspection strategy: rather than scrape internal state out of SSL_CTX, we
// drive a full TLS 1.2 handshake between two SSL objects wired together with
// in-memory BIO pairs, then query SSL_get_peer_signature_nid() on the client
// side. The NID reported there is the signature algorithm the server actually
// produced during CertificateVerify / ServerKeyExchange, which is exactly
// what the pin is supposed to constrain.
namespace sigalg_pin_test {

using SSL_CTX_ptr = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
using SSL_ptr = std::unique_ptr<SSL, decltype(&SSL_free)>;
using X509_ptr = std::unique_ptr<X509, decltype(&X509_free)>;
using EVP_PKEY_ptr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using EVP_PKEY_CTX_ptr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using BIGNUM_ptr = std::unique_ptr<BIGNUM, decltype(&BN_free)>;

// Advertise all three ECDSA sigalgs on the client so the pin on the server is
// the only thing that can pick a single pair.
constexpr auto client_ecdsa_sigalgs = "ECDSA+SHA256:ECDSA+SHA384:ECDSA+SHA512";

// For the RSA negative path, advertise only RSA-based sigalgs on the client.
// If the helper erroneously pinned the server to ECDSA-only, no overlap would
// exist and the handshake would fail with a no-shared-sigalgs alert.
constexpr auto client_rsa_sigalgs = "RSA-PSS+SHA256:RSA-PSS+SHA384:RSA-PSS+SHA512:RSA+SHA256:RSA+SHA384:RSA+SHA512";

/// Result of a mini handshake: both the hash/digest NID of the server's
/// signature (e.g. NID_sha256) and the signature key-type NID (e.g.
/// EVP_PKEY_EC / EVP_PKEY_RSA). Both are zero when the handshake failed.
struct HandshakeResult {
    int hash_nid = 0;
    int sig_type_nid = 0;
};

/// Run a complete TLS 1.2 handshake between a pre-built server CTX (with leaf
/// cert already loaded) and a fresh client CTX, using an in-memory BIO pair.
/// Reports back what the client observed for the server's signature.
HandshakeResult do_memory_handshake(SSL_CTX* server_ctx, const char* client_sigalgs) {
    HandshakeResult res{};
    SSL_CTX_ptr client_ctx_owner(SSL_CTX_new(TLS_client_method()), &SSL_CTX_free);
    if (!client_ctx_owner) {
        return res;
    }
    SSL_CTX* client_ctx = client_ctx_owner.get();

    SSL_CTX_set_min_proto_version(client_ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(client_ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(client_ctx, SSL_VERIFY_NONE, nullptr);
    if (SSL_CTX_set1_sigalgs_list(client_ctx, client_sigalgs) != 1) {
        return res;
    }

    SSL_ptr server_ssl(SSL_new(server_ctx), &SSL_free);
    SSL_ptr client_ssl(SSL_new(client_ctx), &SSL_free);
    if (!server_ssl || !client_ssl) {
        return res;
    }
    SSL_set_accept_state(server_ssl.get());
    SSL_set_connect_state(client_ssl.get());

    // A BIO pair creates two linked memory BIOs that shuttle bytes between
    // the two SSL objects without touching a real socket.
    BIO* server_bio = nullptr;
    BIO* client_bio = nullptr;
    if (BIO_new_bio_pair(&server_bio, 0, &client_bio, 0) != 1) {
        return res;
    }
    SSL_set_bio(server_ssl.get(), server_bio, server_bio);
    SSL_set_bio(client_ssl.get(), client_bio, client_bio);

    // Drive both sides alternately until both succeed or we run out of
    // iterations. 32 rounds is plenty for a TLS 1.2 handshake.
    bool server_done = false;
    bool client_done = false;
    for (int i = 0; i < 32 && !(server_done && client_done); ++i) {
        if (!client_done) {
            const int rc = SSL_do_handshake(client_ssl.get());
            if (rc == 1) {
                client_done = true;
            } else {
                const int err = SSL_get_error(client_ssl.get(), rc);
                if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                    return res;
                }
            }
        }
        if (!server_done) {
            const int rc = SSL_do_handshake(server_ssl.get());
            if (rc == 1) {
                server_done = true;
            } else {
                const int err = SSL_get_error(server_ssl.get(), rc);
                if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                    return res;
                }
            }
        }
    }

    if (!(server_done && client_done)) {
        return res;
    }

    (void)SSL_get_peer_signature_nid(client_ssl.get(), &res.hash_nid);
    (void)SSL_get_peer_signature_type_nid(client_ssl.get(), &res.sig_type_nid);
    return res;
}

/// Build an SSL_CTX with the given server chain + private key loaded, then
/// invoke openssl::pin_sigalgs_to_cert_curve() against it. This mirrors what
/// tls::Server::init_ssl() does internally.
SSL_CTX_ptr make_server_ctx_from_files(const std::string& chain_path, const std::string& key_path, bool& helper_ok) {
    SSL_CTX_ptr ctx(SSL_CTX_new(TLS_server_method()), &SSL_CTX_free);
    if (!ctx) {
        helper_ok = false;
        return ctx;
    }
    SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx.get(), TLS1_2_VERSION);
    if (SSL_CTX_use_certificate_chain_file(ctx.get(), chain_path.c_str()) != 1) {
        helper_ok = false;
        return ctx;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx.get(), key_path.c_str(), SSL_FILETYPE_PEM) != 1) {
        helper_ok = false;
        return ctx;
    }
    if (SSL_CTX_check_private_key(ctx.get()) != 1) {
        helper_ok = false;
        return ctx;
    }
    helper_ok = openssl::pin_sigalgs_to_cert_curve(ctx.get());
    return ctx;
}

/// Generate an RSA-2048 self-signed certificate entirely in-memory so the RSA
/// negative test doesn't depend on PKI fixtures.
bool make_rsa_self_signed(EVP_PKEY_ptr& key_out, X509_ptr& cert_out) {
    EVP_PKEY_CTX_ptr kctx(EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr), &EVP_PKEY_CTX_free);
    if (!kctx || EVP_PKEY_keygen_init(kctx.get()) <= 0) {
        return false;
    }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(kctx.get(), 2048) <= 0) {
        return false;
    }
    EVP_PKEY* pkey_raw = nullptr;
    if (EVP_PKEY_keygen(kctx.get(), &pkey_raw) <= 0) {
        return false;
    }
    EVP_PKEY_ptr pkey(pkey_raw, &EVP_PKEY_free);

    X509_ptr cert(X509_new(), &X509_free);
    if (!cert) {
        return false;
    }
    X509_set_version(cert.get(), 2); // X.509 v3
    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert.get()), 60L * 60 * 24 * 1); // 1 day
    X509_set_pubkey(cert.get(), pkey.get());

    X509_NAME* name = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("rsa-test-leaf"), -1,
                               -1, 0);
    X509_set_issuer_name(cert.get(), name);

    if (X509_sign(cert.get(), pkey.get(), EVP_sha256()) == 0) {
        return false;
    }
    key_out = std::move(pkey);
    cert_out = std::move(cert);
    return true;
}

/// Translate the OpenSSL group short-name (e.g. "prime256v1", "secp521r1")
/// returned by EVP_PKEY_get_group_name() into the NIST/IANA curve label
/// ("P-256", "P-521") used by the test parametrisation and by the sigalg pin
/// helper's internal mapping.
std::string openssl_group_to_nist(const std::string& group) {
    if (group == "prime256v1" || group == "P-256") {
        return "P-256";
    }
    if (group == "secp384r1" || group == "P-384") {
        return "P-384";
    }
    if (group == "secp521r1" || group == "P-521") {
        return "P-521";
    }
    return group;
}

class SigalgPin : public ::testing::TestWithParam<const char*> {};

TEST_P(SigalgPin, PeerSignatureNidMatchesLeafCurve) {
    const std::string curve = GetParam(); // "P-256", "P-384", "P-521"
    const std::string chain_path = "pki-" + curve + "/server_chain.pem";
    const std::string key_path = "pki-" + curve + "/server_priv.pem";

    bool helper_ok = false;
    SSL_CTX_ptr server_ctx = make_server_ctx_from_files(chain_path, key_path, helper_ok);
    ASSERT_TRUE(server_ctx) << "SSL_CTX_new / load failed for curve " << curve;
    ASSERT_TRUE(helper_ok) << "pin_sigalgs_to_cert_curve returned false for " << curve;

    // Cross-check that the leaf we actually loaded uses the expected curve.
    // EVP_PKEY_get_group_name() returns the OpenSSL short-name
    // ("prime256v1"/"secp384r1"/"secp521r1"); normalise to the NIST label.
    X509* leaf = SSL_CTX_get0_certificate(server_ctx.get());
    ASSERT_NE(leaf, nullptr);
    EVP_PKEY* pkey = X509_get0_pubkey(leaf);
    ASSERT_NE(pkey, nullptr);
    ASSERT_EQ(EVP_PKEY_id(pkey), EVP_PKEY_EC);
    std::array<char, 80> name{};
    std::size_t name_len = 0;
    ASSERT_EQ(EVP_PKEY_get_group_name(pkey, name.data(), name.size(), &name_len), 1);
    const std::string leaf_group(name.data(), name_len);
    EXPECT_EQ(openssl_group_to_nist(leaf_group), curve) << "leaf cert group mismatch: " << leaf_group;

    // SSL_get_peer_signature_nid() returns the HASH part of the sigalg pair
    // (NID_sha256/384/512). Combined with SSL_get_peer_signature_type_nid()
    // (expected EVP_PKEY_EC for ECDSA) this pins down the exact (ECDSA, SHAx)
    // choice. With the server pinned to one sigalg and the client offering
    // all three ECDSA sigalgs, any deviation means the pin didn't take.
    const int expected_hash_nid = (curve == "P-256")   ? NID_sha256
                                  : (curve == "P-384") ? NID_sha384
                                  : (curve == "P-521") ? NID_sha512
                                                       : 0;
    ASSERT_NE(expected_hash_nid, 0) << "unhandled curve param " << curve;

    const HandshakeResult res = do_memory_handshake(server_ctx.get(), client_ecdsa_sigalgs);
    EXPECT_EQ(res.hash_nid, expected_hash_nid)
        << "peer hash NID " << res.hash_nid << " != expected " << expected_hash_nid << " for curve " << curve;
    EXPECT_EQ(res.sig_type_nid, EVP_PKEY_EC) << "expected ECDSA signature type for curve " << curve;
}

INSTANTIATE_TEST_SUITE_P(AllCurves, SigalgPin, ::testing::Values("P-256", "P-384", "P-521"));

TEST(SigalgPinRsa, DoesNotNarrowForNonEcLeaf) {
    // Build an SSL_CTX with an RSA self-signed cert and run the pin helper.
    // The helper should be a no-op and return true; the handshake should then
    // succeed against a client that advertises only RSA-family sigalgs, which
    // could not happen if the server were pinned to ECDSA-only. The
    // complementary TLS 1.2 cipher_list restriction to RSA key exchange keeps
    // the cert selection aligned with the sigalg family.
    EVP_PKEY_ptr key(nullptr, &EVP_PKEY_free);
    X509_ptr cert(nullptr, &X509_free);
    ASSERT_TRUE(make_rsa_self_signed(key, cert));

    SSL_CTX_ptr server_ctx(SSL_CTX_new(TLS_server_method()), &SSL_CTX_free);
    ASSERT_TRUE(server_ctx);
    SSL_CTX_set_min_proto_version(server_ctx.get(), TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(server_ctx.get(), TLS1_2_VERSION);
    // Restrict to RSA-authenticated cipher suites so the server has no choice
    // but to authenticate with the RSA leaf.
    ASSERT_EQ(SSL_CTX_set_cipher_list(server_ctx.get(), "aRSA"), 1);
    ASSERT_EQ(SSL_CTX_use_certificate(server_ctx.get(), cert.get()), 1);
    ASSERT_EQ(SSL_CTX_use_PrivateKey(server_ctx.get(), key.get()), 1);
    ASSERT_EQ(SSL_CTX_check_private_key(server_ctx.get()), 1);

    // The helper must report success (no-op on non-EC key).
    EXPECT_TRUE(openssl::pin_sigalgs_to_cert_curve(server_ctx.get()));

    // Handshake with an RSA-only client sigalgs list; would alert out if the
    // helper had restricted the server to ECDSA-only.
    const HandshakeResult res = do_memory_handshake(server_ctx.get(), client_rsa_sigalgs);
    EXPECT_NE(res.hash_nid, 0) << "handshake failed - helper may have narrowed sigalgs for RSA leaf";
    // The signature-type NID must be an RSA variant (plain RSA or RSA-PSS),
    // never an EC one. That directly demonstrates the pin helper didn't
    // narrow the sigalgs to ECDSA when the leaf was RSA.
    EXPECT_NE(res.sig_type_nid, EVP_PKEY_EC);
    EXPECT_TRUE(res.sig_type_nid == EVP_PKEY_RSA || res.sig_type_nid == EVP_PKEY_RSA_PSS)
        << "unexpected sig type NID " << res.sig_type_nid;
}

} // namespace sigalg_pin_test

} // namespace
