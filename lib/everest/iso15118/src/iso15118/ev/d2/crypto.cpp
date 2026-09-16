// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/detail/d2/crypto.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

#include <iso15118/detail/helper.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefDecoder.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

// The contract-key handling below uses the classic EC_KEY / EC_POINT API to parse the DHpublickey point
// and rebuild the contract key from its raw scalar. Deprecated in OpenSSL 3.0 but fully supported;
// suppressed for this translation unit rather than reimplemented over OSSL_PARAM.
#define OPENSSL_SUPPRESS_DEPRECATED

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <iso15118/detail/cb_exi.hpp>

namespace iso15118::ev::d2::crypto {

namespace {

constexpr std::size_t MAX_EXI_SIZE = 8192;
constexpr std::size_t SHA256_LEN = 32;
constexpr std::size_t ECDSA_SIG_LEN = 64; // r (32) || s (32)

// xmldsig algorithm identifiers; ISO 15118-2 uses EXI canonicalization.
constexpr char ALGO_CANONICAL_EXI[] = "http://www.w3.org/TR/canonical-exi/";
constexpr char ALGO_ECDSA_SHA256[] = "http://www.w3.org/2001/04/xmldsig-more#ecdsa-sha256";
constexpr char ALGO_SHA256[] = "http://www.w3.org/2001/04/xmlenc#sha256";

using X509_ptr = std::unique_ptr<X509, decltype(&X509_free)>;
using PKEY_ptr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

X509_ptr der_to_x509(const std::vector<uint8_t>& der) {
    const unsigned char* p = der.data();
    return X509_ptr(d2i_X509(nullptr, &p, static_cast<long>(der.size())), &X509_free);
}

bool sha256(const uint8_t* data, std::size_t len, std::array<uint8_t, SHA256_LEN>& out) {
    unsigned int md_len = 0;
    return EVP_Digest(data, len, out.data(), &md_len, EVP_sha256(), nullptr) == 1 and md_len == SHA256_LEN;
}

// ECDSA-P256 verify of a raw r||s signature over a SHA-256 digest.
bool ecdsa_verify(EVP_PKEY* pkey, const uint8_t* sig_rs, std::size_t sig_len,
                  const std::array<uint8_t, SHA256_LEN>& digest) {
    if (sig_len != ECDSA_SIG_LEN) {
        return false;
    }

    ECDSA_SIG* sig = ECDSA_SIG_new();
    if (sig == nullptr) {
        return false;
    }
    BIGNUM* r = BN_bin2bn(sig_rs, 32, nullptr);
    BIGNUM* s = BN_bin2bn(sig_rs + 32, 32, nullptr);
    if (r == nullptr or s == nullptr or ECDSA_SIG_set0(sig, r, s) != 1) {
        BN_free(r);
        BN_free(s);
        ECDSA_SIG_free(sig);
        return false;
    }

    unsigned char* der = nullptr;
    const int der_len = i2d_ECDSA_SIG(sig, &der);
    ECDSA_SIG_free(sig); // frees r and s
    if (der_len <= 0 or der == nullptr) {
        return false;
    }

    bool ok = false;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (ctx != nullptr and EVP_PKEY_verify_init(ctx) == 1) {
        ok = EVP_PKEY_verify(ctx, der, static_cast<size_t>(der_len), digest.data(), digest.size()) == 1;
    }
    EVP_PKEY_CTX_free(ctx);
    OPENSSL_free(der);
    // A failed verify leaves an entry on OpenSSL's thread-local error queue, which poisons the next
    // OpenSSL call on this thread (including the TLS read/write of the session). Clear it always.
    ERR_clear_error();
    return ok;
}

std::string cert_to_pem(X509* cert) {
    BIO* bio = BIO_new(BIO_s_mem());
    if (bio == nullptr) {
        return {};
    }
    std::string pem;
    if (PEM_write_bio_X509(bio, cert) == 1) {
        char* data = nullptr;
        const long n = BIO_get_mem_data(bio, &data);
        if (n > 0 and data != nullptr) {
            pem.assign(data, static_cast<std::size_t>(n));
        }
    }
    BIO_free(bio);
    return pem;
}

// False when `value` does not fit the fixed cbv2g character array; the field is then left untouched.
template <typename CbStringField> bool set_cb_string(CbStringField& field, const char* value) {
    const std::size_t len = std::strlen(value);
    if (len + 1 > sizeof(field.characters)) {
        logf_error("PnC: string of %zu bytes does not fit the %zu-byte EXI field", len, sizeof(field.characters));
        return false;
    }
    std::memcpy(field.characters, value, len);
    field.charactersLen = static_cast<uint16_t>(len);
    return true;
}

PKEY_ptr load_private_key(const PrivateKey& key) {
    BIO* bio = BIO_new_mem_buf(key.pem.data(), static_cast<int>(key.pem.size()));
    if (bio == nullptr) {
        return PKEY_ptr(nullptr, &EVP_PKEY_free);
    }
    // The password (if any) is the OpenSSL PEM callback userdata.
    void* pw = key.password ? const_cast<char*>(key.password->c_str()) : nullptr;
    PKEY_ptr pkey(PEM_read_bio_PrivateKey(bio, nullptr, nullptr, pw), &EVP_PKEY_free);
    BIO_free(bio);
    if (pkey == nullptr) {
        logf_error("PnC: failed to load EC private key from PEM");
        ERR_clear_error();
    }
    return pkey;
}

// ECDSA-P256 sign a SHA-256 digest; returns raw r||s (64 bytes) or empty.
std::vector<uint8_t> ecdsa_sign(EVP_PKEY* pkey, const std::array<uint8_t, SHA256_LEN>& digest) {
    std::vector<uint8_t> out;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (ctx == nullptr or EVP_PKEY_sign_init(ctx) != 1) {
        EVP_PKEY_CTX_free(ctx);
        ERR_clear_error();
        return out;
    }

    size_t der_len = 0;
    if (EVP_PKEY_sign(ctx, nullptr, &der_len, digest.data(), digest.size()) != 1) {
        EVP_PKEY_CTX_free(ctx);
        ERR_clear_error();
        return out;
    }
    std::vector<uint8_t> der(der_len);
    if (EVP_PKEY_sign(ctx, der.data(), &der_len, digest.data(), digest.size()) != 1) {
        EVP_PKEY_CTX_free(ctx);
        ERR_clear_error();
        return out;
    }
    EVP_PKEY_CTX_free(ctx);
    der.resize(der_len);

    const unsigned char* p = der.data();
    ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(der.size()));
    if (sig == nullptr) {
        ERR_clear_error();
        return out;
    }
    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(sig, &r, &s);
    out.assign(ECDSA_SIG_LEN, 0);
    if (BN_bn2binpad(r, out.data(), 32) != 32 or BN_bn2binpad(s, out.data() + 32, 32) != 32) {
        out.clear();
    }
    ECDSA_SIG_free(sig);
    return out;
}

// ConcatKDF (NIST SP 800-56A) with SHA-256, single block (output <= 32 bytes) over
// counter(0x00000001) || Z || OtherInfo.
std::vector<uint8_t> concat_kdf_sha256(const std::vector<uint8_t>& shared_secret,
                                       const std::vector<uint8_t>& other_info, std::size_t out_len) {
    std::vector<uint8_t> input;
    input.reserve(4 + shared_secret.size() + other_info.size());
    const uint8_t counter[4] = {0x00, 0x00, 0x00, 0x01};
    input.insert(input.end(), counter, counter + 4);
    input.insert(input.end(), shared_secret.begin(), shared_secret.end());
    input.insert(input.end(), other_info.begin(), other_info.end());

    std::array<uint8_t, SHA256_LEN> hash{};
    if (not sha256(input.data(), input.size(), hash)) {
        return {};
    }
    if (out_len > hash.size()) {
        return {};
    }
    return std::vector<uint8_t>(hash.begin(), hash.begin() + out_len);
}

// Clear the optional SignedInfo fields the verifier also clears [V2G2-771], so both sides digest the
// same fragment.
void normalize_signed_info(iso2_SignedInfoType& signed_info) {
    signed_info.Id_isUsed = 0;
    signed_info.CanonicalizationMethod.ANY_isUsed = 0;
    signed_info.SignatureMethod.HMACOutputLength_isUsed = 0;
    signed_info.SignatureMethod.ANY_isUsed = 0;
    for (auto* r = signed_info.Reference.array; r != signed_info.Reference.array + signed_info.Reference.arrayLen;
         ++r) {
        r->Type_isUsed = 0;
        r->Transforms.Transform.ANY_isUsed = 0;
        r->Transforms.Transform.XPath_isUsed = 0;
        r->DigestMethod.ANY_isUsed = 0;
    }
}

// SHA-256 over the EXI fragment of a normalized SignedInfo.
bool digest_signed_info(const iso2_SignedInfoType& signed_info, std::array<uint8_t, SHA256_LEN>& out) {
    auto fragment = std::make_unique<iso2_xmldsigFragment>();
    init_iso2_xmldsigFragment(fragment.get());
    fragment->SignedInfo_isUsed = 1;
    fragment->SignedInfo = signed_info;
    normalize_signed_info(fragment->SignedInfo);

    std::array<uint8_t, MAX_EXI_SIZE> buffer{};
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, buffer.data(), MAX_EXI_SIZE, 0, nullptr);
    if (encode_iso2_xmldsigFragment(&stream, fragment.get()) != 0) {
        logf_error("PnC: failed to encode the SignedInfo fragment");
        return false;
    }
    return sha256(buffer.data(), exi_bitstream_get_length(&stream), out);
}

