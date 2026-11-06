// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
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
#include <utility>
#include <vector>

#include <unistd.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefDecoder.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

#include <everest/tls/openssl_util.hpp>

#include <iso15118/detail/d2/crypto.hpp>

#ifdef ISO15118_TEST_EV_SIDE
#include "../ev/d2/helper.hpp"
#include <iso15118/ev/d2/state/certificate_installation.hpp>
#include <iso15118/ev/d2/state/payment_service_selection.hpp>
#include <iso15118/ev/detail/crypto.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#else
#include "../test_pki.hpp"
inline constexpr iso15118::message_2::datatypes::SessionId D2_SESSION_ID{0x10, 0x34, 0xAB, 0x7A,
                                                                         0x01, 0xF3, 0x95, 0x02};
std::string make_test_ec_key_pem() {
    auto key = iso15118::test_pki::generate_key("prime256v1");
    return iso15118::test_pki::key_to_pem(key.get());
}
#endif

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

// The SECC half of ISO 15118-2 7.9.2.4.3: ephemeral ECDH against the OEM public key, ConcatKDF-SHA256
// (OtherInfo 0x01 0x55 0x56, 16 bytes), AES-128-CBC with a 16-byte IV prepended. Returns the encrypted
// key and the ephemeral DHpublickey.
std::pair<std::vector<uint8_t>, std::vector<uint8_t>> encrypt_contract_scalar(EVP_PKEY* oem,
                                                                              const std::vector<uint8_t>& scalar) {
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
    EVP_PKEY_free(ephemeral);

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
    return {encrypted, dh_pub};
}

} // namespace

