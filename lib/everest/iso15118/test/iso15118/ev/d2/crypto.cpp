// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// EVCC-side ISO 15118-2 Plug & Charge crypto: the xmldsig signer must produce a signature that verifies
// against the signing key over the SignedInfo it emitted, and the contract-key decryption must recover a
// scalar encrypted with the ISO 15118-2 7.9.2.4.3 scheme.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefDecoder.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

#include <everest/tls/openssl_util.hpp>

#include <iso15118/ev/detail/d2/crypto.hpp>

#include "helper.hpp"

using namespace iso15118;

namespace {

EVP_PKEY* gen_p256() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1);
    EVP_PKEY_keygen(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

EVP_PKEY* load_key(const std::string& pem) {
    return openssl::pem_to_private_key(pem, nullptr).release();
}

// Independent re-implementation of the SignedInfo digest the signer covers: encode the xmldsig fragment
// with the optional fields cleared per [V2G2-771], then SHA-256 it.
openssl::sha_256_digest_t signed_info_digest(const iso2_SignatureType& signature) {
    auto fragment = std::make_unique<iso2_xmldsigFragment>();
    init_iso2_xmldsigFragment(fragment.get());
    fragment->SignedInfo_isUsed = 1;
    fragment->SignedInfo = signature.SignedInfo;
    fragment->SignedInfo.Id_isUsed = 0;
    fragment->SignedInfo.CanonicalizationMethod.ANY_isUsed = 0;
    fragment->SignedInfo.SignatureMethod.HMACOutputLength_isUsed = 0;
    fragment->SignedInfo.SignatureMethod.ANY_isUsed = 0;
    for (uint16_t i = 0; i < fragment->SignedInfo.Reference.arrayLen; ++i) {
        auto& ref = fragment->SignedInfo.Reference.array[i];
        ref.Type_isUsed = 0;
        ref.Transforms.Transform.ANY_isUsed = 0;
        ref.Transforms.Transform.XPath_isUsed = 0;
        ref.DigestMethod.ANY_isUsed = 0;
    }

    std::array<uint8_t, 8192> buffer{};
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, buffer.data(), buffer.size(), 0, nullptr);
    REQUIRE(encode_iso2_xmldsigFragment(&stream, fragment.get()) == 0);

    openssl::sha_256_digest_t digest{};
    REQUIRE(openssl::sha_256(buffer.data(), exi_bitstream_get_length(&stream), digest));
    return digest;
}

// ECDSA-P256 verify of the raw r||s the signer wrote into SignatureValue.
bool verify_rs(EVP_PKEY* pkey, const uint8_t* rs, size_t rs_len, const openssl::sha_256_digest_t& digest) {
    return rs_len == 2 * openssl::signature_n_size and
           openssl::verify(pkey, rs, rs + openssl::signature_n_size, digest);
}

} // namespace