// SHA-256 over an iso2_exiFragment carrying the single element `fill` sets.
template <typename Fill> bool digest_fragment(Fill fill, std::array<uint8_t, SHA256_LEN>& out) {
    auto fragment = std::make_unique<iso2_exiFragment>();
    init_iso2_exiFragment(fragment.get());
    fill(*fragment);

    std::array<uint8_t, MAX_EXI_SIZE> buffer{};
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, buffer.data(), MAX_EXI_SIZE, 0, nullptr);
    if (encode_iso2_exiFragment(&stream, fragment.get()) != 0) {
        return false;
    }
    return sha256(buffer.data(), exi_bitstream_get_length(&stream), out);
}

// SHA-256 over the EXI fragment of the single set request body element in `doc`.
bool digest_request_fragment(const iso2_exiDocument& doc, std::array<uint8_t, SHA256_LEN>& out) {
    const auto& body = doc.V2G_Message.Body;
    const auto fill = [&body](iso2_exiFragment& fragment) {
        if (body.AuthorizationReq_isUsed) {
            fragment.AuthorizationReq_isUsed = 1;
            std::memcpy(&fragment.AuthorizationReq, &body.AuthorizationReq, sizeof(fragment.AuthorizationReq));
        } else if (body.MeteringReceiptReq_isUsed) {
            fragment.MeteringReceiptReq_isUsed = 1;
            std::memcpy(&fragment.MeteringReceiptReq, &body.MeteringReceiptReq, sizeof(fragment.MeteringReceiptReq));
        } else if (body.CertificateInstallationReq_isUsed) {
            fragment.CertificateInstallationReq_isUsed = 1;
            std::memcpy(&fragment.CertificateInstallationReq, &body.CertificateInstallationReq,
                        sizeof(fragment.CertificateInstallationReq));
        }
    };

    if (not(body.AuthorizationReq_isUsed or body.MeteringReceiptReq_isUsed or body.CertificateInstallationReq_isUsed)) {
        return false;
    }
    if (not digest_fragment(fill, out)) {
        logf_error("PnC: failed to encode the request fragment for signing");
        return false;
    }
    return true;
}