SCENARIO("EVCC signs an AuthorizationReq with a verifiable ECDSA signature") {
    const auto pem = make_test_ec_key_pem();
    const d2::crypto::PrivateKey key{pem, std::nullopt};

    message_2::AuthorizationRequest req;
    req.header.session_id = D2_SESSION_ID;
    req.id = "id1";
    req.gen_challenge = message_2::datatypes::GenChallenge{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    const auto exi = d2::crypto::serialize_signed(req, key);
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
    REQUIRE(d2::crypto::serialize_signed(req, d2::crypto::PrivateKey{"not a key", std::nullopt}).empty());
}

SCENARIO("EVCC recovers a contract private key encrypted per ISO 15118-2 7.9.2.4.3") {
    const auto oem_pem = make_test_ec_key_pem();
    EVP_PKEY* oem = load_key(oem_pem);
    REQUIRE(oem != nullptr);

    std::vector<uint8_t> scalar(32);
    for (size_t i = 0; i < scalar.size(); ++i) {
        scalar[i] = static_cast<uint8_t>(i + 1);
    }

    const auto [encrypted, dh_pub] = encrypt_contract_scalar(oem, scalar);

    const d2::crypto::PrivateKey oem_key{oem_pem, std::nullopt};
    const auto recovered = d2::crypto::decrypt_contract_private_key(encrypted, dh_pub, oem_key);

    THEN("It recovers the original 32-byte scalar") {
        REQUIRE(recovered == scalar);
    }
    THEN("The scalar re-serializes to a usable PEM EC private key") {
        const auto pem = d2::crypto::contract_scalar_to_pem(recovered);
        REQUIRE_FALSE(pem.empty());
        EVP_PKEY* reloaded = load_key(pem);
        REQUIRE(reloaded != nullptr);
        EVP_PKEY_free(reloaded);
    }
    THEN("A malformed ciphertext is rejected") {
        REQUIRE(d2::crypto::decrypt_contract_private_key({0x01}, dh_pub, oem_key).empty());
    }

    EVP_PKEY_free(oem);
}

SCENARIO("EVCC wipes key material in place") {
    std::vector<uint8_t> secret{0xDE, 0xAD, 0xBE, 0xEF};
    d2::crypto::wipe(secret);
    REQUIRE(secret == std::vector<uint8_t>(4, 0x00));
}

namespace {

// A self-signed CA (`issuer_key == nullptr`) or a leaf signed by `issuer`, with a DomainComponent RDN when
// `dc` is set.
X509* make_cert(EVP_PKEY* subject_key, const char* cn, X509* issuer, EVP_PKEY* issuer_key, const char* dc = nullptr) {
    X509* cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 4711);
    X509_gmtime_adj(X509_getm_notBefore(cert), -3600);
    X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
    X509_set_pubkey(cert, subject_key);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(cn), -1, -1, 0);
    if (dc != nullptr) {
        X509_NAME_add_entry_by_txt(name, "DC", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(dc), -1, -1, 0);
    }
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

SCENARIO("ISO 15118-2 signed requests round trip through receiver verification") {
    const auto pem = make_test_ec_key_pem();
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{load_key(pem), EVP_PKEY_free};
    std::unique_ptr<X509, decltype(&X509_free)> cert{make_cert(key.get(), "Test OEM", nullptr, nullptr), X509_free};
    const d2::crypto::PrivateKey private_key{pem, std::nullopt};

    SECTION("AuthorizationReq uses the SECC verifier") {
        message_2::AuthorizationRequest req;
        req.header.session_id = D2_SESSION_ID;
        req.gen_challenge = message_2::datatypes::GenChallenge{1, 2, 3};
        const auto exi = d2::crypto::serialize_signed(req, private_key);
        REQUIRE(d2::crypto::verify_authorization_signature(exi, cert_der(cert.get())));
    }
    SECTION("MeteringReceiptReq uses the SECC verifier") {
        message_2::MeteringReceiptRequest req;
        req.header.session_id = D2_SESSION_ID;
        req.session_id = D2_SESSION_ID;
        req.meter_info.meter_id = "METER1";
        const auto exi = d2::crypto::serialize_signed(req, private_key);
        REQUIRE(d2::crypto::verify_metering_receipt_signature(exi, cert_der(cert.get())));
    }
    SECTION("CertificateInstallationReq verifies its reference digest and OEM signature") {
        message_2::CertificateInstallationRequest req;
        req.header.session_id = D2_SESSION_ID;
        req.id = "id1";
        req.oem_provisioning_cert = cert_der(cert.get());
        req.root_certificate_ids = {{"CN=Test OEM", {1}}};
        const auto exi = d2::crypto::serialize_signed(req, private_key);
        REQUIRE_FALSE(exi.empty());
        exi_bitstream_t stream;
        exi_bitstream_init(&stream, const_cast<uint8_t*>(exi.data()), exi.size(), 0, nullptr);
        auto doc = std::make_unique<iso2_exiDocument>();
        REQUIRE(decode_iso2_exiDocument(&stream, doc.get()) == 0);
        REQUIRE(doc->V2G_Message.Body.CertificateInstallationReq_isUsed);
        const auto& signature = doc->V2G_Message.Header.Signature;
        const auto digest = fragment_digest([&](iso2_exiFragment& f) {
            f.CertificateInstallationReq_isUsed = 1;
            f.CertificateInstallationReq = doc->V2G_Message.Body.CertificateInstallationReq;
        });
        const auto& ref = signature.SignedInfo.Reference.array[0];
        REQUIRE(std::string(ref.URI.characters, ref.URI.charactersLen) == "#id1");
        REQUIRE(ref.DigestValue.bytesLen == digest.size());
        REQUIRE(std::memcmp(ref.DigestValue.bytes, digest.data(), digest.size()) == 0);
        const auto& value = signature.SignatureValue.CONTENT;
        REQUIRE(verify_rs(key.get(), value.bytes, value.bytesLen, signed_info_digest(signature)));
    }
}

SCENARIO("A root certificate missing its mandatory serial yields no root ID") {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{gen_p256(), EVP_PKEY_free};
    std::unique_ptr<X509, decltype(&X509_free)> root{make_cert(key.get(), "Root", nullptr, nullptr), X509_free};
    const auto der = cert_der(root.get());
    const unsigned char* cursor = der.data();
    long length = 0;
    int tag = 0;
    int cls = 0;
    const auto read = [&] {
        REQUIRE((ASN1_get_object(&cursor, &length, &tag, &cls, der.data() + der.size() - cursor) & 0x80) == 0);
    };
    read(); // Certificate SEQUENCE
    REQUIRE(tag == V_ASN1_SEQUENCE);
    read(); // TBSCertificate SEQUENCE
    REQUIRE(tag == V_ASN1_SEQUENCE);
    const auto* tbs = cursor;
    const auto* tbs_end = tbs + length;
    read(); // explicit version
    REQUIRE(cls == V_ASN1_CONTEXT_SPECIFIC);
    cursor += length;
    const auto* serial = cursor;
    read();
    REQUIRE(tag == V_ASN1_INTEGER);
    cursor += length;
    const auto* after_serial = cursor;
    const auto sequence = [](const std::vector<uint8_t>& content) {
        std::vector<uint8_t> out(ASN1_object_size(1, content.size(), V_ASN1_SEQUENCE));
        auto* dest = out.data();
        ASN1_put_object(&dest, 1, content.size(), V_ASN1_SEQUENCE, V_ASN1_UNIVERSAL);
        std::memcpy(dest, content.data(), content.size());
        return out;
    };
    std::vector<uint8_t> body(tbs, serial);
    body.insert(body.end(), after_serial, tbs_end);
    auto certificate = sequence(body);
    certificate.insert(certificate.end(), tbs_end, der.data() + der.size());
    const auto id = d2::crypto::root_cert_id_from_der(sequence(certificate));
    REQUIRE(id.issuer_name.empty());
    REQUIRE(id.serial_number.empty());
}

SCENARIO("Shared root IDs preserve RFC 2253 escaping and reject unusable serials") {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{gen_p256(), EVP_PKEY_free};
    std::unique_ptr<X509, decltype(&X509_free)> root{make_cert(key.get(), "V2G, Root", nullptr, nullptr), X509_free};
    std::vector<uint8_t> serial(20, 0x5A);
    SECTION("full serial") {
        set_serial(root.get(), key.get(), serial);
    }
    SECTION("zero serial") {
        serial.clear();
        set_serial(root.get(), key.get(), {0});
    }
    const auto id = d2::crypto::root_cert_id_from_der(cert_der(root.get()));
    REQUIRE(id.issuer_name == "CN=V2G\\, Root");
    REQUIRE(id.serial_number == serial);
    const auto missing = d2::crypto::root_cert_id_from_der({});
    REQUIRE(missing.issuer_name.empty());
    REQUIRE(missing.serial_number.empty());
}

SCENARIO("EVCC verifies the CPS signature over the four CertificateInstallationRes elements") {
    EVP_PKEY* root_key = gen_p256();
    EVP_PKEY* leaf_key = gen_p256();
    EVP_PKEY* contract_key = gen_p256();
    X509* root = make_cert(root_key, "Test V2G Root", nullptr, nullptr);
    X509* leaf = make_cert(leaf_key, "Test CPS Leaf", root, root_key, "CPS");
    X509* contract = make_cert(contract_key, "DE1234567890", root, root_key);

    auto root_path = std::filesystem::temp_directory_path() / ("d2-crypto-root-" + std::to_string(::getpid()) + ".pem");
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
        REQUIRE(d2::crypto::verify_certificate_installation_res(exi, root_path.string()));
    }

    THEN("A response whose eMAID was swapped after signing is rejected") {
        auto tampered = std::make_unique<iso2_exiDocument>(*doc);
        set_chars(tampered->V2G_Message.Body.CertificateInstallationRes.eMAID.CONTENT, "DE0000000000");
        REQUIRE_FALSE(d2::crypto::verify_certificate_installation_res(encode_doc(*tampered), root_path.string()));
    }

    THEN("A response whose contract chain was swapped after signing is rejected") {
        EVP_PKEY* other_key = gen_p256();
        X509* other = make_cert(other_key, "DE9999999999", root, root_key);
        auto tampered = std::make_unique<iso2_exiDocument>(*doc);
        set_bytes(tampered->V2G_Message.Body.CertificateInstallationRes.ContractSignatureCertChain.Certificate,
                  cert_der(other));
        REQUIRE_FALSE(d2::crypto::verify_certificate_installation_res(encode_doc(*tampered), root_path.string()));
        X509_free(other);
        EVP_PKEY_free(other_key);
    }

    THEN("Each referenced element rejects a one-byte change with SignedInfo unchanged") {
        for (int element = 0; element < 4; ++element) {
            auto tampered = std::make_unique<iso2_exiDocument>(*doc);
            auto& res = tampered->V2G_Message.Body.CertificateInstallationRes;
            switch (element) {
            case 0:
                res.ContractSignatureCertChain.Certificate.bytes[0] ^= 1;
                break;
            case 1:
                res.ContractSignatureEncryptedPrivateKey.CONTENT.bytes[0] ^= 1;
                break;
            case 2:
                res.DHpublickey.CONTENT.bytes[0] ^= 1;
                break;
            case 3:
                res.eMAID.CONTENT.characters[0] ^= 1;
                break;
            }
            REQUIRE(signed_info_digest(tampered->V2G_Message.Header.Signature) ==
                    signed_info_digest(doc->V2G_Message.Header.Signature));
            REQUIRE_FALSE(d2::crypto::verify_certificate_installation_res(encode_doc(*tampered), root_path.string()));
        }
    }

    THEN("A one-byte change in SignedInfo rejects an otherwise intact response") {
        auto tampered = std::make_unique<iso2_exiDocument>(*doc);
        tampered->V2G_Message.Header.Signature.SignedInfo.SignatureMethod.Algorithm.characters[0] ^= 1;
        REQUIRE_FALSE(d2::crypto::verify_certificate_installation_res(encode_doc(*tampered), root_path.string()));
    }

    THEN("A response signed by a leaf outside the trusted root is rejected") {
        EVP_PKEY* rogue_root_key = gen_p256();
        EVP_PKEY* rogue_leaf_key = gen_p256();
        X509* rogue_root = make_cert(rogue_root_key, "Rogue Root", nullptr, nullptr);
        X509* rogue_leaf = make_cert(rogue_leaf_key, "Rogue Leaf", rogue_root, rogue_root_key);

        auto rogue = std::make_unique<iso2_exiDocument>();
        fill_res(*rogue, cert_der(rogue_leaf), contract_der, "DE1234567890");
        sign_res(*rogue, rogue_leaf_key);
        REQUIRE_FALSE(d2::crypto::verify_certificate_installation_res(encode_doc(*rogue), root_path.string()));

        X509_free(rogue_leaf);
        X509_free(rogue_root);
        EVP_PKEY_free(rogue_leaf_key);
        EVP_PKEY_free(rogue_root_key);
    }

    THEN("The root certificate id carries the RFC 2253 issuer name") {
        const auto id = d2::crypto::root_cert_id_from_der(cert_der(root));
        REQUIRE(id.issuer_name == "CN=Test V2G Root");
        REQUIRE(id.serial_number == std::vector<uint8_t>{0x12, 0x67}); // 4711
    }

    THEN("A 20-octet serial, as production CAs emit, survives whole") {
        EVP_PKEY* wide_key = gen_p256();
        X509* wide_root = make_cert(wide_key, "Wide Serial Root", nullptr, nullptr);

        const std::vector<uint8_t> serial_bytes(20, 0x5A);
        set_serial(wide_root, wide_key, serial_bytes);

        const auto id = d2::crypto::root_cert_id_from_der(cert_der(wide_root));
        REQUIRE(id.issuer_name == "CN=Wide Serial Root");
        REQUIRE(id.serial_number == serial_bytes);

        X509_free(wide_root);
        EVP_PKEY_free(wide_key);
    }

    THEN("A serial wider than the EXI converter takes yields no id at all") {
        // 26 bytes need 30 EXI octets and the converter writes them into a 29-octet buffer without
        // checking.
        EVP_PKEY* wide_key = gen_p256();
        X509* wide_root = make_cert(wide_key, "Too Wide Serial Root", nullptr, nullptr);

        set_serial(wide_root, wide_key, std::vector<uint8_t>(message_2::MAX_SERIAL_NUMBER_BYTES + 1, 0x5A));

        const auto id = d2::crypto::root_cert_id_from_der(cert_der(wide_root));
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

        const auto id = d2::crypto::root_cert_id_from_der(cert_der(negative_root));
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
    const d2::crypto::PrivateKey key{make_test_ec_key_pem(), std::nullopt};
    SECTION("Authorization Id or its reference URI is too long") {
        message_2::AuthorizationRequest req;
        req.header.session_id = D2_SESSION_ID;
        REQUIRE_FALSE(d2::crypto::serialize_signed(req, key).empty());
        for (const auto length : {64, 65}) {
            req.id = std::string(length, 'x');
            REQUIRE(d2::crypto::serialize_signed(req, key).empty());
        }
    }
    SECTION("CertificateInstallation reference URI is too long") {
        message_2::CertificateInstallationRequest req;
        req.header.session_id = D2_SESSION_ID;
        req.id = "id1";
        req.oem_provisioning_cert = {1};
        req.root_certificate_ids = {{"CN=Root", {1}}};
        REQUIRE_FALSE(d2::crypto::serialize_signed(req, key).empty());
        for (const auto length : {64, 65}) {
            req.id = std::string(length, 'x');
            REQUIRE(d2::crypto::serialize_signed(req, key).empty());
        }
    }
}

#ifdef ISO15118_TEST_EV_SIDE
// CertificateInstallationReq is prepared on the PaymentServiceSelection transition: an unusable root or
// OEM key stops the session there, before anything is sent and without entering CertificateInstallation.
SCENARIO("EV CertificateInstallation advertises the configured root with its complete serial") {
    const auto key_pem = make_test_ec_key_pem();
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{load_key(key_pem), EVP_PKEY_free};
    std::unique_ptr<X509, decltype(&X509_free)> root{make_cert(key.get(), "V2G, Root", nullptr, nullptr), X509_free};
    std::vector<uint8_t> serial{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x10, 0x32,
                                0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x11, 0x22, 0x33, 0x44};
    set_serial(root.get(), key.get(), serial);
    auto path = (std::filesystem::temp_directory_path() / "ev-install-root-XXXXXX").string();
    const int fd = mkstemp(path.data());
    REQUIRE(fd >= 0);
    close(fd);
    struct RemoveFile {
        std::string path;
        ~RemoveFile() {
            std::filesystem::remove(path);
        }
    } cleanup{path};
    auto oem_key_pem = key_pem;
    bool usable_root = true;
    SECTION("valid first certificate in a PEM chain") {
        std::unique_ptr<X509, decltype(&X509_free)> other{make_cert(key.get(), "Other Root", nullptr, nullptr),
                                                          X509_free};
        std::ofstream(path) << cert_pem(root.get()) << cert_pem(other.get());
    }
    SECTION("zero serial remains usable") {
        serial.clear();
        set_serial(root.get(), key.get(), {0});
        std::ofstream(path) << cert_pem(root.get());
    }
    SECTION("missing root file") {
        std::filesystem::remove(path);
        usable_root = false;
    }
    SECTION("malformed root file") {
        std::ofstream(path) << "not a certificate";
        usable_root = false;
    }
    SECTION("negative serial") {
        ASN1_INTEGER_set(X509_get_serialNumber(root.get()), -1);
        X509_sign(root.get(), key.get(), EVP_sha256());
        std::ofstream(path) << cert_pem(root.get());
        usable_root = false;
    }
    SECTION("over-wide serial") {
        set_serial(root.get(), key.get(), std::vector<uint8_t>(message_2::MAX_SERIAL_NUMBER_BYTES + 1, 0x5A));
        std::ofstream(path) << cert_pem(root.get());
        usable_root = false;
    }
    SECTION("OEM key that cannot sign") {
        std::ofstream(path) << cert_pem(root.get());
        oem_key_pem = "not a key";
        usable_root = false;
    }

    ev::params::EvSessionParams params;
    params.secure_transport = true;
    params.pnc.prefer_contract = true;
    params.v2g_root_cert_path = path;
    params.pnc.oem_prov_key_pem = oem_key_pem;
    params.pnc.oem_prov_cert_der = cert_der(root.get());
    const auto seed_installation = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.evse_info.contract_offered = true;
        ctx.evse_info.certificate_service_offered = true;
        ctx.evse_info.certificate_installation_offered = true;
        ctx.pnc.contract_selected = true;
    };
    const auto selection_ok = d2_ok<message_2::PaymentServiceSelectionResponse>();
    if (not usable_root) {
        auto pred = D2Predecessor::make<ev::d2::state::PaymentServiceSelection>(params, false, seed_installation);
        pred.stops_without_request(selection_ok);
        return;
    }
    D2StateHelper helper{{}, params};
    helper.get_context().set_session_id(D2_SESSION_ID);
    seed_installation(helper);
    auto installation = d2_successor_of<ev::d2::state::PaymentServiceSelection>(helper, selection_ok);
    REQUIRE(installation->get_id() == ev::d2::StateID::CertificateInstallation);
    auto& exchange = helper.get_message_exchange();
    const auto request = exchange.take_request();
    REQUIRE(request.has_value());
    const auto& bytes = request->first;
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, const_cast<uint8_t*>(bytes.data()), bytes.size(), 0, nullptr);
    auto doc = std::make_unique<iso2_exiDocument>();
    REQUIRE(decode_iso2_exiDocument(&stream, doc.get()) == 0);
    REQUIRE(doc->V2G_Message.Header.Signature_isUsed);
    REQUIRE(doc->V2G_Message.Body.CertificateInstallationReq_isUsed);
    const auto& ids = doc->V2G_Message.Body.CertificateInstallationReq.ListOfRootCertificateIDs.RootCertificateID;
    REQUIRE(ids.arrayLen == 1);
    const auto& id = ids.array[0];
    REQUIRE(std::string(id.X509IssuerName.characters, id.X509IssuerName.charactersLen) == "CN=V2G\\, Root");
    std::array<uint8_t, EXI_BASETYPES_MAX_OCTETS_SUPPORTED> decoded{};
    size_t length = 0;
    REQUIRE_FALSE(id.X509SerialNumber.is_negative);
    REQUIRE(exi_basetypes_convert_bytes_from_unsigned(&id.X509SerialNumber.data, decoded.data(), &length,
                                                      decoded.size()) == 0);
    REQUIRE(std::vector<uint8_t>(decoded.begin(), decoded.begin() + length) == serial);
    REQUIRE_FALSE(exchange.has_request());
    REQUIRE_FALSE(helper.get_context().is_session_stopped());
}

