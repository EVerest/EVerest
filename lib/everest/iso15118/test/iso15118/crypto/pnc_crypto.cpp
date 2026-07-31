// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
//
// Round-trip tests for the EVCC-side ISO 15118-2 Plug-and-Charge crypto: the xmldsig signer must produce
// signatures the (independently written) verifier accepts, and the contract-key decryption must recover a
// scalar encrypted with the ISO 15118-2 §7.9.2.4.3 scheme.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <iso15118/detail/d2/crypto.hpp>

using namespace iso15118;

namespace {

// A freshly generated secp256r1 key: private key PEM + self-signed leaf certificate DER (so the leaf
// public key matches the signing key, letting the verifier check the signature).
struct KeyAndCert {
    d2::crypto::PrivateKey key;
    std::vector<uint8_t> leaf_der;
    EVP_PKEY* pkey{nullptr}; // owned; freed by caller
};

EVP_PKEY* gen_p256() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1);
    EVP_PKEY_keygen(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

std::string pkey_to_pem(EVP_PKEY* pkey) {
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    char* data = nullptr;
    const long n = BIO_get_mem_data(bio, &data);
    std::string pem(data, static_cast<size_t>(n));
    BIO_free(bio);
    return pem;
}

KeyAndCert make_key_and_cert(const char* cn = "UKSWI123456789A") {
    KeyAndCert out;
    out.pkey = gen_p256();
    out.key.pem = pkey_to_pem(out.pkey);

    X509* x = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
    X509_gmtime_adj(X509_getm_notBefore(x), 0);
    X509_gmtime_adj(X509_getm_notAfter(x), 3600);
    X509_set_pubkey(x, out.pkey);
    X509_NAME* name = X509_get_subject_name(x);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(cn), -1, -1, 0);
    X509_set_issuer_name(x, name);
    X509_sign(x, out.pkey, EVP_sha256());

    unsigned char* der = nullptr;
    const int len = i2d_X509(x, &der);
    out.leaf_der.assign(der, der + len);
    OPENSSL_free(der);
    X509_free(x);
    return out;
}

} // namespace

SCENARIO("EVCC signs an AuthorizationReq the SECC verifier accepts") {
    auto kc = make_key_and_cert();

    GIVEN("A signed AuthorizationReq") {
        message_2::AuthorizationRequest req;
        req.id = "id1";
        req.gen_challenge = message_2::datatypes::GenChallenge{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        const auto exi = d2::crypto::serialize_signed(req, kc.key);

        THEN("The EXI is non-empty and the signature verifies against the contract leaf") {
            REQUIRE(not exi.empty());
            REQUIRE(d2::crypto::verify_authorization_signature(exi, kc.leaf_der));
        }

        THEN("Verification fails against an unrelated certificate") {
            auto other = make_key_and_cert("UKSWI000000000B");
            REQUIRE_FALSE(d2::crypto::verify_authorization_signature(exi, other.leaf_der));
            EVP_PKEY_free(other.pkey);
        }
    }

    EVP_PKEY_free(kc.pkey);
}

SCENARIO("EVCC recovers a contract private key encrypted per ISO 15118-2 7.9.2.4.3") {
    // OEM provisioning key = the static ECDH receiver key (kept by the EV).
    auto oem = make_key_and_cert();
    // The 32-byte contract private scalar the backend delivers, encrypted.
    std::vector<uint8_t> scalar(32);
    for (size_t i = 0; i < scalar.size(); ++i) {
        scalar[i] = static_cast<uint8_t>(i + 1);
    }

    // --- Encrypt side (mirrors the test CSMS EXIGenerator): ephemeral ECDH against the OEM public key,
    // ConcatKDF-SHA256 (OtherInfo 0x01 0x55 0x56, 16 bytes), AES-128-CBC with a 16-byte IV prepended.
    EVP_PKEY* ephemeral = gen_p256();

    // dh_public_key = uncompressed point of the ephemeral public key.
    std::vector<uint8_t> dh_pub(65);
    {
        size_t len = dh_pub.size();
        EVP_PKEY_get_octet_string_param(ephemeral, "encoded-pub-key", dh_pub.data(), dh_pub.size(), &len);
        dh_pub.resize(len);
    }

    // shared secret Z = ECDH(ephemeral_priv, oem_pub).
    std::vector<uint8_t> z;
    {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(ephemeral, nullptr);
        EVP_PKEY_derive_init(ctx);
        EVP_PKEY_derive_set_peer(ctx, oem.pkey);
        size_t zl = 0;
        EVP_PKEY_derive(ctx, nullptr, &zl);
        z.resize(zl);
        EVP_PKEY_derive(ctx, z.data(), &zl);
        z.resize(zl);
        EVP_PKEY_CTX_free(ctx);
    }

    // ConcatKDF-SHA256 single block.
    std::array<uint8_t, 32> hash{};
    {
        std::vector<uint8_t> input{0x00, 0x00, 0x00, 0x01};
        input.insert(input.end(), z.begin(), z.end());
        const uint8_t other[3] = {0x01, 0x55, 0x56};
        input.insert(input.end(), other, other + 3);
        unsigned int ml = 0;
        EVP_Digest(input.data(), input.size(), hash.data(), &ml, EVP_sha256(), nullptr);
    }
    std::array<uint8_t, 16> session_key{};
    std::copy(hash.begin(), hash.begin() + 16, session_key.begin());

    // AES-128-CBC encrypt, IV prepended.
    std::vector<uint8_t> encrypted(16);
    for (auto& b : encrypted) {
        b = 0x42; // deterministic IV for the test
    }
    {
        std::vector<uint8_t> ct(scalar.size() + 16);
        int out_len = 0;
        int fin = 0;
        EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(c, EVP_aes_128_cbc(), nullptr, session_key.data(), encrypted.data());
        EVP_CIPHER_CTX_set_padding(c, 0);
        EVP_EncryptUpdate(c, ct.data(), &out_len, scalar.data(), static_cast<int>(scalar.size()));
        EVP_EncryptFinal_ex(c, ct.data() + out_len, &fin);
        EVP_CIPHER_CTX_free(c);
        ct.resize(out_len + fin);
        encrypted.insert(encrypted.end(), ct.begin(), ct.end());
    }

    WHEN("The EVCC decrypts with the OEM provisioning private key") {
        const auto recovered = d2::crypto::decrypt_contract_private_key(encrypted, dh_pub, oem.key);
        THEN("It recovers the original 32-byte scalar") {
            REQUIRE(recovered == scalar);
        }
        THEN("The scalar re-serializes to a usable PEM EC private key") {
            REQUIRE_FALSE(d2::crypto::contract_scalar_to_pem(recovered).empty());
        }
    }

    EVP_PKEY_free(ephemeral);
    EVP_PKEY_free(oem.pkey);
}