// [V2G2-771]/[V2G2-909]: the CertificateInstallationRes signature covers four elements. Each must have
// a Reference with URI "#"+Id whose DigestValue is SHA-256 over that element's EXI fragment.
bool verify_res_references(const iso2_CertificateInstallationResType& res, const iso2_SignedInfoType& signed_info) {
    struct Element {
        const char* name;
        std::string id;
        std::array<uint8_t, SHA256_LEN> digest;
    };

    std::vector<Element> elements;
    const auto add = [&elements](const char* name, const char* id_chars, uint16_t id_len, auto fill) {
        Element element{name, std::string(id_chars, id_len), {}};
        if (not digest_fragment(fill, element.digest)) {
            logf_error("PnC: failed to encode the %s fragment", name);
            return false;
        }
        elements.push_back(std::move(element));
        return true;
    };

    const bool built =
        add("ContractSignatureCertChain",
            (res.ContractSignatureCertChain.Id_isUsed != 0) ? res.ContractSignatureCertChain.Id.characters : "",
            (res.ContractSignatureCertChain.Id_isUsed != 0) ? res.ContractSignatureCertChain.Id.charactersLen
                                                            : static_cast<uint16_t>(0),
            [&res](iso2_exiFragment& f) {
                f.ContractSignatureCertChain_isUsed = 1;
                std::memcpy(&f.ContractSignatureCertChain, &res.ContractSignatureCertChain,
                            sizeof(f.ContractSignatureCertChain));
            }) and
        add("ContractSignatureEncryptedPrivateKey", res.ContractSignatureEncryptedPrivateKey.Id.characters,
            res.ContractSignatureEncryptedPrivateKey.Id.charactersLen,
            [&res](iso2_exiFragment& f) {
                f.ContractSignatureEncryptedPrivateKey_isUsed = 1;
                std::memcpy(&f.ContractSignatureEncryptedPrivateKey, &res.ContractSignatureEncryptedPrivateKey,
                            sizeof(f.ContractSignatureEncryptedPrivateKey));
            }) and
        add("DHpublickey", res.DHpublickey.Id.characters, res.DHpublickey.Id.charactersLen,
            [&res](iso2_exiFragment& f) {
                f.DHpublickey_isUsed = 1;
                std::memcpy(&f.DHpublickey, &res.DHpublickey, sizeof(f.DHpublickey));
            }) and
        add("eMAID", res.eMAID.Id.characters, res.eMAID.Id.charactersLen, [&res](iso2_exiFragment& f) {
            f.eMAID_isUsed = 1;
            std::memcpy(&f.eMAID, &res.eMAID, sizeof(f.eMAID));
        });
    if (not built) {
        return false;
    }

    for (const auto& element : elements) {
        if (element.id.empty()) {
            logf_error("PnC: CertificateInstallationRes %s carries no Id", element.name);
            return false;
        }
        const std::string uri = "#" + element.id;
        const iso2_ReferenceType* match = nullptr;
        for (uint16_t i = 0; i < signed_info.Reference.arrayLen; ++i) {
            const auto& ref = signed_info.Reference.array[i];
            if (ref.URI_isUsed != 0 and std::string(ref.URI.characters, ref.URI.charactersLen) == uri) {
                match = &ref;
                break;
            }
        }
        if (match == nullptr) {
            logf_error("PnC: CertificateInstallationRes signature references no %s", element.name);
            return false;
        }
        if (match->DigestValue.bytesLen != element.digest.size() or
            std::memcmp(match->DigestValue.bytes, element.digest.data(), element.digest.size()) != 0) {
            logf_error("PnC: CertificateInstallationRes %s digest mismatch", element.name);
            return false;
        }
    }
    return true;
}