SCENARIO("EV DomainComponent checks match only their own role") {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{gen_p256(), EVP_PKEY_free};
    for (const char* dc : {"CPS", "CPO", static_cast<const char*>(nullptr)}) {
        std::unique_ptr<X509, decltype(&X509_free)> cert{make_cert(key.get(), "Leaf", nullptr, nullptr, dc), X509_free};
        const std::string role = (dc != nullptr) ? dc : "";
        CAPTURE(role);
        REQUIRE(ev::crypto::has_cps_domain_component(cert.get()) == (role == "CPS"));
        REQUIRE(ev::crypto::has_cpo_domain_component(cert.get()) == (role == "CPO"));
    }
    REQUIRE_FALSE(ev::crypto::has_cps_domain_component(nullptr));

    std::unique_ptr<X509, decltype(&X509_free)> both{make_cert(key.get(), "Leaf", nullptr, nullptr, "CPO"), X509_free};
    X509_NAME_add_entry_by_txt(X509_get_subject_name(both.get()), "DC", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("CPS"), -1, -1, 0);
    REQUIRE(ev::crypto::has_cps_domain_component(both.get()));
    REQUIRE(ev::crypto::has_cpo_domain_component(both.get()));
}

// [V2G2-896]: a response that verifies against the trusted root is still discarded unless its signer
// carries DomainComponent=CPS.
SCENARIO("EV CertificateInstallation accepts only a response signed by a CPS") {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> root_key{gen_p256(), EVP_PKEY_free};
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> signer_key{gen_p256(), EVP_PKEY_free};
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> contract_key{gen_p256(), EVP_PKEY_free};
    const auto oem_pem = make_test_ec_key_pem();
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> oem{load_key(oem_pem), EVP_PKEY_free};
    std::unique_ptr<X509, decltype(&X509_free)> root{make_cert(root_key.get(), "Test V2G Root", nullptr, nullptr),
                                                     X509_free};
    std::unique_ptr<X509, decltype(&X509_free)> contract{
        make_cert(contract_key.get(), "DE1234567890", root.get(), root_key.get()), X509_free};

    auto path = (std::filesystem::temp_directory_path() / "ev-install-res-root-XXXXXX").string();
    const int fd = mkstemp(path.data());
    REQUIRE(fd >= 0);
    close(fd);
    struct RemoveFile {
        std::string path;
        ~RemoveFile() {
            std::filesystem::remove(path);
        }
    } cleanup{path};
    std::ofstream(path) << cert_pem(root.get());

    const char* signer_dc = "CPS";
    bool cps_signer = true;
    SECTION("signer with DomainComponent=CPO") {
        signer_dc = "CPO";
        cps_signer = false;
    }
    SECTION("signer without a DomainComponent") {
        signer_dc = nullptr;
        cps_signer = false;
    }
    SECTION("signer with DomainComponent=CPS") {
    }
    std::unique_ptr<X509, decltype(&X509_free)> signer{
        make_cert(signer_key.get(), "Test Signer", root.get(), root_key.get(), signer_dc), X509_free};

    const auto [encrypted, dh_pub] = encrypt_contract_scalar(oem.get(), std::vector<uint8_t>(32, 0x07));
    auto doc = std::make_unique<iso2_exiDocument>();
    fill_res(*doc, cert_der(signer.get()), cert_der(contract.get()), "DE1234567890");
    std::memcpy(doc->V2G_Message.Header.SessionID.bytes, D2_SESSION_ID.data(), D2_SESSION_ID.size());
    auto& body = doc->V2G_Message.Body.CertificateInstallationRes;
    set_bytes(body.ContractSignatureEncryptedPrivateKey.CONTENT, encrypted);
    set_bytes(body.DHpublickey.CONTENT, dh_pub);
    sign_res(*doc, signer_key.get());
    const auto exi = encode_doc(*doc);

    ev::params::EvSessionParams params;
    params.v2g_root_cert_path = path;
    params.pnc.oem_prov_key_pem = oem_pem;
    D2StateHelper helper{{}, params};
    auto& ctx = helper.get_context();
    ctx.set_session_id(D2_SESSION_ID);
    auto state = ctx.create_state<ev::d2::state::CertificateInstallation>();
    helper.get_message_exchange().set_response(
        std::make_unique<message_2::ResponseVariant>(io::StreamInputView{exi.data(), exi.size()}));
    auto result = state->feed(ev::d2::Event::V2GTP_MESSAGE);

    if (cps_signer) {
        REQUIRE(result.output == ev::d2::Disposition::Transitioning);
        REQUIRE(result.new_state->get_id() == ev::d2::StateID::PaymentDetails);
        REQUIRE(ctx.pnc.contract.has_value());
        REQUIRE(ctx.pnc.contract->emaid == "DE1234567890");
        REQUIRE_FALSE(ctx.is_session_stopped());
    } else {
        REQUIRE(result.new_state == nullptr);
        REQUIRE(ctx.is_session_stopped());
        REQUIRE_FALSE(ctx.pnc.contract.has_value());
    }
}