SCENARIO("EVCC signs an AuthorizationReq with a verifiable ECDSA signature") {
    const auto pem = make_test_ec_key_pem();
    const ev::d2::crypto::PrivateKey key{pem, std::nullopt};

    message_2::AuthorizationRequest req;
    req.header.session_id = D2_SESSION_ID;
    req.id = "id1";
    req.gen_challenge = message_2::datatypes::GenChallenge{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    const auto exi = ev::d2::crypto::serialize_signed(req, key);
    REQUIRE_FALSE(exi.empty());

    exi_bitstream_t stream;
    exi_bitstream_init(&stream, const_cast<uint8_t*>(exi.data()), exi.size(), 0, nullptr);
    auto doc = std::make_unique<iso2_exiDocument>();
    REQUIRE(decode_iso2_exiDocument(&stream, doc.get()) == 0);

    THEN("The signature references the signed element by its Id") {
        REQUIRE(doc->V2G_Message.Header.Signature_isUsed == 1);
        REQUIRE(doc->V2G_Message.Body.AuthorizationReq_isUsed == 1);
        const auto& ref = doc->V2G_Message.Header.Signature.SignedInfo.Reference.array[0];
        REQUIRE(std::string(ref.URI.characters, ref.URI.charactersLen) == "#id1");
        REQUIRE(ref.DigestValue.bytesLen == 32);
    }

    THEN("The SignatureValue verifies against the signing key") {
        EVP_PKEY* pkey = load_key(pem);
        REQUIRE(pkey != nullptr);
        const auto digest = signed_info_digest(doc->V2G_Message.Header.Signature);
        const auto& value = doc->V2G_Message.Header.Signature.SignatureValue.CONTENT;
        REQUIRE(verify_rs(pkey, value.bytes, value.bytesLen, digest));
        EVP_PKEY_free(pkey);
    }

    THEN("It verifies no longer against an unrelated key") {
        EVP_PKEY* other = gen_p256();
        const auto digest = signed_info_digest(doc->V2G_Message.Header.Signature);
        const auto& value = doc->V2G_Message.Header.Signature.SignatureValue.CONTENT;
        REQUIRE_FALSE(verify_rs(other, value.bytes, value.bytesLen, digest));
        EVP_PKEY_free(other);
    }
}

SCENARIO("EVCC signing fails on an unusable key") {
    message_2::MeteringReceiptRequest req;
    req.header.session_id = D2_SESSION_ID;
    req.session_id = D2_SESSION_ID;
    req.meter_info.meter_id = "METER1";
    REQUIRE(ev::d2::crypto::serialize_signed(req, ev::d2::crypto::PrivateKey{"not a key", std::nullopt}).empty());
}

SCENARIO("EVCC recovers a contract private key encrypted per ISO 15118-2 7.9.2.4.3") {
    const auto oem_pem = make_test_ec_key_pem();
    EVP_PKEY* oem = load_key(oem_pem);
    REQUIRE(oem != nullptr);

    std::vector<uint8_t> scalar(32);
    for (size_t i = 0; i < scalar.size(); ++i) {
        scalar[i] = static_cast<uint8_t>(i + 1);
    }

    // Encrypt side: ephemeral ECDH against the OEM public key, ConcatKDF-SHA256
    // (OtherInfo 0x01 0x55 0x56, 16 bytes), AES-128-CBC with a 16-byte IV prepended.
    EVP_PKEY* ephemeral = gen_p256();

    std::vector<uint8_t> dh_pub(65);
    {
        size_t len = dh_pub.size();
        REQUIRE(EVP_PKEY_get_octet_string_param(ephemeral, "encoded-pub-key", dh_pub.data(), dh_pub.size(), &len) == 1);
        dh_pub.resize(len);
    }

    std::vector<uint8_t> z;
    {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(ephemeral, nullptr);
        EVP_PKEY_derive_init(ctx);
        EVP_PKEY_derive_set_peer(ctx, oem);
        size_t zl = 0;
        EVP_PKEY_derive(ctx, nullptr, &zl);
        z.resize(zl);
        EVP_PKEY_derive(ctx, z.data(), &zl);
        z.resize(zl);
        EVP_PKEY_CTX_free(ctx);
    }

    openssl::sha_256_digest_t hash{};
    {
        std::vector<uint8_t> input{0x00, 0x00, 0x00, 0x01};
        input.insert(input.end(), z.begin(), z.end());
        const uint8_t other[3] = {0x01, 0x55, 0x56};
        input.insert(input.end(), other, other + 3);
        REQUIRE(openssl::sha_256(input.data(), input.size(), hash));
    }

    std::vector<uint8_t> encrypted(16, 0x42); // deterministic IV
    {
        std::vector<uint8_t> ct(scalar.size() + 16);
        int out_len = 0;
        int fin = 0;
        EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(c, EVP_aes_128_cbc(), nullptr, hash.data(), encrypted.data());
        EVP_CIPHER_CTX_set_padding(c, 0);
        EVP_EncryptUpdate(c, ct.data(), &out_len, scalar.data(), static_cast<int>(scalar.size()));
        EVP_EncryptFinal_ex(c, ct.data() + out_len, &fin);
        EVP_CIPHER_CTX_free(c);
        ct.resize(static_cast<size_t>(out_len + fin));
        encrypted.insert(encrypted.end(), ct.begin(), ct.end());
    }

    const ev::d2::crypto::PrivateKey oem_key{oem_pem, std::nullopt};
    const auto recovered = ev::d2::crypto::decrypt_contract_private_key(encrypted, dh_pub, oem_key);

    THEN("It recovers the original 32-byte scalar") {
        REQUIRE(recovered == scalar);
    }
    THEN("The scalar re-serializes to a usable PEM EC private key") {
        const auto pem = ev::d2::crypto::contract_scalar_to_pem(recovered);
        REQUIRE_FALSE(pem.empty());
        EVP_PKEY* reloaded = load_key(pem);
        REQUIRE(reloaded != nullptr);
        EVP_PKEY_free(reloaded);
    }
    THEN("A malformed ciphertext is rejected") {
        REQUIRE(ev::d2::crypto::decrypt_contract_private_key({0x01}, dh_pub, oem_key).empty());
    }

    EVP_PKEY_free(ephemeral);
    EVP_PKEY_free(oem);
}

SCENARIO("EVCC wipes key material in place") {
    std::vector<uint8_t> secret{0xDE, 0xAD, 0xBE, 0xEF};
    ev::d2::crypto::wipe(secret);
    REQUIRE(secret == std::vector<uint8_t>(4, 0x00));
}

namespace {

// A self-signed CA (`issuer_key == nullptr`) or a leaf signed by `issuer`.
X509* make_cert(EVP_PKEY* subject_key, const char* cn, X509* issuer, EVP_PKEY* issuer_key) {
    X509* cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 4711);
    X509_gmtime_adj(X509_getm_notBefore(cert), -3600);
    X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
    X509_set_pubkey(cert, subject_key);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(cn), -1, -1, 0);
    X509_set_subject_name(cert, name);
    X509_set_issuer_name(cert, (issuer != nullptr) ? X509_get_subject_name(issuer) : name);

    const char* constraints = (issuer == nullptr) ? "critical,CA:TRUE" : "critical,CA:FALSE";
    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, nullptr, NID_basic_constraints, constraints);
    X509_add_ext(cert, ext, -1);
    X509_EXTENSION_free(ext);

    X509_sign(cert, (issuer_key != nullptr) ? issuer_key : subject_key, EVP_sha256());
    return cert;
}