// Build the xmldsig Signature over the set request body element (already carrying its Id), attach it to
// the header and encode the document. Returns the EXI payload or empty.
std::vector<uint8_t> finalize_signed(iso2_exiDocument& doc, const std::string& element_id, const PrivateKey& key) {
    std::array<uint8_t, SHA256_LEN> element_digest{};
    if (not digest_request_fragment(doc, element_digest)) {
        return {};
    }

    auto& signature = doc.V2G_Message.Header.Signature;
    init_iso2_SignatureType(&signature);
    auto& signed_info = signature.SignedInfo;

    signed_info.Reference.arrayLen = 1;
    auto& ref = signed_info.Reference.array[0];
    const std::string uri = "#" + element_id;
    if (not set_cb_string(signed_info.CanonicalizationMethod.Algorithm, ALGO_CANONICAL_EXI) or
        not set_cb_string(signed_info.SignatureMethod.Algorithm, ALGO_ECDSA_SHA256) or
        not set_cb_string(ref.URI, uri.c_str()) or
        not set_cb_string(ref.Transforms.Transform.Algorithm, ALGO_CANONICAL_EXI) or
        not set_cb_string(ref.DigestMethod.Algorithm, ALGO_SHA256)) {
        return {};
    }
    ref.URI_isUsed = 1;
    ref.Transforms_isUsed = 1;
    std::memcpy(ref.DigestValue.bytes, element_digest.data(), element_digest.size());
    ref.DigestValue.bytesLen = static_cast<uint16_t>(element_digest.size());

    std::array<uint8_t, SHA256_LEN> si_digest{};
    if (not digest_signed_info(signed_info, si_digest)) {
        return {};
    }

    auto pkey = load_private_key(key);
    if (pkey == nullptr) {
        return {};
    }
    const auto sig_rs = ecdsa_sign(pkey.get(), si_digest);
    if (sig_rs.size() != ECDSA_SIG_LEN) {
        logf_error("PnC: failed to ECDSA-sign the SignedInfo");
        return {};
    }
    std::memcpy(signature.SignatureValue.CONTENT.bytes, sig_rs.data(), sig_rs.size());
    signature.SignatureValue.CONTENT.bytesLen = static_cast<uint16_t>(sig_rs.size());
    doc.V2G_Message.Header.Signature_isUsed = 1;

    std::array<uint8_t, MAX_EXI_SIZE> out_buffer{};
    exi_bitstream_t out_stream;
    exi_bitstream_init(&out_stream, out_buffer.data(), MAX_EXI_SIZE, 0, nullptr);
    if (encode_iso2_exiDocument(&out_stream, &doc) != 0) {
        logf_error("PnC: failed to encode the signed message document");
        return {};
    }
    const auto len = exi_bitstream_get_length(&out_stream);
    return std::vector<uint8_t>(out_buffer.data(), out_buffer.data() + len);
}

} // namespace