#endif

namespace {

// The trusted V2G root of the capture, tests/ocpp_tests/test_sets/everest-aux/certs/ca/v2g.
constexpr const char* V2G_ROOT_PEM = "-----BEGIN CERTIFICATE-----\n"
                                     "MIIBxTCCAWqgAwIBAgICMDkwCgYIKoZIzj0EAwIwSDESMBAGA1UEAwwJVjJHUm9v\n"
                                     "dENBMRAwDgYDVQQKDAdFVmVyZXN0MQswCQYDVQQGEwJERTETMBEGCgmSJomT8ixk\n"
                                     "ARkWA1YyRzAgFw0yMzA5MjYwNzM4MzRaGA8zMDIzMDEyNzA3MzgzNFowSDESMBAG\n"
                                     "A1UEAwwJVjJHUm9vdENBMRAwDgYDVQQKDAdFVmVyZXN0MQswCQYDVQQGEwJERTET\n"
                                     "MBEGCgmSJomT8ixkARkWA1YyRzBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABJjZ\n"
                                     "qKsQaffrsSSRTQE57gcpjuxtkKluOMbQWHmpBHgK7coPhm/xlmfDn/rRmQ0fvEqi\n"
                                     "zx/oDCt8yAObxSTyj3CjQjBAMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQD\n"
                                     "AgEGMB0GA1UdDgQWBBRnxqnie55mMxFdFY0Ht6WzBfPjPjAKBggqhkjOPQQDAgNJ\n"
                                     "ADBGAiEAzmGWz+ES3AskIzWkpyLReF5uumL3P9M6oGbuWQNI7oUCIQCxMh9YfpQ9\n"
                                     "ODORWoaQhzzcGylXRfW0Vo+KbGSUIM5UJQ==\n"
                                     "-----END CERTIFICATE-----\n";

// One CertificateInstallationRes, 4143 bytes of EXI, base64. eMAID id4 = UKSWI123456789A.
constexpr const char* RESPONSE_B64 =
    "gJgCKEU2LX/ropJKiVodHRwOi8vd3d3LnczLm9yZy9UUi9jYW5vbmljYWwtZXhpL0NWh0dHA6Ly93d3cudzMub3JnLzIwMDE\n"
    "vMDQveG1sZHNpZy1tb3JlI2VjZHNhLXNoYTI1NkQMRtLIYgStDo6OB0Xl7u7u5c7mZc3uTOXqikXsbC3N7c0sbC2FrK8NJek\n"
    "KWh0dHA6Ly93d3cudzMub3JnLzIwMDEvMDQveG1sZW5jI3NoYTI1NkIKDkFx+5xeFduxMWndL6x63rLqdXVtEXkfm2WP1Tus\n"
    "fzBAxG0shkBK0Ojo4HReXu7u7lzuZlze5M5eqKRexsLc3tzSxsLYWsrw0l6QpaHR0cDovL3d3dy53My5vcmcvMjAwMS8wNC9\n"
    "4bWxlbmMjc2hhMjU2QgLb8dUepS6KaKITsoShJ4TD4LB5erl4BYZ/9HqJII6M0EDEbSyGYErQ6OjgdF5e7u7uXO5mXN7kzl6\n"
    "opF7Gwtze3NLGwthayvDSXpClodHRwOi8vd3d3LnczLm9yZy8yMDAxLzA0L3htbGVuYyNzaGEyNTZCC818h/n046JxEku478\n"
    "Hy+bQdIzvbjhoTZD3XRcXOc1xwQMRtLIaAStDo6OB0Xl7u7u5c7mZc3uTOXqikXsbC3N7c0sbC2FrK8NJekKWh0dHA6Ly93d\n"
    "3cudzMub3JnLzIwMDEvMDQveG1sZW5jI3NoYTI1NkIF/HhNyPO33/Df+huv9J2SV8tWMLaMuQAseoDKUE962WEoCyAiMuDZh\n"
    "a09Ld7fOvu7VyZIwdnfDJg8gHMlzvR8Jwgexq6EDVoJvBYCU1O+euOg6lvvFWr9r0pTHQetO7jagcgYALlAzCCAeEwggGGoA\n"
    "MCAQICAjBHMAoGCCqGSM49BAMCMEkxEzARBgNVBAMMClByb3ZTdWJDQTIxEDAOBgNVBAoMB0VWZXJlc3QxCzAJBgNVBAYTAk\n"
    "RFMRMwEQYKCZImiZPyLGQBGRYDQ1BTMB4XDTIzMDkyNjA3MzgzNFoXDTQ4MDUxNzA3MzgzNFowRzERMA8GA1UEAwwIQ1BTIE\n"
    "xlYWYxEDAOBgNVBAoMB0VWZXJlc3QxCzAJBgNVBAYTAkRFMRMwEQYKCZImiZPyLGQBGRYDQ1BTMFkwEwYHKoZIzj0CAQYIKo\n"
    "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\n";

} // namespace

SCENARIO("EVCC verifies a CertificateInstallationRes signed by another implementation") {

    GIVEN("a response captured off the wire and the V2G root that signed its CPS chain") {
        const auto response = openssl::base64_decode(RESPONSE_B64, std::strlen(RESPONSE_B64));
        REQUIRE(response.size() == 4143);

        // Per-process name and a checked write: a shared path someone else owns would otherwise
        // fail silently and leave the verification running against their file.
        const auto root_path =
            std::filesystem::temp_directory_path() / ("ev_d2_interop_v2g_root_" + std::to_string(::getpid()) + ".pem");
        {
            std::ofstream out(root_path);
            out << V2G_ROOT_PEM;
            REQUIRE(out.good());
        }

        THEN("the signature over all four signed elements verifies") {
            REQUIRE(d2::crypto::verify_certificate_installation_res(response, root_path.string()));
        }

        std::filesystem::remove(root_path);
    }
}