// Replace the serial of an already signed certificate and re-sign it.
void set_serial(X509* cert, EVP_PKEY* key, const std::vector<uint8_t>& big_endian_magnitude) {
    BIGNUM* bn = BN_bin2bn(big_endian_magnitude.data(), static_cast<int>(big_endian_magnitude.size()), nullptr);
    BN_to_ASN1_INTEGER(bn, X509_get_serialNumber(cert));
    BN_free(bn);
    X509_sign(cert, key, EVP_sha256());
}

std::vector<uint8_t> cert_der(X509* cert) {
    const auto der = openssl::certificate_to_der(cert);
    return std::vector<uint8_t>(der.get(), der.get() + der.size());
}

std::string cert_pem(X509* cert) {
    return openssl::certificate_to_pem(cert);
}

openssl::sha_256_digest_t sha256_of(const uint8_t* data, size_t len) {
    openssl::sha_256_digest_t out{};
    REQUIRE(openssl::sha_256(data, len, out));
    return out;
}

// SHA-256 over the EXI fragment of the single element `fill` sets.
template <typename Fill> std::array<uint8_t, 32> fragment_digest(Fill fill) {
    auto fragment = std::make_unique<iso2_exiFragment>();
    init_iso2_exiFragment(fragment.get());
    fill(*fragment);
    std::array<uint8_t, 8192> buffer{};
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, buffer.data(), buffer.size(), 0, nullptr);
    REQUIRE(encode_iso2_exiFragment(&stream, fragment.get()) == 0);
    return sha256_of(buffer.data(), exi_bitstream_get_length(&stream));
}

template <typename Field> void set_chars(Field& field, const std::string& value) {
    std::memcpy(field.characters, value.data(), value.size());
    field.charactersLen = static_cast<uint16_t>(value.size());
}

template <typename Field> void set_bytes(Field& field, const std::vector<uint8_t>& value) {
    std::memcpy(field.bytes, value.data(), value.size());
    field.bytesLen = static_cast<uint16_t>(value.size());
}