std::vector<uint8_t> serialize_signed(const message_2::AuthorizationRequest& req, const PrivateKey& key) {
    iso2_exiDocument doc{};
    message_2::convert(req.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.AuthorizationReq);
    message_2::convert(req, doc.V2G_Message.Body.AuthorizationReq);
    const std::string id = req.id.value_or("id1");
    if (not set_cb_string(doc.V2G_Message.Body.AuthorizationReq.Id, id.c_str())) {
        return {};
    }
    doc.V2G_Message.Body.AuthorizationReq.Id_isUsed = 1;
    return finalize_signed(doc, id, key);
}

std::vector<uint8_t> serialize_signed(const message_2::MeteringReceiptRequest& req, const PrivateKey& key) {
    iso2_exiDocument doc{};
    message_2::convert(req.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.MeteringReceiptReq);
    message_2::convert(req, doc.V2G_Message.Body.MeteringReceiptReq);
    // MeteringReceiptRequest has no id field; the signature reference needs one.
    const std::string id = "id1";
    if (not set_cb_string(doc.V2G_Message.Body.MeteringReceiptReq.Id, id.c_str())) {
        return {};
    }
    doc.V2G_Message.Body.MeteringReceiptReq.Id_isUsed = 1;
    return finalize_signed(doc, id, key);
}

std::vector<uint8_t> serialize_signed(const message_2::CertificateInstallationRequest& req, const PrivateKey& key) {
    iso2_exiDocument doc{};
    message_2::convert(req.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.CertificateInstallationReq);
    message_2::convert(req, doc.V2G_Message.Body.CertificateInstallationReq);
    // convert() already set Id from req.id; reuse it as the signature reference.
    return finalize_signed(doc, req.id, key);
}

