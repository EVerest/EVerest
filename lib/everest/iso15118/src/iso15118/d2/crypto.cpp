// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/d2/crypto.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <memory>

#include <iso15118/detail/helper.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefDecoder.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

// The EVCC contract-key handling below uses the classic EC_KEY / EC_POINT API to parse the DHpublickey
// point and rebuild the contract key from its raw scalar. These are marked deprecated in OpenSSL 3.0 but
// remain fully supported; suppress the deprecation attribute for this translation unit (documented
// OpenSSL mechanism) rather than reimplement point parsing via the more verbose OSSL_PARAM path.
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

namespace iso15118::d2::crypto {

namespace {

constexpr std::size_t MAX_EXI_SIZE = 8192;
constexpr std::size_t SHA256_LEN = 32;
constexpr std::size_t ECDSA_SIG_LEN = 64; // r (32) || s (32)

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

// ECDSA-P256 verify of a raw r||s signature (64 bytes) over a SHA-256 digest.
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
    // A failed EVP_PKEY_verify pushes an entry onto OpenSSL's thread-local error queue. Left
    // uncleared it poisons the next OpenSSL call on this thread -- the TLS SSL_read/SSL_write that
    // should carry the FAILED_* response then aborts ("SSL_read_ex ... ECDSA verify"), so the SECC
    // drops the connection instead of answering. Clear the queue on every exit path.
    ERR_clear_error();
    return ok;
}

std::string emaid_from_cert(X509* cert) {
    std::string cn;
    X509_NAME* name = X509_get_subject_name(cert);
    if (name == nullptr) {
        return cn;
    }
    char buf[256] = {0};
    const int len = X509_NAME_get_text_by_NID(name, NID_commonName, buf, sizeof(buf) - 1);
    if (len > 0) {
        cn.assign(buf, static_cast<std::size_t>(len));
    }
    return cn;
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

std::string strip_dashes(std::string in) {
    in.erase(std::remove(in.begin(), in.end(), '-'), in.end());
    return in;
}

// --- EVCC signing / decryption helpers ---

// xmldsig algorithm identifiers (ISO 15118-2 uses EXI canonicalization).
constexpr char ALGO_CANONICAL_EXI[] = "http://www.w3.org/TR/canonical-exi/";
constexpr char ALGO_ECDSA_SHA256[] = "http://www.w3.org/2001/04/xmldsig-more#ecdsa-sha256";
constexpr char ALGO_SHA256[] = "http://www.w3.org/2001/04/xmlenc#sha256";

template <typename CbStringField> void set_cb_string(CbStringField& field, const char* value) {
    const std::size_t len = std::strlen(value);
    std::memcpy(field.characters, value, len);
    field.charactersLen = static_cast<uint16_t>(len);
}

PKEY_ptr load_private_key(const PrivateKey& key) {
    BIO* bio = BIO_new_mem_buf(key.pem.data(), static_cast<int>(key.pem.size()));
    if (bio == nullptr) {
        return PKEY_ptr(nullptr, &EVP_PKEY_free);
    }
    // The password (if any) is passed as the OpenSSL PEM callback userdata.
    void* pw = key.password ? const_cast<char*>(key.password->c_str()) : nullptr;
    PKEY_ptr pkey(PEM_read_bio_PrivateKey(bio, nullptr, nullptr, pw), &EVP_PKEY_free);
    BIO_free(bio);
    if (pkey == nullptr) {
        logf_error("PnC: failed to load EC private key from PEM");
        ERR_clear_error();
    }
    return pkey;
}

// ECDSA-P256 sign a SHA-256 digest and return the raw r||s (64 bytes), or empty on failure.
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

// ConcatKDF (NIST SP 800-56A) with SHA-256: single block (output <= 32 bytes) over
// counter(0x00000001) || Z || OtherInfo. Returns `out_len` bytes.
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

} // namespace

ContractValidationResult validate_contract_chain(const std::vector<uint8_t>& leaf_der,
                                                 const std::vector<std::vector<uint8_t>>& sub_certs,
                                                 const std::string& req_emaid, const std::string& mo_root_path,
                                                 const std::string& v2g_root_path) {
    ContractValidationResult result;

    if (leaf_der.empty()) {
        logf_error("PnC: no contract certificate received");
        result.response_code = dt::ResponseCode::FAILED;
        return result;
    }

    auto leaf = der_to_x509(leaf_der);
    if (leaf == nullptr) {
        logf_error("PnC: failed to parse contract leaf certificate");
        result.response_code = dt::ResponseCode::FAILED;
        return result;
    }

    // Cross-check the requested eMAID against the leaf CommonName (case-insensitive, '-' removed).
    const auto cert_emaid = emaid_from_cert(leaf.get());
    {
        const auto cert_e = strip_dashes(cert_emaid);
        const auto req_e = strip_dashes(req_emaid);
        if (cert_e.size() != req_e.size() or
            std::equal(cert_e.begin(), cert_e.end(), req_e.begin(),
                       [](char a, char b) { return std::tolower(a) == std::tolower(b); }) == false) {
            logf_error("PnC: eMAID mismatch (req '%s' vs cert '%s')", req_emaid.c_str(), cert_emaid.c_str());
            result.response_code = dt::ResponseCode::FAILED;
            return result;
        }
    }

    // Build the untrusted intermediate stack from the SubCertificates.
    std::vector<X509_ptr> sub_x509;
    STACK_OF(X509)* untrusted = sk_X509_new_null();
    if (untrusted == nullptr) {
        result.response_code = dt::ResponseCode::FAILED;
        return result;
    }
    for (const auto& der : sub_certs) {
        auto x = der_to_x509(der);
        if (x == nullptr) {
            logf_error("PnC: failed to parse a SubCertificate");
            sk_X509_free(untrusted);
            result.response_code = dt::ResponseCode::FAILED;
            return result;
        }
        sk_X509_push(untrusted, x.get());
        sub_x509.push_back(std::move(x));
    }

    // Trust anchors: the MO root (and, mirroring EvseV2G, the V2G root too).
    X509_STORE* store = X509_STORE_new();
    bool any_root = false;
    if (store != nullptr) {
        if (not mo_root_path.empty() and X509_STORE_load_locations(store, mo_root_path.c_str(), nullptr) == 1) {
            any_root = true;
        }
        if (not v2g_root_path.empty() and X509_STORE_load_locations(store, v2g_root_path.c_str(), nullptr) == 1) {
            any_root = true;
        }
    }

    // The leaf parsed and the eMAID matched: fill the contract identity so a forwardable failure can
    // still hand the chain to the CSMS for central validation.
    result.emaid = strip_dashes(cert_emaid);
    result.chain_pem = cert_to_pem(leaf.get());
    for (const auto& x : sub_x509) {
        result.chain_pem += cert_to_pem(x.get());
    }

    if (store == nullptr or not any_root) {
        logf_error("PnC: no MO/V2G root available to validate the contract chain");
        sk_X509_free(untrusted);
        if (store != nullptr) {
            X509_STORE_free(store);
        }
        result.response_code = dt::ResponseCode::FAILED_NoCertificateAvailable;
        result.forwardable = true;
        return result;
    }

    X509_STORE_CTX* ctx = X509_STORE_CTX_new();
    result.response_code = dt::ResponseCode::FAILED;
    if (ctx != nullptr and X509_STORE_CTX_init(ctx, store, leaf.get(), untrusted) == 1) {
        const int rc = X509_verify_cert(ctx);
        if (rc == 1) {
            result.response_code = dt::ResponseCode::OK;
        } else {
            const int err = X509_STORE_CTX_get_error(ctx);
            const int err_depth = X509_STORE_CTX_get_error_depth(ctx);
            logf_error("PnC: contract chain verification failed at depth %d: %s", err_depth,
                       X509_verify_cert_error_string(err));
            switch (err) {
            case X509_V_ERR_CERT_HAS_EXPIRED:
            case X509_V_ERR_CERT_NOT_YET_VALID:
                // FAILED_CertificateExpired applies only to the contract (leaf) certificate itself
                // (depth 0) -- ISO 15118-4 TC PaymentDetails_007. An expired/not-yet-valid Sub-CA
                // (depth > 0) is a chain failure: report the generic FAILED, the code the ATS
                // accepts for an expired issuer (PaymentDetails_009/_010, Tables 171/172).
                result.response_code =
                    (err_depth == 0) ? dt::ResponseCode::FAILED_CertificateExpired : dt::ResponseCode::FAILED;
                break;
            case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
            case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
            case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
                // Local trust anchor missing (e.g. no MO root installed): eligible for central
                // validation by the CSMS when allowed (EvseV2G NoCertificateAvailable parity).
                result.response_code = dt::ResponseCode::FAILED;
                result.forwardable = true;
                break;
            default:
                result.response_code = dt::ResponseCode::FAILED;
                break;
            }
        }
    }

    if (ctx != nullptr) {
        X509_STORE_CTX_free(ctx);
    }
    X509_STORE_free(store);
    sk_X509_free(untrusted);

    // A verification failure (e.g. a malformed public key -> X509_PUBKEY_get0 decode error) leaves
    // entries on OpenSSL's thread-local error queue. On TLS this queue is shared with the SECC's own
    // SSL_read on the same thread, which would then misread the stale error as a connection fault and
    // tear the TCP connection down before the FAILED PaymentDetailsRes is sent. Clear it so only this
    // function's result (the ResponseCode) leaves the chain check.
    ERR_clear_error();

    return result;
}

bool verify_authorization_signature(const std::vector<uint8_t>& request_exi, const std::vector<uint8_t>& leaf_der) {
    if (request_exi.empty() or leaf_der.empty()) {
        return false;
    }

    // Re-decode the raw request to recover the cbv2g iso2 structs (Header.Signature + AuthorizationReq).
    exi_bitstream_t in_stream;
    exi_bitstream_init(&in_stream, const_cast<uint8_t*>(request_exi.data()), request_exi.size(), 0, nullptr);
    auto doc = std::make_unique<iso2_exiDocument>();
    if (decode_iso2_exiDocument(&in_stream, doc.get()) != 0) {
        logf_error("PnC: failed to re-decode AuthorizationReq for signature verification");
        return false;
    }

    if (doc->V2G_Message.Header.Signature_isUsed == 0) {
        logf_error("PnC: AuthorizationReq carries no signature");
        return false;
    }
    if (doc->V2G_Message.Body.AuthorizationReq_isUsed == 0) {
        logf_error("PnC: re-decoded message is not an AuthorizationReq");
        return false;
    }

    const iso2_SignatureType& signature = doc->V2G_Message.Header.Signature;

    auto leaf = der_to_x509(leaf_der);
    if (leaf == nullptr) {
        return false;
    }
    PKEY_ptr pkey(X509_get_pubkey(leaf.get()), &EVP_PKEY_free);
    if (pkey == nullptr) {
        return false;
    }

    std::array<uint8_t, MAX_EXI_SIZE> exi_buffer{};
    exi_bitstream_t stream;

    // 1) digest over the signed AuthorizationReq EXI fragment; compare against Reference[0].DigestValue.
    {
        auto fragment = std::make_unique<iso2_exiFragment>();
        init_iso2_exiFragment(fragment.get());
        fragment->AuthorizationReq_isUsed = 1;
        std::memcpy(&fragment->AuthorizationReq, &doc->V2G_Message.Body.AuthorizationReq,
                    sizeof(fragment->AuthorizationReq));

        exi_bitstream_init(&stream, exi_buffer.data(), MAX_EXI_SIZE, 0, nullptr);
        if (encode_iso2_exiFragment(&stream, fragment.get()) != 0) {
            logf_error("PnC: failed to encode AuthorizationReq fragment");
            return false;
        }

        std::array<uint8_t, SHA256_LEN> digest{};
        if (not sha256(exi_buffer.data(), exi_bitstream_get_length(&stream), digest)) {
            return false;
        }

        const iso2_ReferenceType& ref = signature.SignedInfo.Reference.array[0];
        if (ref.DigestValue.bytesLen != digest.size() or
            std::memcmp(ref.DigestValue.bytes, digest.data(), digest.size()) != 0) {
            logf_error("PnC: AuthorizationReq digest mismatch");
            return false;
        }
    }

    // 2) digest over the SignedInfo xmldsig fragment; ECDSA-verify against the contract public key.
    std::array<uint8_t, SHA256_LEN> si_digest{};
    {
        auto sig_fragment = std::make_unique<iso2_xmldsigFragment>();
        init_iso2_xmldsigFragment(sig_fragment.get());
        sig_fragment->SignedInfo_isUsed = 1;
        sig_fragment->SignedInfo = signature.SignedInfo;

        // [V2G2-771] the optional fields below must not be present in the fragment to be verified.
        sig_fragment->SignedInfo.Id_isUsed = 0;
        sig_fragment->SignedInfo.CanonicalizationMethod.ANY_isUsed = 0;
        sig_fragment->SignedInfo.SignatureMethod.HMACOutputLength_isUsed = 0;
        sig_fragment->SignedInfo.SignatureMethod.ANY_isUsed = 0;
        for (auto* ref = sig_fragment->SignedInfo.Reference.array;
             ref != sig_fragment->SignedInfo.Reference.array + sig_fragment->SignedInfo.Reference.arrayLen; ++ref) {
            ref->Type_isUsed = 0;
            ref->Transforms.Transform.ANY_isUsed = 0;
            ref->Transforms.Transform.XPath_isUsed = 0;
            ref->DigestMethod.ANY_isUsed = 0;
        }

        exi_bitstream_init(&stream, exi_buffer.data(), MAX_EXI_SIZE, 0, nullptr);
        if (encode_iso2_xmldsigFragment(&stream, sig_fragment.get()) != 0) {
            logf_error("PnC: failed to encode SignedInfo fragment");
            return false;
        }
        if (not sha256(exi_buffer.data(), exi_bitstream_get_length(&stream), si_digest)) {
            return false;
        }
    }

    const bool ok = ecdsa_verify(pkey.get(), signature.SignatureValue.CONTENT.bytes,
                                 signature.SignatureValue.CONTENT.bytesLen, si_digest);
    if (not ok) {
        logf_error("PnC: AuthorizationReq signature verification failed");
    }
    return ok;
}

bool verify_metering_receipt_signature(const std::vector<uint8_t>& request_exi, const std::vector<uint8_t>& leaf_der) {
    if (request_exi.empty() or leaf_der.empty()) {
        return false;
    }

    // Re-decode the raw request to recover the cbv2g iso2 structs (Header.Signature + MeteringReceiptReq).
    exi_bitstream_t in_stream;
    exi_bitstream_init(&in_stream, const_cast<uint8_t*>(request_exi.data()), request_exi.size(), 0, nullptr);
    auto doc = std::make_unique<iso2_exiDocument>();
    if (decode_iso2_exiDocument(&in_stream, doc.get()) != 0) {
        logf_error("PnC: failed to re-decode MeteringReceiptReq for signature verification");
        return false;
    }

    if (doc->V2G_Message.Header.Signature_isUsed == 0) {
        logf_error("PnC: MeteringReceiptReq carries no signature");
        return false;
    }
    if (doc->V2G_Message.Body.MeteringReceiptReq_isUsed == 0) {
        logf_error("PnC: re-decoded message is not a MeteringReceiptReq");
        return false;
    }

    const iso2_SignatureType& signature = doc->V2G_Message.Header.Signature;

    auto leaf = der_to_x509(leaf_der);
    if (leaf == nullptr) {
        return false;
    }
    PKEY_ptr pkey(X509_get_pubkey(leaf.get()), &EVP_PKEY_free);
    if (pkey == nullptr) {
        return false;
    }

    std::array<uint8_t, MAX_EXI_SIZE> exi_buffer{};
    exi_bitstream_t stream;

    // 1) digest over the signed MeteringReceiptReq EXI fragment; compare against Reference[0].DigestValue.
    {
        auto fragment = std::make_unique<iso2_exiFragment>();
        init_iso2_exiFragment(fragment.get());
        fragment->MeteringReceiptReq_isUsed = 1;
        std::memcpy(&fragment->MeteringReceiptReq, &doc->V2G_Message.Body.MeteringReceiptReq,
                    sizeof(fragment->MeteringReceiptReq));

        exi_bitstream_init(&stream, exi_buffer.data(), MAX_EXI_SIZE, 0, nullptr);
        if (encode_iso2_exiFragment(&stream, fragment.get()) != 0) {
            logf_error("PnC: failed to encode MeteringReceiptReq fragment");
            return false;
        }

        std::array<uint8_t, SHA256_LEN> digest{};
        if (not sha256(exi_buffer.data(), exi_bitstream_get_length(&stream), digest)) {
            return false;
        }

        const iso2_ReferenceType& ref = signature.SignedInfo.Reference.array[0];
        if (ref.DigestValue.bytesLen != digest.size() or
            std::memcmp(ref.DigestValue.bytes, digest.data(), digest.size()) != 0) {
            logf_error("PnC: MeteringReceiptReq digest mismatch");
            return false;
        }
    }

    // 2) digest over the SignedInfo xmldsig fragment; ECDSA-verify against the contract public key.
    std::array<uint8_t, SHA256_LEN> si_digest{};
    {
        auto sig_fragment = std::make_unique<iso2_xmldsigFragment>();
        init_iso2_xmldsigFragment(sig_fragment.get());
        sig_fragment->SignedInfo_isUsed = 1;
        sig_fragment->SignedInfo = signature.SignedInfo;

        // [V2G2-771] the optional fields below must not be present in the fragment to be verified.
        sig_fragment->SignedInfo.Id_isUsed = 0;
        sig_fragment->SignedInfo.CanonicalizationMethod.ANY_isUsed = 0;
        sig_fragment->SignedInfo.SignatureMethod.HMACOutputLength_isUsed = 0;
        sig_fragment->SignedInfo.SignatureMethod.ANY_isUsed = 0;
        for (auto* ref = sig_fragment->SignedInfo.Reference.array;
             ref != sig_fragment->SignedInfo.Reference.array + sig_fragment->SignedInfo.Reference.arrayLen; ++ref) {
            ref->Type_isUsed = 0;
            ref->Transforms.Transform.ANY_isUsed = 0;
            ref->Transforms.Transform.XPath_isUsed = 0;
            ref->DigestMethod.ANY_isUsed = 0;
        }

        exi_bitstream_init(&stream, exi_buffer.data(), MAX_EXI_SIZE, 0, nullptr);
        if (encode_iso2_xmldsigFragment(&stream, sig_fragment.get()) != 0) {
            logf_error("PnC: failed to encode SignedInfo fragment");
            return false;
        }
        if (not sha256(exi_buffer.data(), exi_bitstream_get_length(&stream), si_digest)) {
            return false;
        }
    }

    const bool ok = ecdsa_verify(pkey.get(), signature.SignatureValue.CONTENT.bytes,
                                 signature.SignatureValue.CONTENT.bytesLen, si_digest);
    if (not ok) {
        logf_error("PnC: MeteringReceiptReq signature verification failed");
    }
    return ok;
}

namespace {

// SHA-256 the EXI fragment of the single set request body element in `doc`.
bool digest_request_fragment(const iso2_exiDocument& doc, std::array<uint8_t, SHA256_LEN>& out) {
    auto fragment = std::make_unique<iso2_exiFragment>();
    init_iso2_exiFragment(fragment.get());

    const auto& body = doc.V2G_Message.Body;
    if (body.AuthorizationReq_isUsed) {
        fragment->AuthorizationReq_isUsed = 1;
        std::memcpy(&fragment->AuthorizationReq, &body.AuthorizationReq, sizeof(fragment->AuthorizationReq));
    } else if (body.MeteringReceiptReq_isUsed) {
        fragment->MeteringReceiptReq_isUsed = 1;
        std::memcpy(&fragment->MeteringReceiptReq, &body.MeteringReceiptReq, sizeof(fragment->MeteringReceiptReq));
    } else if (body.CertificateInstallationReq_isUsed) {
        fragment->CertificateInstallationReq_isUsed = 1;
        std::memcpy(&fragment->CertificateInstallationReq, &body.CertificateInstallationReq,
                    sizeof(fragment->CertificateInstallationReq));
    } else {
        return false;
    }

    std::array<uint8_t, MAX_EXI_SIZE> buffer{};
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, buffer.data(), MAX_EXI_SIZE, 0, nullptr);
    if (encode_iso2_exiFragment(&stream, fragment.get()) != 0) {
        logf_error("PnC: failed to encode request fragment for signing");
        return false;
    }
    return sha256(buffer.data(), exi_bitstream_get_length(&stream), out);
}

// Build the xmldsig Signature over the single set request body element (already carrying its Id) and
// attach it to the message header, then encode the whole document. Returns the EXI payload or empty.
std::vector<uint8_t> finalize_signed(iso2_exiDocument& doc, const std::string& element_id, const PrivateKey& key) {
    std::array<uint8_t, SHA256_LEN> element_digest{};
    if (not digest_request_fragment(doc, element_digest)) {
        return {};
    }

    // Build SignedInfo with a single Reference to "#<element_id>".
    auto& signature = doc.V2G_Message.Header.Signature;
    init_iso2_SignatureType(&signature);
    auto& signed_info = signature.SignedInfo;
    set_cb_string(signed_info.CanonicalizationMethod.Algorithm, ALGO_CANONICAL_EXI);
    set_cb_string(signed_info.SignatureMethod.Algorithm, ALGO_ECDSA_SHA256);

    signed_info.Reference.arrayLen = 1;
    auto& ref = signed_info.Reference.array[0];
    const std::string uri = "#" + element_id;
    set_cb_string(ref.URI, uri.c_str());
    ref.URI_isUsed = 1;
    ref.Transforms_isUsed = 1;
    set_cb_string(ref.Transforms.Transform.Algorithm, ALGO_CANONICAL_EXI);
    set_cb_string(ref.DigestMethod.Algorithm, ALGO_SHA256);
    std::memcpy(ref.DigestValue.bytes, element_digest.data(), element_digest.size());
    ref.DigestValue.bytesLen = static_cast<uint16_t>(element_digest.size());

    // Digest over the SignedInfo xmldsig fragment (optional fields cleared per [V2G2-771], matching the
    // verifier), then ECDSA-P256 sign it with the signing key.
    std::array<uint8_t, SHA256_LEN> si_digest{};
    {
        auto sig_fragment = std::make_unique<iso2_xmldsigFragment>();
        init_iso2_xmldsigFragment(sig_fragment.get());
        sig_fragment->SignedInfo_isUsed = 1;
        sig_fragment->SignedInfo = signed_info;
        sig_fragment->SignedInfo.Id_isUsed = 0;
        sig_fragment->SignedInfo.CanonicalizationMethod.ANY_isUsed = 0;
        sig_fragment->SignedInfo.SignatureMethod.HMACOutputLength_isUsed = 0;
        sig_fragment->SignedInfo.SignatureMethod.ANY_isUsed = 0;
        for (auto* r = sig_fragment->SignedInfo.Reference.array;
             r != sig_fragment->SignedInfo.Reference.array + sig_fragment->SignedInfo.Reference.arrayLen; ++r) {
            r->Type_isUsed = 0;
            r->Transforms.Transform.ANY_isUsed = 0;
            r->Transforms.Transform.XPath_isUsed = 0;
            r->DigestMethod.ANY_isUsed = 0;
        }

        std::array<uint8_t, MAX_EXI_SIZE> buffer{};
        exi_bitstream_t stream;
        exi_bitstream_init(&stream, buffer.data(), MAX_EXI_SIZE, 0, nullptr);
        if (encode_iso2_xmldsigFragment(&stream, sig_fragment.get()) != 0) {
            logf_error("PnC: failed to encode SignedInfo fragment for signing");
            return {};
        }
        if (not sha256(buffer.data(), exi_bitstream_get_length(&stream), si_digest)) {
            return {};
        }
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
        logf_error("PnC: failed to encode signed message document");
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
    set_cb_string(doc.V2G_Message.Body.AuthorizationReq.Id, id.c_str());
    doc.V2G_Message.Body.AuthorizationReq.Id_isUsed = 1;
    return finalize_signed(doc, id, key);
}

std::vector<uint8_t> serialize_signed(const message_2::MeteringReceiptRequest& req, const PrivateKey& key) {
    iso2_exiDocument doc{};
    message_2::convert(req.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.MeteringReceiptReq);
    message_2::convert(req, doc.V2G_Message.Body.MeteringReceiptReq);
    const std::string id = "id1";
    set_cb_string(doc.V2G_Message.Body.MeteringReceiptReq.Id, id.c_str());
    doc.V2G_Message.Body.MeteringReceiptReq.Id_isUsed = 1;
    return finalize_signed(doc, id, key);
}

std::vector<uint8_t> serialize_signed(const message_2::CertificateInstallationRequest& req, const PrivateKey& key) {
    iso2_exiDocument doc{};
    message_2::convert(req.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.CertificateInstallationReq);
    message_2::convert(req, doc.V2G_Message.Body.CertificateInstallationReq);
    // convert() already sets Id from req.id; reuse it as the signature reference.
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
    std::vector<uint8_t> sa_leaf_der(sa_leaf.bytes, sa_leaf.bytes + sa_leaf.bytesLen);
    std::vector<std::vector<uint8_t>> sa_subs;
    if (res.SAProvisioningCertificateChain.SubCertificates_isUsed) {
        const auto& subs = res.SAProvisioningCertificateChain.SubCertificates.Certificate;
        for (uint16_t i = 0; i < subs.arrayLen; ++i) {
            sa_subs.emplace_back(subs.array[i].bytes, subs.array[i].bytes + subs.array[i].bytesLen);
        }
    }

    auto leaf = der_to_x509(sa_leaf_der);
    if (leaf == nullptr) {
        logf_error("PnC: failed to parse SAProvisioningCertificate leaf");
        return false;
    }

    // Chain-verify the SA provisioning leaf against the V2G root.
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
        bool any_root = store != nullptr and not v2g_root_path.empty() and
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

    // Digest over the SignedInfo xmldsig fragment (optional fields cleared per [V2G2-771]).
    std::array<uint8_t, MAX_EXI_SIZE> exi_buffer{};
    exi_bitstream_t stream;
    std::array<uint8_t, SHA256_LEN> si_digest{};
    {
        auto sig_fragment = std::make_unique<iso2_xmldsigFragment>();
        init_iso2_xmldsigFragment(sig_fragment.get());
        sig_fragment->SignedInfo_isUsed = 1;
        sig_fragment->SignedInfo = signature.SignedInfo;
        sig_fragment->SignedInfo.Id_isUsed = 0;
        sig_fragment->SignedInfo.CanonicalizationMethod.ANY_isUsed = 0;
        sig_fragment->SignedInfo.SignatureMethod.HMACOutputLength_isUsed = 0;
        sig_fragment->SignedInfo.SignatureMethod.ANY_isUsed = 0;
        for (auto* r = sig_fragment->SignedInfo.Reference.array;
             r != sig_fragment->SignedInfo.Reference.array + sig_fragment->SignedInfo.Reference.arrayLen; ++r) {
            r->Type_isUsed = 0;
            r->Transforms.Transform.ANY_isUsed = 0;
            r->Transforms.Transform.XPath_isUsed = 0;
            r->DigestMethod.ANY_isUsed = 0;
        }
        exi_bitstream_init(&stream, exi_buffer.data(), MAX_EXI_SIZE, 0, nullptr);
        if (encode_iso2_xmldsigFragment(&stream, sig_fragment.get()) != 0) {
            return false;
        }
        if (not sha256(exi_buffer.data(), exi_bitstream_get_length(&stream), si_digest)) {
            return false;
        }
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

    // Build the peer (sender ephemeral) public key on prime256v1 from the uncompressed point.
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

    // ECDH shared secret Z.
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

    // ConcatKDF-SHA256, OtherInfo = AlgorithmID(0x01) || PartyU(0x55) || PartyV(0x56), 16-byte session key.
    const std::vector<uint8_t> other_info{0x01, 0x55, 0x56};
    const auto session_key = concat_kdf_sha256(shared_secret, other_info, 16);
    if (session_key.size() != 16) {
        return {};
    }

    // AES-128-CBC decrypt (no padding: the plaintext is exactly the 32-byte private scalar).
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

std::string emaid_from_contract_der(const std::vector<uint8_t>& leaf_der) {
    auto leaf = der_to_x509(leaf_der);
    if (leaf == nullptr) {
        return {};
    }
    return strip_dashes(emaid_from_cert(leaf.get()));
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
    ERR_clear_error(); // the loop terminates on a benign "no start line" PEM error
    return out;
}

message_2::RootCertificateId root_cert_id_from_der(const std::vector<uint8_t>& root_der) {
    message_2::RootCertificateId id;
    auto root = der_to_x509(root_der);
    if (root == nullptr) {
        return id;
    }
    char* issuer = X509_NAME_oneline(X509_get_issuer_name(root.get()), nullptr, 0);
    if (issuer != nullptr) {
        id.issuer_name = issuer;
        OPENSSL_free(issuer);
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

std::string der_chain_to_pem(const std::vector<uint8_t>& leaf_der, const std::vector<std::vector<uint8_t>>& subs_der) {
    std::string pem;
    auto append = [&pem](const std::vector<uint8_t>& der) {
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

} // namespace iso15118::d2::crypto