// A CertificateInstallationRes carrying the four signed elements, ready to be signed.
void fill_res(iso2_exiDocument& doc, const std::vector<uint8_t>& sa_leaf_der, const std::vector<uint8_t>& contract_der,
              const std::string& emaid) {
    doc.V2G_Message.Header.SessionID.bytesLen = 8;
    doc.V2G_Message.Body.CertificateInstallationRes_isUsed = 1;

    auto& res = doc.V2G_Message.Body.CertificateInstallationRes;
    res.ResponseCode = iso2_responseCodeType_OK;
    set_bytes(res.SAProvisioningCertificateChain.Certificate, sa_leaf_der);

    res.ContractSignatureCertChain.Id_isUsed = 1;
    set_chars(res.ContractSignatureCertChain.Id, "id1");
    set_bytes(res.ContractSignatureCertChain.Certificate, contract_der);

    set_chars(res.ContractSignatureEncryptedPrivateKey.Id, "id2");
    set_bytes(res.ContractSignatureEncryptedPrivateKey.CONTENT, std::vector<uint8_t>(48, 0xAB));

    set_chars(res.DHpublickey.Id, "id3");
    set_bytes(res.DHpublickey.CONTENT, std::vector<uint8_t>(65, 0xCD));

    set_chars(res.eMAID.Id, "id4");
    set_chars(res.eMAID.CONTENT, emaid);
}

// The fragment models eMAID with the schema-informed element fragment grammar, so its member is a
// distinct generated type from the message's EMAIDType and the two character fields are copied
// across. The asserts fire if either buffer stops matching, rather than truncating in silence.
iso2_eMAIDElementFragment as_fragment(const iso2_EMAIDType& emaid) {
    iso2_eMAIDElementFragment fragment{};
    static_assert(sizeof(fragment.Id.characters) == sizeof(emaid.Id.characters));
    static_assert(sizeof(fragment.CONTENT.characters) == sizeof(emaid.CONTENT.characters));
    fragment.Id_isUsed = 1;
    fragment.Id.charactersLen = emaid.Id.charactersLen;
    std::memcpy(fragment.Id.characters, emaid.Id.characters, sizeof(fragment.Id.characters));
    fragment.CONTENT_isUsed = 1;
    fragment.CONTENT.charactersLen = emaid.CONTENT.charactersLen;
    std::memcpy(fragment.CONTENT.characters, emaid.CONTENT.characters, sizeof(fragment.CONTENT.characters));
    return fragment;
}

// The CPS half: four References over the current element contents, ECDSA-signed with `leaf_key`.
void sign_res(iso2_exiDocument& doc, EVP_PKEY* leaf_key) {
    const auto& res = doc.V2G_Message.Body.CertificateInstallationRes;
    const auto chain_digest = fragment_digest([&res](iso2_exiFragment& f) {
        f.ContractSignatureCertChain_isUsed = 1;
        f.ContractSignatureCertChain = res.ContractSignatureCertChain;
    });
    const auto key_digest = fragment_digest([&res](iso2_exiFragment& f) {
        f.ContractSignatureEncryptedPrivateKey_isUsed = 1;
        f.ContractSignatureEncryptedPrivateKey = res.ContractSignatureEncryptedPrivateKey;
    });
    const auto dh_digest = fragment_digest([&res](iso2_exiFragment& f) {
        f.DHpublickey_isUsed = 1;
        f.DHpublickey = res.DHpublickey;
    });
    const auto emaid_digest = fragment_digest([&res](iso2_exiFragment& f) {
        f.eMAID_isUsed = 1;
        f.eMAID = as_fragment(res.eMAID);
    });
    const std::array<std::array<uint8_t, 32>, 4> digests{chain_digest, key_digest, dh_digest, emaid_digest};
    const std::array<const char*, 4> uris{"#id1", "#id2", "#id3", "#id4"};

    auto& signature = doc.V2G_Message.Header.Signature;
    init_iso2_SignatureType(&signature);
    auto& signed_info = signature.SignedInfo;
    set_chars(signed_info.CanonicalizationMethod.Algorithm, "http://www.w3.org/TR/canonical-exi/");
    set_chars(signed_info.SignatureMethod.Algorithm, "http://www.w3.org/2001/04/xmldsig-more#ecdsa-sha256");
    signed_info.Reference.arrayLen = 4;
    for (size_t i = 0; i < 4; ++i) {
        auto& ref = signed_info.Reference.array[i];
        set_chars(ref.URI, uris.at(i));
        ref.URI_isUsed = 1;
        ref.Transforms_isUsed = 1;
        set_chars(ref.Transforms.Transform.Algorithm, "http://www.w3.org/TR/canonical-exi/");
        set_chars(ref.DigestMethod.Algorithm, "http://www.w3.org/2001/04/xmlenc#sha256");
        std::memcpy(ref.DigestValue.bytes, digests.at(i).data(), digests.at(i).size());
        ref.DigestValue.bytesLen = 32;
    }
    doc.V2G_Message.Header.Signature_isUsed = 1;

    const auto si_digest = signed_info_digest(signature);
    openssl::bn_t r{};
    openssl::bn_t s{};
    REQUIRE(openssl::sign(leaf_key, r, s, si_digest));
    std::memcpy(signature.SignatureValue.CONTENT.bytes, r.data(), r.size());
    std::memcpy(signature.SignatureValue.CONTENT.bytes + r.size(), s.data(), s.size());
    signature.SignatureValue.CONTENT.bytesLen = 64;
}