bool verify_certificate_installation_res(const std::vector<uint8_t>& res_exi, const std::string& v2g_root_path) {
    if (res_exi.empty()) {
        return false;
    }

    exi_bitstream_t in_stream;
    exi_bitstream_init(&in_stream, const_cast<uint8_t*>(res_exi.data()), res_exi.size(), 0, nullptr);
    auto doc = std::make_unique<iso2_exiDocument>();
    if (decode_iso2_exiDocument(&in_stream, doc.get()) != 0) {
        logf_error("PnC: failed to re-decode CertificateInstallationRes for signature verification");
        return false;
    }
    if (doc->V2G_Message.Header.Signature_isUsed == 0 or doc->V2G_Message.Body.CertificateInstallationRes_isUsed == 0) {
        logf_error("PnC: CertificateInstallationRes carries no signature");
        return false;
    }

    const auto& res = doc->V2G_Message.Body.CertificateInstallationRes;
    const iso2_SignatureType& signature = doc->V2G_Message.Header.Signature;

    // The signing key is the SAProvisioningCertificateChain leaf; validate it up to the trusted V2G root.
    const auto& sa_leaf = res.SAProvisioningCertificateChain.Certificate;
    const std::vector<uint8_t> sa_leaf_der(sa_leaf.bytes, sa_leaf.bytes + sa_leaf.bytesLen);
    std::vector<std::vector<uint8_t>> sa_subs;
    if (res.SAProvisioningCertificateChain.SubCertificates_isUsed) {
        const auto& subs = res.SAProvisioningCertificateChain.SubCertificates.Certificate;
        for (uint16_t i = 0; i < subs.arrayLen; ++i) {
            sa_subs.emplace_back(subs.array[i].bytes, subs.array[i].bytes + subs.array[i].bytesLen);
        }
    }

    auto leaf = der_to_x509(sa_leaf_der);
    if (leaf == nullptr) {
        logf_error("PnC: failed to parse the SAProvisioningCertificate leaf");
        return false;
    }

    {
        std::vector<X509_ptr> sub_x509;
        STACK_OF(X509)* untrusted = sk_X509_new_null();
        for (const auto& der : sa_subs) {
            auto x = der_to_x509(der);
            if (x != nullptr) {
                sk_X509_push(untrusted, x.get());
                sub_x509.push_back(std::move(x));
            }
        }
        X509_STORE* store = X509_STORE_new();
        const bool any_root = store != nullptr and not v2g_root_path.empty() and
                              X509_STORE_load_locations(store, v2g_root_path.c_str(), nullptr) == 1;
        bool chain_ok = false;
        if (any_root) {
            X509_STORE_CTX* ctx = X509_STORE_CTX_new();
            if (ctx != nullptr and X509_STORE_CTX_init(ctx, store, leaf.get(), untrusted) == 1) {
                chain_ok = X509_verify_cert(ctx) == 1;
                if (not chain_ok) {
                    logf_error("PnC: SAProvisioningCertificate chain verification failed: %s",
                               X509_verify_cert_error_string(X509_STORE_CTX_get_error(ctx)));
                }
            }
            if (ctx != nullptr) {
                X509_STORE_CTX_free(ctx);
            }
        } else {
            logf_error("PnC: no V2G root available to validate the SAProvisioningCertificate chain");
        }
        if (store != nullptr) {
            X509_STORE_free(store);
        }
        sk_X509_free(untrusted);
        ERR_clear_error();
        if (not chain_ok) {
            return false;
        }
    }

    PKEY_ptr pkey(X509_get_pubkey(leaf.get()), &EVP_PKEY_free);
    if (pkey == nullptr) {
        return false;
    }

    if (not verify_res_references(res, signature.SignedInfo)) {
        return false;
    }

    std::array<uint8_t, SHA256_LEN> si_digest{};
    if (not digest_signed_info(signature.SignedInfo, si_digest)) {
        return false;
    }

    const bool ok = ecdsa_verify(pkey.get(), signature.SignatureValue.CONTENT.bytes,
                                 signature.SignatureValue.CONTENT.bytesLen, si_digest);
    if (not ok) {
        logf_error("PnC: CertificateInstallationRes signature verification failed");
    }
    return ok;
}

