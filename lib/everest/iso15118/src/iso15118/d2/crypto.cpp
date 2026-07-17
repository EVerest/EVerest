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

#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

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

} // namespace

ContractValidationResult validate_contract_chain(const std::vector<uint8_t>& leaf_der,
                                                  const std::vector<std::vector<uint8_t>>& sub_certs,
                                                  const std::string& req_emaid, const std::string& mo_root_path,
                                                  const std::string& v2g_root_path) {
    ContractValidationResult result;

    if (leaf_der.empty()) {
        logf_error("PnC: no contract certificate received");
        result.response_code = dt::ResponseCode::FAILED_CertChainError;
        return result;
    }

    auto leaf = der_to_x509(leaf_der);
    if (leaf == nullptr) {
        logf_error("PnC: failed to parse contract leaf certificate");
        result.response_code = dt::ResponseCode::FAILED_CertChainError;
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
            result.response_code = dt::ResponseCode::FAILED_CertChainError;
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
            result.response_code = dt::ResponseCode::FAILED_CertChainError;
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

    if (store == nullptr or not any_root) {
        logf_error("PnC: no MO/V2G root available to validate the contract chain");
        sk_X509_free(untrusted);
        if (store != nullptr) {
            X509_STORE_free(store);
        }
        result.response_code = dt::ResponseCode::FAILED_NoCertificateAvailable;
        return result;
    }

    X509_STORE_CTX* ctx = X509_STORE_CTX_new();
    result.response_code = dt::ResponseCode::FAILED_CertChainError;
    if (ctx != nullptr and X509_STORE_CTX_init(ctx, store, leaf.get(), untrusted) == 1) {
        const int rc = X509_verify_cert(ctx);
        if (rc == 1) {
            result.response_code = dt::ResponseCode::OK;
        } else {
            const int err = X509_STORE_CTX_get_error(ctx);
            logf_error("PnC: contract chain verification failed: %s", X509_verify_cert_error_string(err));
            switch (err) {
            case X509_V_ERR_CERT_HAS_EXPIRED:
            case X509_V_ERR_CERT_NOT_YET_VALID:
                result.response_code = dt::ResponseCode::FAILED_CertificateExpired;
                break;
            case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
            case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
            case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
                result.response_code = dt::ResponseCode::FAILED_CertChainError;
                break;
            default:
                result.response_code = dt::ResponseCode::FAILED_CertChainError;
                break;
            }
        }
    }

    if (ctx != nullptr) {
        X509_STORE_CTX_free(ctx);
    }
    X509_STORE_free(store);
    sk_X509_free(untrusted);

    if (result.response_code == dt::ResponseCode::OK) {
        result.emaid = strip_dashes(cert_emaid);
        result.chain_pem = cert_to_pem(leaf.get());
        for (const auto& x : sub_x509) {
            result.chain_pem += cert_to_pem(x.get());
        }
    }

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

bool verify_metering_receipt_signature(const std::vector<uint8_t>& request_exi,
                                       const std::vector<uint8_t>& leaf_der) {
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

} // namespace iso15118::d2::crypto