std::vector<uint8_t> encode_doc(iso2_exiDocument& doc) {
    std::array<uint8_t, 8192> buffer{};
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, buffer.data(), buffer.size(), 0, nullptr);
    REQUIRE(encode_iso2_exiDocument(&stream, &doc) == 0);
    return std::vector<uint8_t>(buffer.data(), buffer.data() + exi_bitstream_get_length(&stream));
}

} // namespace

SCENARIO("EVCC verifies the CPS signature over the four CertificateInstallationRes elements") {
    EVP_PKEY* root_key = gen_p256();
    EVP_PKEY* leaf_key = gen_p256();
    EVP_PKEY* contract_key = gen_p256();
    X509* root = make_cert(root_key, "Test V2G Root", nullptr, nullptr);
    X509* leaf = make_cert(leaf_key, "Test CPS Leaf", root, root_key);
    X509* contract = make_cert(contract_key, "DE1234567890", root, root_key);

    const auto root_path = std::filesystem::temp_directory_path() / "ev_d2_crypto_v2g_root.pem";
    {
        std::ofstream out(root_path);
        out << cert_pem(root);
    }

    const auto sa_leaf_der = cert_der(leaf);
    const auto contract_der = cert_der(contract);

    auto doc = std::make_unique<iso2_exiDocument>();
    fill_res(*doc, sa_leaf_der, contract_der, "DE1234567890");
    sign_res(*doc, leaf_key);
    const auto exi = encode_doc(*doc);

    THEN("A correctly signed response verifies against the trusted V2G root") {
        REQUIRE(ev::d2::crypto::verify_certificate_installation_res(exi, root_path.string()));
    }

    THEN("A response whose eMAID was swapped after signing is rejected") {
        auto tampered = std::make_unique<iso2_exiDocument>(*doc);
        set_chars(tampered->V2G_Message.Body.CertificateInstallationRes.eMAID.CONTENT, "DE0000000000");
        REQUIRE_FALSE(ev::d2::crypto::verify_certificate_installation_res(encode_doc(*tampered), root_path.string()));
    }

    THEN("A response whose contract chain was swapped after signing is rejected") {
        EVP_PKEY* other_key = gen_p256();
        X509* other = make_cert(other_key, "DE9999999999", root, root_key);
        auto tampered = std::make_unique<iso2_exiDocument>(*doc);
        set_bytes(tampered->V2G_Message.Body.CertificateInstallationRes.ContractSignatureCertChain.Certificate,
                  cert_der(other));
        REQUIRE_FALSE(ev::d2::crypto::verify_certificate_installation_res(encode_doc(*tampered), root_path.string()));
        X509_free(other);
        EVP_PKEY_free(other_key);
    }

    THEN("A response signed by a leaf outside the trusted root is rejected") {
        EVP_PKEY* rogue_root_key = gen_p256();
        EVP_PKEY* rogue_leaf_key = gen_p256();
        X509* rogue_root = make_cert(rogue_root_key, "Rogue Root", nullptr, nullptr);
        X509* rogue_leaf = make_cert(rogue_leaf_key, "Rogue Leaf", rogue_root, rogue_root_key);

        auto rogue = std::make_unique<iso2_exiDocument>();
        fill_res(*rogue, cert_der(rogue_leaf), contract_der, "DE1234567890");
        sign_res(*rogue, rogue_leaf_key);
        REQUIRE_FALSE(ev::d2::crypto::verify_certificate_installation_res(encode_doc(*rogue), root_path.string()));

        X509_free(rogue_leaf);
        X509_free(rogue_root);
        EVP_PKEY_free(rogue_leaf_key);
        EVP_PKEY_free(rogue_root_key);
    }

    THEN("The root certificate id carries the RFC 2253 issuer name") {
        const auto id = ev::d2::crypto::root_cert_id_from_der(cert_der(root));
        REQUIRE(id.issuer_name == "CN=Test V2G Root");
        REQUIRE(id.serial_number == std::vector<uint8_t>{0x12, 0x67}); // 4711
    }

    THEN("A 20-octet serial, as production CAs emit, survives whole") {
        EVP_PKEY* wide_key = gen_p256();
        X509* wide_root = make_cert(wide_key, "Wide Serial Root", nullptr, nullptr);

        const std::vector<uint8_t> serial_bytes(20, 0x5A);
        set_serial(wide_root, wide_key, serial_bytes);

        const auto id = ev::d2::crypto::root_cert_id_from_der(cert_der(wide_root));
        REQUIRE(id.issuer_name == "CN=Wide Serial Root");
        REQUIRE(id.serial_number == serial_bytes);

        X509_free(wide_root);
        EVP_PKEY_free(wide_key);
    }

    THEN("A serial wider than the EXI converter takes yields no id at all") {
        // 26 bytes need 30 EXI octets and the converter writes them into a 29-octet buffer without
        // checking. The caller keeps an id only when issuer_name is non-empty, so reporting nothing
        // is what makes it warn.
        EVP_PKEY* wide_key = gen_p256();
        X509* wide_root = make_cert(wide_key, "Too Wide Serial Root", nullptr, nullptr);

        set_serial(wide_root, wide_key, std::vector<uint8_t>(message_2::MAX_SERIAL_NUMBER_BYTES + 1, 0x5A));

        const auto id = ev::d2::crypto::root_cert_id_from_der(cert_der(wide_root));
        REQUIRE(id.issuer_name.empty());
        REQUIRE(id.serial_number.empty());

        X509_free(wide_root);
        EVP_PKEY_free(wide_key);
    }

    THEN("A negative serial yields no id at all") {
        EVP_PKEY* negative_key = gen_p256();
        X509* negative_root = make_cert(negative_key, "Negative Serial Root", nullptr, nullptr);
        ASN1_INTEGER_set(X509_get_serialNumber(negative_root), -1);
        X509_sign(negative_root, negative_key, EVP_sha256());

        const auto id = ev::d2::crypto::root_cert_id_from_der(cert_der(negative_root));
        REQUIRE(id.issuer_name.empty());
        REQUIRE(id.serial_number.empty());

        X509_free(negative_root);
        EVP_PKEY_free(negative_key);
    }

    std::filesystem::remove(root_path);
    X509_free(contract);
    X509_free(leaf);
    X509_free(root);
    EVP_PKEY_free(contract_key);
    EVP_PKEY_free(leaf_key);
    EVP_PKEY_free(root_key);
}

SCENARIO("EVCC signing fails instead of overrunning a fixed EXI string field") {
    const auto pem = make_test_ec_key_pem();
    message_2::AuthorizationRequest req;
    req.header.session_id = D2_SESSION_ID;
    // The Id fills the 64-character field exactly, so the "#"+Id reference URI no longer fits.
    req.id = std::string(64, 'x');
    REQUIRE(ev::d2::crypto::serialize_signed(req, ev::d2::crypto::PrivateKey{pem, std::nullopt}).empty());
}