std::vector<uint8_t> decrypt_contract_private_key(const std::vector<uint8_t>& encrypted_with_iv,
                                                  const std::vector<uint8_t>& dh_public_key,
                                                  const PrivateKey& oem_priv_key) {
    constexpr std::size_t IV_LEN = 16;
    constexpr std::size_t PRIV_KEY_LEN = 32;
    if (encrypted_with_iv.size() <= IV_LEN or dh_public_key.empty()) {
        logf_error("PnC: malformed ContractSignatureEncryptedPrivateKey");
        return {};
    }

    auto oem_key = load_private_key(oem_priv_key);
    if (oem_key == nullptr) {
        return {};
    }

    // Peer (sender ephemeral) public key on prime256v1 from the uncompressed point.
    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    EC_POINT* point = (group != nullptr) ? EC_POINT_new(group) : nullptr;
    EC_KEY* peer_ec = (group != nullptr) ? EC_KEY_new_by_curve_name(NID_X9_62_prime256v1) : nullptr;
    PKEY_ptr peer_key(nullptr, &EVP_PKEY_free);
    if (group != nullptr and point != nullptr and peer_ec != nullptr and
        EC_POINT_oct2point(group, point, dh_public_key.data(), dh_public_key.size(), nullptr) == 1 and
        EC_KEY_set_public_key(peer_ec, point) == 1) {
        EVP_PKEY* pk = EVP_PKEY_new();
        if (pk != nullptr and EVP_PKEY_set1_EC_KEY(pk, peer_ec) == 1) {
            peer_key.reset(pk);
        } else if (pk != nullptr) {
            EVP_PKEY_free(pk);
        }
    }
    if (point != nullptr) {
        EC_POINT_free(point);
    }
    if (peer_ec != nullptr) {
        EC_KEY_free(peer_ec);
    }
    if (group != nullptr) {
        EC_GROUP_free(group);
    }
    if (peer_key == nullptr) {
        logf_error("PnC: failed to build the DHpublickey EC point");
        ERR_clear_error();
        return {};
    }

    std::vector<uint8_t> shared_secret;
    {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(oem_key.get(), nullptr);
        size_t secret_len = 0;
        if (ctx != nullptr and EVP_PKEY_derive_init(ctx) == 1 and EVP_PKEY_derive_set_peer(ctx, peer_key.get()) == 1 and
            EVP_PKEY_derive(ctx, nullptr, &secret_len) == 1) {
            shared_secret.resize(secret_len);
            if (EVP_PKEY_derive(ctx, shared_secret.data(), &secret_len) != 1) {
                shared_secret.clear();
            } else {
                shared_secret.resize(secret_len);
            }
        }
        if (ctx != nullptr) {
            EVP_PKEY_CTX_free(ctx);
        }
    }
    if (shared_secret.empty()) {
        logf_error("PnC: ECDH key agreement failed");
        ERR_clear_error();
        return {};
    }

    // OtherInfo = AlgorithmID(0x01) || PartyU(0x55) || PartyV(0x56), 16-byte session key.
    const std::vector<uint8_t> other_info{0x01, 0x55, 0x56};
    const auto session_key = concat_kdf_sha256(shared_secret, other_info, 16);
    if (session_key.size() != 16) {
        return {};
    }

    // AES-128-CBC, no padding: the plaintext is exactly the 32-byte private scalar.
    const uint8_t* iv = encrypted_with_iv.data();
    const uint8_t* ciphertext = encrypted_with_iv.data() + IV_LEN;
    const int ct_len = static_cast<int>(encrypted_with_iv.size() - IV_LEN);
    std::vector<uint8_t> plaintext(static_cast<std::size_t>(ct_len) + IV_LEN, 0);
    int out_len = 0;
    int final_len = 0;
    bool ok = false;
    EVP_CIPHER_CTX* cctx = EVP_CIPHER_CTX_new();
    if (cctx != nullptr and EVP_DecryptInit_ex(cctx, EVP_aes_128_cbc(), nullptr, session_key.data(), iv) == 1) {
        EVP_CIPHER_CTX_set_padding(cctx, 0);
        if (EVP_DecryptUpdate(cctx, plaintext.data(), &out_len, ciphertext, ct_len) == 1 and
            EVP_DecryptFinal_ex(cctx, plaintext.data() + out_len, &final_len) == 1) {
            ok = true;
        }
    }
    if (cctx != nullptr) {
        EVP_CIPHER_CTX_free(cctx);
    }
    ERR_clear_error();
    if (not ok) {
        logf_error("PnC: AES-128-CBC decryption of the contract private key failed");
        return {};
    }
    plaintext.resize(static_cast<std::size_t>(out_len + final_len));
    if (plaintext.size() != PRIV_KEY_LEN) {
        logf_error("PnC: decrypted contract private key has unexpected length %zu", plaintext.size());
        return {};
    }
    return plaintext;
}

std::string contract_scalar_to_pem(const std::vector<uint8_t>& scalar) {
    if (scalar.size() != 32) {
        return {};
    }
    std::string pem;
    EC_KEY* ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    BIGNUM* priv = BN_bin2bn(scalar.data(), static_cast<int>(scalar.size()), nullptr);
    const EC_GROUP* group = (ec != nullptr) ? EC_KEY_get0_group(ec) : nullptr;
    EC_POINT* pub = (group != nullptr) ? EC_POINT_new(group) : nullptr;
    if (ec != nullptr and priv != nullptr and pub != nullptr and EC_KEY_set_private_key(ec, priv) == 1 and
        EC_POINT_mul(group, pub, priv, nullptr, nullptr, nullptr) == 1 and EC_KEY_set_public_key(ec, pub) == 1) {
        BIO* bio = BIO_new(BIO_s_mem());
        if (bio != nullptr and PEM_write_bio_ECPrivateKey(bio, ec, nullptr, nullptr, 0, nullptr, nullptr) == 1) {
            char* data = nullptr;
            const long n = BIO_get_mem_data(bio, &data);
            if (n > 0 and data != nullptr) {
                pem.assign(data, static_cast<std::size_t>(n));
            }
        }
        if (bio != nullptr) {
            BIO_free(bio);
        }
    }
    if (pub != nullptr) {
        EC_POINT_free(pub);
    }
    if (priv != nullptr) {
        BN_free(priv);
    }
    if (ec != nullptr) {
        EC_KEY_free(ec);
    }
    ERR_clear_error();
    return pem;
}

std::string der_chain_to_pem(const std::vector<uint8_t>& leaf_der, const std::vector<std::vector<uint8_t>>& subs_der) {
    std::string pem;
    const auto append = [&pem](const std::vector<uint8_t>& der) {
        auto x = der_to_x509(der);
        if (x != nullptr) {
            pem += cert_to_pem(x.get());
        }
    };
    append(leaf_der);
    for (const auto& sub : subs_der) {
        append(sub);
    }
    return pem;
}

std::string emaid_from_contract_der(const std::vector<uint8_t>& leaf_der) {
    auto leaf = der_to_x509(leaf_der);
    if (leaf == nullptr) {
        return {};
    }
    X509_NAME* name = X509_get_subject_name(leaf.get());
    if (name == nullptr) {
        return {};
    }
    char buf[256] = {0};
    const int len = X509_NAME_get_text_by_NID(name, NID_commonName, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return {};
    }
    std::string cn(buf, static_cast<std::size_t>(len));
    cn.erase(std::remove(cn.begin(), cn.end(), '-'), cn.end());
    return cn;
}

std::vector<std::vector<uint8_t>> pem_chain_to_der(const std::string& pem) {
    std::vector<std::vector<uint8_t>> out;
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (bio == nullptr) {
        return out;
    }
    X509* cert = nullptr;
    while ((cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) != nullptr) {
        unsigned char* der = nullptr;
        const int len = i2d_X509(cert, &der);
        if (len > 0 and der != nullptr) {
            out.emplace_back(der, der + len);
        }
        OPENSSL_free(der);
        X509_free(cert);
    }
    BIO_free(bio);
    ERR_clear_error(); // the loop ends on a benign "no start line" PEM error
    return out;
}

message_2::RootCertificateId root_cert_id_from_der(const std::vector<uint8_t>& root_der) {
    message_2::RootCertificateId id;
    auto root = der_to_x509(root_der);
    if (root == nullptr) {
        return id;
    }
    // ISO 15118-2 X509IssuerName is the RFC 2253 form of the DN.
    BIO* bio = BIO_new(BIO_s_mem());
    if (bio != nullptr) {
        if (X509_NAME_print_ex(bio, X509_get_issuer_name(root.get()), 0, XN_FLAG_RFC2253) >= 0) {
            char* data = nullptr;
            const long n = BIO_get_mem_data(bio, &data);
            if (n > 0 and data != nullptr) {
                id.issuer_name.assign(data, static_cast<std::size_t>(n));
            }
        }
        BIO_free(bio);
    }
    const ASN1_INTEGER* serial = X509_get0_serialNumber(root.get());
    if (serial != nullptr) {
        int64_t value = 0;
        if (ASN1_INTEGER_get_int64(&value, serial) == 1) {
            id.serial_number = value;
        }
    }
    return id;
}

} // namespace iso15118::ev::d2::crypto
