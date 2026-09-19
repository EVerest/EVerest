// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/d20/crypto.hpp>

#include <array>
#include <cstring>
#include <ctime>
#include <memory>
#include <set>

#include <iso15118/detail/helper.hpp>

#include <cbv2g/iso_20/iso20_CommonMessages_Datatypes.h>
#include <cbv2g/iso_20/iso20_CommonMessages_Decoder.h>
#include <cbv2g/iso_20/iso20_CommonMessages_Encoder.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include <iso15118/detail/x509_helper.hpp>

namespace iso15118::d20::crypto {

using x509::cert_to_pem;
using x509::der_to_x509;
using x509::PKEY_ptr;
using x509::strip_dashes;
using x509::X509_ptr;

namespace {

constexpr std::size_t MAX_EXI_SIZE = 16384;
constexpr std::size_t SHA512_LEN = 64;
constexpr std::size_t ED448_SIG_LEN = 114;
constexpr int EXPIRES_SOON_DAYS = 14;

// xmldsig algorithm identifiers ([V2G20-765/766], [V2G20-2473..2476]).
constexpr char ALGO_CANONICAL_EXI[] = "http://www.w3.org/TR/canonical-exi/";
constexpr char ALGO_ECDSA_SHA512[] = "http://www.w3.org/2001/04/xmldsig-more#ecdsa-sha512";
constexpr char ALGO_ED448[] = "urn:iso:std:iso:15118:-20:Security:xmldsig#Ed448";
constexpr char ALGO_SHA512[] = "http://www.w3.org/2001/04/xmlenc#sha512";
constexpr char ALGO_SHAKE256[] = "urn:iso:std:iso:15118:-20:Security:xmlenc#SHAKE256";

template <typename CbStringField> std::string cb_string(const CbStringField& field) {
    return std::string(field.characters, field.charactersLen);
}

template <typename CbStringField> void set_cb_string(CbStringField& field, const std::string& value) {
    std::memcpy(field.characters, value.data(), value.size());
    field.charactersLen = static_cast<uint16_t>(value.size());
}

enum class KeyAlgorithm {
    Secp521r1,
    Ed448,
    Other,
};

KeyAlgorithm key_algorithm(EVP_PKEY* pkey) {
    if (pkey == nullptr) {
        return KeyAlgorithm::Other;
    }
    const int base = EVP_PKEY_base_id(pkey);
    if (base == EVP_PKEY_ED448) {
        return KeyAlgorithm::Ed448;
    }
    if (base != EVP_PKEY_EC) {
        return KeyAlgorithm::Other;
    }
    char name[64] = {0};
    std::size_t len = 0;
    if (EVP_PKEY_get_group_name(pkey, name, sizeof(name), &len) != 1) {
        return KeyAlgorithm::Other;
    }
    const std::string group(name, len);
    return (group == "secp521r1" or group == "P-521") ? KeyAlgorithm::Secp521r1 : KeyAlgorithm::Other;
}

bool sha512(const uint8_t* data, std::size_t len, std::vector<uint8_t>& out) {
    out.assign(SHA512_LEN, 0);
    unsigned int md_len = 0;
    return EVP_Digest(data, len, out.data(), &md_len, EVP_sha512(), nullptr) == 1 and md_len == SHA512_LEN;
}

bool shake256(const uint8_t* data, std::size_t len, std::size_t out_len, std::vector<uint8_t>& out) {
    out.assign(out_len, 0);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    const bool ok = ctx != nullptr and EVP_DigestInit_ex(ctx, EVP_shake256(), nullptr) == 1 and
                    EVP_DigestUpdate(ctx, data, len) == 1 and EVP_DigestFinalXOF(ctx, out.data(), out_len) == 1;
    EVP_MD_CTX_free(ctx);
    return ok;
}

// [V2G20-1000]: r and s at the key's coordinate length, zero padded, concatenated.
bool ecdsa_verify_rs(EVP_PKEY* pkey, const uint8_t* sig_rs, std::size_t sig_len, const std::vector<uint8_t>& digest) {
    if (sig_len == 0 or sig_len % 2 != 0) {
        return false;
    }
    const int half = static_cast<int>(sig_len / 2);
    ECDSA_SIG* sig = ECDSA_SIG_new();
    if (sig == nullptr) {
        return false;
    }
    BIGNUM* r = BN_bin2bn(sig_rs, half, nullptr);
    BIGNUM* s = BN_bin2bn(sig_rs + half, half, nullptr);
    if (r == nullptr or s == nullptr or ECDSA_SIG_set0(sig, r, s) != 1) {
        BN_free(r);
        BN_free(s);
        ECDSA_SIG_free(sig);
        return false;
    }
    unsigned char* der = nullptr;
    const int der_len = i2d_ECDSA_SIG(sig, &der);
    ECDSA_SIG_free(sig);
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
    return ok;
}

std::vector<uint8_t> ecdsa_sign_rs(EVP_PKEY* pkey, const std::vector<uint8_t>& digest) {
    std::vector<uint8_t> out;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (ctx == nullptr or EVP_PKEY_sign_init(ctx) != 1) {
        EVP_PKEY_CTX_free(ctx);
        return out;
    }
    size_t der_len = 0;
    if (EVP_PKEY_sign(ctx, nullptr, &der_len, digest.data(), digest.size()) != 1) {
        EVP_PKEY_CTX_free(ctx);
        return out;
    }
    std::vector<uint8_t> der(der_len);
    if (EVP_PKEY_sign(ctx, der.data(), &der_len, digest.data(), digest.size()) != 1) {
        EVP_PKEY_CTX_free(ctx);
        return out;
    }
    EVP_PKEY_CTX_free(ctx);
    der.resize(der_len);

    const unsigned char* p = der.data();
    ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(der.size()));
    if (sig == nullptr) {
        return out;
    }
    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(sig, &r, &s);
    const int coord_len = (EVP_PKEY_bits(pkey) + 7) / 8;
    out.assign(static_cast<std::size_t>(2 * coord_len), 0);
    if (BN_bn2binpad(r, out.data(), coord_len) != coord_len or
        BN_bn2binpad(s, out.data() + coord_len, coord_len) != coord_len) {
        out.clear();
    }
    ECDSA_SIG_free(sig);
    return out;
}

bool ed448_verify(EVP_PKEY* pkey, const uint8_t* sig, std::size_t sig_len, const uint8_t* msg, std::size_t msg_len) {
    if (sig_len != ED448_SIG_LEN) {
        return false;
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    const bool ok = ctx != nullptr and EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) == 1 and
                    EVP_DigestVerify(ctx, sig, sig_len, msg, msg_len) == 1;
    EVP_MD_CTX_free(ctx);
    return ok;
}

std::vector<uint8_t> ed448_sign(EVP_PKEY* pkey, const uint8_t* msg, std::size_t msg_len) {
    std::vector<uint8_t> out(ED448_SIG_LEN);
    size_t sig_len = out.size();
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    const bool ok = ctx != nullptr and EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey) == 1 and
                    EVP_DigestSign(ctx, out.data(), &sig_len, msg, msg_len) == 1 and sig_len == ED448_SIG_LEN;
    EVP_MD_CTX_free(ctx);
    if (not ok) {
        out.clear();
    }
    return out;
}

PKEY_ptr load_private_key(const PrivateKey& key) {
    BIO* bio = BIO_new_mem_buf(key.pem.data(), static_cast<int>(key.pem.size()));
    if (bio == nullptr) {
        return PKEY_ptr(nullptr, &EVP_PKEY_free);
    }
    void* pw = key.password ? const_cast<char*>(key.password->c_str()) : nullptr;
    PKEY_ptr pkey(PEM_read_bio_PrivateKey(bio, nullptr, nullptr, pw), &EVP_PKEY_free);
    BIO_free(bio);
    if (pkey == nullptr) {
        logf_error("PnC: failed to load the private key from PEM");
    }
    return pkey;
}

struct Fragment {
    std::array<uint8_t, MAX_EXI_SIZE> buffer{};
    std::size_t length{0};
};

// The signed element as an EXI fragment ([V2G20-119], [V2G20-1449]). A header signature never covers
// itself, so it is absent from the CertificateInstallationReq fragment ([V2G20-1548], Table 17).
bool encode_signed_element(const iso20_exiDocument& doc, SignedElement element, Fragment& out) {
    auto fragment = std::make_unique<iso20_exiFragment>();
    init_iso20_exiFragment(fragment.get());
    switch (element) {
    case SignedElement::PnC_AReqAuthorizationMode:
        if (not doc.AuthorizationReq_isUsed or not doc.AuthorizationReq.PnC_AReqAuthorizationMode_isUsed) {
            return false;
        }
        fragment->PnC_AReqAuthorizationMode_isUsed = 1;
        fragment->PnC_AReqAuthorizationMode = doc.AuthorizationReq.PnC_AReqAuthorizationMode;
        break;
    case SignedElement::CertificateInstallationReq:
        if (not doc.CertificateInstallationReq_isUsed) {
            return false;
        }
        fragment->CertificateInstallationReq_isUsed = 1;
        fragment->CertificateInstallationReq = doc.CertificateInstallationReq;
        fragment->CertificateInstallationReq.Header.Signature_isUsed = 0;
        break;
    }
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, out.buffer.data(), out.buffer.size(), 0, nullptr);
    if (encode_iso20_exiFragment(&stream, fragment.get()) != 0) {
        logf_error("PnC: failed to encode the signed element fragment");
        return false;
    }
    out.length = exi_bitstream_get_length(&stream);
    return true;
}

// [V2G20-771]: the optional xmldsig fields never appear in the SignedInfo that is digested.
bool encode_signed_info(const iso20_SignedInfoType& signed_info, Fragment& out) {
    auto fragment = std::make_unique<iso20_xmldsigFragment>();
    init_iso20_xmldsigFragment(fragment.get());
    fragment->SignedInfo_isUsed = 1;
    fragment->SignedInfo = signed_info;
    fragment->SignedInfo.Id_isUsed = 0;
    fragment->SignedInfo.CanonicalizationMethod.ANY_isUsed = 0;
    fragment->SignedInfo.SignatureMethod.HMACOutputLength_isUsed = 0;
    fragment->SignedInfo.SignatureMethod.ANY_isUsed = 0;
    for (auto* ref = fragment->SignedInfo.Reference.array;
         ref != fragment->SignedInfo.Reference.array + fragment->SignedInfo.Reference.arrayLen; ++ref) {
        ref->Type_isUsed = 0;
        ref->Transforms.Transform.ANY_isUsed = 0;
        ref->Transforms.Transform.XPath_isUsed = 0;
        ref->DigestMethod.ANY_isUsed = 0;
    }
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, out.buffer.data(), out.buffer.size(), 0, nullptr);
    if (encode_iso20_xmldsigFragment(&stream, fragment.get()) != 0) {
        logf_error("PnC: failed to encode the SignedInfo fragment");
        return false;
    }
    out.length = exi_bitstream_get_length(&stream);
    return true;
}

const iso20_MessageHeaderType* header_of(const iso20_exiDocument& doc, SignedElement element) {
    switch (element) {
    case SignedElement::PnC_AReqAuthorizationMode:
        return doc.AuthorizationReq_isUsed ? &doc.AuthorizationReq.Header : nullptr;
    case SignedElement::CertificateInstallationReq:
        return doc.CertificateInstallationReq_isUsed ? &doc.CertificateInstallationReq.Header : nullptr;
    }
    return nullptr;
}

iso20_MessageHeaderType* header_of(iso20_exiDocument& doc, SignedElement element) {
    return const_cast<iso20_MessageHeaderType*>(header_of(static_cast<const iso20_exiDocument&>(doc), element));
}

bool decode_document(const std::vector<uint8_t>& exi, iso20_exiDocument& doc) {
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, const_cast<uint8_t*>(exi.data()), exi.size(), 0, nullptr);
    return decode_iso20_exiDocument(&stream, &doc) == 0;
}

enum class ChainRole {
    ContractLeaf,
    SubCa,
};

bool extension_is_critical(X509* cert, int nid) {
    const int idx = X509_get_ext_by_NID(cert, nid, -1);
    if (idx < 0) {
        return false;
    }
    X509_EXTENSION* ext = X509_get_ext(cert, idx);
    return ext != nullptr and X509_EXTENSION_get_critical(ext) == 1;
}

bool has_extension(X509* cert, int nid) {
    return X509_get_ext_by_NID(cert, nid, -1) >= 0;
}

// Contract certificate profile of Table B.9 / B.10, including [V2G20-2686]: no extension outside it.
// Returns the failed check, empty when the certificate conforms.
std::string profile_fault(X509* cert, ChainRole role, int sub_ca_index) {
    if (X509_get_version(cert) != 2) {
        return "not an X.509v3 certificate";
    }
    const auto algorithm = key_algorithm(X509_get0_pubkey(cert));
    const int signature_nid = X509_get_signature_nid(cert);
    if (algorithm == KeyAlgorithm::Secp521r1) {
        if (signature_nid != NID_ecdsa_with_SHA512) {
            return "signature algorithm is not ecdsa-with-SHA512";
        }
    } else if (algorithm == KeyAlgorithm::Ed448) {
        if (signature_nid != NID_ED448) {
            return "signature algorithm is not Ed448";
        }
    } else {
        return "public key is neither secp521r1 nor Ed448";
    }

    if (not(X509_get_extension_flags(cert) & EXFLAG_KUSAGE) or not extension_is_critical(cert, NID_key_usage)) {
        return "missing critical KeyUsage";
    }
    const uint32_t key_usage = X509_get_key_usage(cert);
    if (not has_extension(cert, NID_basic_constraints) or not extension_is_critical(cert, NID_basic_constraints)) {
        return "missing critical BasicConstraints";
    }
    const bool is_ca = X509_check_ca(cert) > 0;

    if (role == ChainRole::SubCa) {
        if (not is_ca) {
            return "sub-CA without CA:TRUE";
        }
        if (not(key_usage & KU_KEY_CERT_SIGN) or not(key_usage & KU_CRL_SIGN)) {
            return "sub-CA KeyUsage without keyCertSign and cRLSign";
        }
        // Table B.9: sub-CA2 (nearest the leaf) has pathLen 0, sub-CA1 has 1. A lone sub-CA is a
        // sub-CA2 [V2G20-911].
        const long path_len = X509_get_pathlen(cert);
        if (path_len != sub_ca_index) {
            return "sub-CA pathLenConstraint does not match its position";
        }
    } else {
        if (is_ca) {
            return "leaf with CA:TRUE";
        }
        if (not(key_usage & KU_DIGITAL_SIGNATURE)) {
            return "leaf KeyUsage without digitalSignature";
        }
        if (key_usage & (KU_KEY_CERT_SIGN | KU_CRL_SIGN | KU_DATA_ENCIPHERMENT | KU_ENCIPHER_ONLY | KU_DECIPHER_ONLY)) {
            return "leaf KeyUsage with a forbidden bit";
        }
        if (has_extension(cert, NID_ext_key_usage)) {
            return "leaf with ExtendedKeyUsage";
        }
    }

    static const std::set<int> common_extensions = {
        NID_authority_key_identifier, NID_subject_key_identifier,  NID_key_usage,  NID_basic_constraints,
        NID_certificate_policies,     NID_crl_distribution_points, NID_info_access};
    const int count = X509_get_ext_count(cert);
    for (int i = 0; i < count; ++i) {
        X509_EXTENSION* ext = X509_get_ext(cert, i);
        const int nid = OBJ_obj2nid(X509_EXTENSION_get_object(ext));
        const bool leaf_only = (nid == NID_sinfo_access) and role != ChainRole::SubCa;
        if (common_extensions.count(nid) == 0 and not leaf_only) {
            return "extension outside the certificate profile";
        }
    }
    // [V2G20-2590]: revocation information via CRL or OCSP.
    if (not has_extension(cert, NID_crl_distribution_points) and not has_extension(cert, NID_info_access)) {
        return "neither CRLDistributionPoints nor AuthorityInfoAccess";
    }
    if (role == ChainRole::ContractLeaf) {
        // [V2G20-2589]: CN carries the EMAID, O the eMSP; Table B.9 additionally allows C, OU and DC.
        bool has_cn = false;
        bool has_o = false;
        X509_NAME* subject = X509_get_subject_name(cert);
        for (int i = 0; i < X509_NAME_entry_count(subject); ++i) {
            const int nid = OBJ_obj2nid(X509_NAME_ENTRY_get_object(X509_NAME_get_entry(subject, i)));
            if (nid == NID_commonName) {
                has_cn = true;
            } else if (nid == NID_organizationName) {
                has_o = true;
            } else if (nid != NID_countryName and nid != NID_organizationalUnitName and nid != NID_domainComponent) {
                return "subject attribute outside the contract certificate profile";
            }
        }
        if (not has_cn or not has_o) {
            return "subject without CN and O";
        }
    }
    return {};
}

// Annex B marks the key identifiers and revocation pointers critical, which IETF RFC 5280 does not, so
// OpenSSL rejects a conforming contract certificate. Accept those, keep anything else fatal.
int verify_annex_b_extensions(int preverified, X509_STORE_CTX* ctx) {
    if (preverified or X509_STORE_CTX_get_error(ctx) != X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION) {
        return preverified;
    }
    X509* cert = X509_STORE_CTX_get_current_cert(ctx);
    for (int i = 0; i < X509_get_ext_count(cert); ++i) {
        X509_EXTENSION* ext = X509_get_ext(cert, i);
        if (not X509_EXTENSION_get_critical(ext) or X509_supported_extension(ext)) {
            continue;
        }
        const int nid = OBJ_obj2nid(X509_EXTENSION_get_object(ext));
        if (nid != NID_authority_key_identifier and nid != NID_subject_key_identifier and nid != NID_info_access and
            nid != NID_crl_distribution_points and nid != NID_sinfo_access) {
            return 0;
        }
        const X509V3_EXT_METHOD* method = X509V3_EXT_get(ext);
        void* decoded = X509V3_EXT_d2i(ext);
        if (decoded == nullptr or method == nullptr) {
            return 0;
        }
        if (method->it) {
            ASN1_item_free(static_cast<ASN1_VALUE*>(decoded), ASN1_ITEM_ptr(method->it));
        } else {
            method->ext_free(decoded);
        }
    }
    X509_STORE_CTX_set_error(ctx, X509_V_OK);
    return 1;
}

enum class Validity {
    Valid,
    Expired,
    NotYetValid,
};

Validity validity_of(X509* cert) {
    if (X509_cmp_time(X509_get0_notAfter(cert), nullptr) < 0) {
        return Validity::Expired;
    }
    if (X509_cmp_time(X509_get0_notBefore(cert), nullptr) > 0) {
        return Validity::NotYetValid;
    }
    return Validity::Valid;
}

bool expires_within_days(X509* cert, int days) {
    time_t limit = time(nullptr) + static_cast<time_t>(days) * 24 * 60 * 60;
    return X509_cmp_time(X509_get0_notAfter(cert), &limit) < 0;
}

struct ParsedChain {
    X509_ptr leaf{nullptr, &X509_free};
    std::vector<X509_ptr> subs;
};

bool parse_chain(const std::vector<uint8_t>& leaf_der, const std::vector<std::vector<uint8_t>>& sub_certs,
                 ParsedChain& out) {
    out.leaf = der_to_x509(leaf_der);
    if (out.leaf == nullptr) {
        logf_error("PnC: failed to parse the leaf certificate");
        return false;
    }
    for (const auto& der : sub_certs) {
        auto sub = der_to_x509(der);
        if (sub == nullptr) {
            logf_error("PnC: failed to parse a sub-CA certificate");
            return false;
        }
        out.subs.push_back(std::move(sub));
    }
    return true;
}

// Expired anywhere in the chain wins over not-yet-valid ([V2G20-2212] before [V2G20-2213]).
std::optional<dt::ResponseCode> validity_fault(const ParsedChain& chain) {
    bool not_yet_valid = false;
    auto check = [&](X509* cert) {
        switch (validity_of(cert)) {
        case Validity::Expired:
            return true;
        case Validity::NotYetValid:
            not_yet_valid = true;
            return false;
        case Validity::Valid:
            return false;
        }
        return false;
    };
    bool expired = check(chain.leaf.get());
    for (const auto& sub : chain.subs) {
        expired = check(sub.get()) or expired;
    }
    if (expired) {
        return dt::ResponseCode::WARNING_CertificateExpired;
    }
    if (not_yet_valid) {
        return dt::ResponseCode::WARNING_CertificateNotYetValid;
    }
    return std::nullopt;
}

bool chain_matches_profile(const ParsedChain& chain, ChainRole leaf_role) {
    const auto leaf_fault = profile_fault(chain.leaf.get(), leaf_role, 0);
    if (not leaf_fault.empty()) {
        logf_error("PnC: leaf certificate violates the Annex B profile: %s", leaf_fault.c_str());
        return false;
    }
    for (std::size_t i = 0; i < chain.subs.size(); ++i) {
        const auto fault = profile_fault(chain.subs[i].get(), ChainRole::SubCa, static_cast<int>(i));
        if (not fault.empty()) {
            logf_error("PnC: sub-CA certificate %zu violates the Annex B profile: %s", i, fault.c_str());
            return false;
        }
    }
    return true;
}

} // namespace

ContractValidationResult validate_contract_chain(const std::vector<uint8_t>& leaf_der,
                                                 const std::vector<std::vector<uint8_t>>& sub_certs,
                                                 const std::string& mo_root_path, const std::string& v2g_root_path) {
    ContractValidationResult result;
    ParsedChain chain;
    if (leaf_der.empty() or not parse_chain(leaf_der, sub_certs, chain)) {
        ERR_clear_error();
        return result;
    }

    result.emaid = strip_dashes(x509::subject_common_name(chain.leaf.get()));
    result.chain_pem = cert_to_pem(chain.leaf.get());
    for (const auto& sub : chain.subs) {
        result.chain_pem += cert_to_pem(sub.get());
    }
    result.expires_within_14_days = expires_within_days(chain.leaf.get(), EXPIRES_SOON_DAYS);

    if (const auto fault = validity_fault(chain)) {
        result.response_code = *fault;
        ERR_clear_error();
        return result;
    }
    if (not chain_matches_profile(chain, ChainRole::ContractLeaf)) {
        result.response_code = dt::ResponseCode::WARNING_CertificateValidationError;
        ERR_clear_error();
        return result;
    }

    // Trust anchors: an eMSP root in the MO bundle or a V2G root [V2G20-2329].
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
    STACK_OF(X509)* untrusted = sk_X509_new_null();
    for (const auto& sub : chain.subs) {
        sk_X509_push(untrusted, sub.get());
    }

    if (store == nullptr or not any_root or untrusted == nullptr) {
        logf_error("PnC: no MO/V2G root available to validate the contract chain");
        result.response_code = dt::ResponseCode::WARNING_eMSPUnknown;
        result.forwardable = true;
    } else {
        X509_STORE_CTX* ctx = X509_STORE_CTX_new();
        result.response_code = dt::ResponseCode::WARNING_CertificateValidationError;
        if (ctx != nullptr and X509_STORE_CTX_init(ctx, store, chain.leaf.get(), untrusted) == 1) {
            X509_STORE_CTX_set_verify_cb(ctx, verify_annex_b_extensions);
            if (X509_verify_cert(ctx) == 1) {
                result.response_code = dt::ResponseCode::OK;
            } else {
                const int err = X509_STORE_CTX_get_error(ctx);
                logf_error("PnC: contract chain verification failed at depth %d: %s",
                           X509_STORE_CTX_get_error_depth(ctx), X509_verify_cert_error_string(err));
                switch (err) {
                case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
                case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
                case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
                    // [V2G20-2211]: the eMSP is not known here; the backend may still know it.
                    result.response_code = dt::ResponseCode::WARNING_eMSPUnknown;
                    result.forwardable = true;
                    break;
                case X509_V_ERR_CERT_REVOKED:
                    result.response_code = dt::ResponseCode::WARNING_CertificateRevoked;
                    break;
                default:
                    break;
                }
            }
        }
        if (ctx != nullptr) {
            X509_STORE_CTX_free(ctx);
        }
    }
    if (untrusted != nullptr) {
        sk_X509_free(untrusted);
    }
    if (store != nullptr) {
        X509_STORE_free(store);
    }
    // The error queue is shared with the TLS connection; a stale entry would fail the next SSL_read.
    ERR_clear_error();
    return result;
}

std::vector<uint8_t> authorization_request_without_timestamp(const std::vector<uint8_t>& exi) {
    auto doc = std::make_unique<iso20_exiDocument>();
    if (not decode_document(exi, *doc) or not doc->AuthorizationReq_isUsed) {
        return {};
    }
    doc->AuthorizationReq.Header.TimeStamp = 0;
    std::vector<uint8_t> result(MAX_EXI_SIZE);
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, result.data(), result.size(), 0, nullptr);
    if (encode_iso20_exiDocument(&stream, doc.get()) != 0) {
        return {};
    }
    result.resize(exi_bitstream_get_length(&stream));
    return result;
}

SignatureVerdict verify_signature(const std::vector<uint8_t>& request_exi, const std::vector<uint8_t>& leaf_der,
                                  SignedElement element) {
    auto verdict = SignatureVerdict::DecodeError;
    auto doc = std::make_unique<iso20_exiDocument>();
    if (request_exi.empty() or not decode_document(request_exi, *doc)) {
        logf_error("PnC: failed to re-decode the request for signature verification");
        return verdict;
    }
    const auto* header = header_of(*doc, element);
    if (header == nullptr) {
        return verdict;
    }
    if (header->Signature_isUsed == 0) {
        logf_error("PnC: request carries no header signature");
        return SignatureVerdict::NoSignature;
    }
    const iso20_SignatureType& signature = header->Signature;
    const auto& signed_info = signature.SignedInfo;

    auto leaf = der_to_x509(leaf_der);
    PKEY_ptr pkey(leaf != nullptr ? X509_get_pubkey(leaf.get()) : nullptr, &EVP_PKEY_free);
    if (pkey == nullptr) {
        logf_error("PnC: failed to read the public key of the signing certificate");
        ERR_clear_error();
        return verdict;
    }

    const std::string signature_method = cb_string(signed_info.SignatureMethod.Algorithm);
    const bool ecdsa = signature_method == ALGO_ECDSA_SHA512;
    const bool ed448 = signature_method == ALGO_ED448;
    if (signed_info.Reference.arrayLen != 1 or (not ecdsa and not ed448)) {
        logf_error("PnC: unsupported SignatureMethod '%s' or reference count %u", signature_method.c_str(),
                   signed_info.Reference.arrayLen);
        return SignatureVerdict::UnsupportedAlgorithm;
    }
    const auto& reference = signed_info.Reference.array[0];
    const std::string digest_method = cb_string(reference.DigestMethod.Algorithm);
    if (cb_string(signed_info.CanonicalizationMethod.Algorithm) != ALGO_CANONICAL_EXI or
        (ecdsa and digest_method != ALGO_SHA512) or (ed448 and digest_method != ALGO_SHAKE256) or
        reference.DigestValue.bytesLen != SHA512_LEN) {
        return SignatureVerdict::UnsupportedAlgorithm;
    }

    auto fragment = std::make_unique<Fragment>();
    if (not encode_signed_element(*doc, element, *fragment)) {
        return verdict;
    }
    std::vector<uint8_t> digest;
    if (digest_method == ALGO_SHA512) {
        if (not sha512(fragment->buffer.data(), fragment->length, digest)) {
            return verdict;
        }
    } else if (digest_method == ALGO_SHAKE256) {
        const std::size_t out_len = reference.DigestValue.bytesLen;
        if (out_len == 0 or not shake256(fragment->buffer.data(), fragment->length, out_len, digest)) {
            return verdict;
        }
    } else {
        logf_error("PnC: unsupported DigestMethod '%s'", digest_method.c_str());
        return SignatureVerdict::UnsupportedAlgorithm;
    }
    if (reference.DigestValue.bytesLen != digest.size() or
        std::memcmp(reference.DigestValue.bytes, digest.data(), digest.size()) != 0) {
        logf_error("PnC: reference digest does not match the signed element");
        return SignatureVerdict::DigestMismatch;
    }

    if (not encode_signed_info(signed_info, *fragment)) {
        return verdict;
    }
    bool ok = false;
    if (ecdsa) {
        std::vector<uint8_t> si_digest;
        ok = sha512(fragment->buffer.data(), fragment->length, si_digest) and
             ecdsa_verify_rs(pkey.get(), signature.SignatureValue.CONTENT.bytes,
                             signature.SignatureValue.CONTENT.bytesLen, si_digest);
    } else {
        ok = ed448_verify(pkey.get(), signature.SignatureValue.CONTENT.bytes, signature.SignatureValue.CONTENT.bytesLen,
                          fragment->buffer.data(), fragment->length);
    }
    ERR_clear_error();
    if (not ok) {
        logf_error("PnC: signature verification failed");
        return SignatureVerdict::SignatureInvalid;
    }
    return SignatureVerdict::Ok;
}

std::vector<uint8_t> sign_document(const std::vector<uint8_t>& unsigned_request_exi, SignedElement element,
                                   const std::string& reference_id, const PrivateKey& key) {
    auto doc = std::make_unique<iso20_exiDocument>();
    if (not decode_document(unsigned_request_exi, *doc)) {
        return {};
    }
    auto* header = header_of(*doc, element);
    if (header == nullptr) {
        return {};
    }
    auto pkey = load_private_key(key);
    if (pkey == nullptr) {
        ERR_clear_error();
        return {};
    }
    // Any EC key signs ecdsa-sha512; whether its curve meets Annex B is the verifier's profile check.
    const int base = EVP_PKEY_base_id(pkey.get());
    const bool ed448 = base == EVP_PKEY_ED448;
    if (not ed448 and base != EVP_PKEY_EC) {
        logf_error("PnC: the signing key is neither EC nor Ed448");
        return {};
    }

    header->Signature_isUsed = 0;
    auto fragment = std::make_unique<Fragment>();
    if (not encode_signed_element(*doc, element, *fragment)) {
        return {};
    }
    std::vector<uint8_t> digest;
    if (ed448 ? not shake256(fragment->buffer.data(), fragment->length, SHA512_LEN, digest)
              : not sha512(fragment->buffer.data(), fragment->length, digest)) {
        return {};
    }

    auto& signature = header->Signature;
    init_iso20_SignatureType(&signature);
    auto& signed_info = signature.SignedInfo;
    set_cb_string(signed_info.CanonicalizationMethod.Algorithm, ALGO_CANONICAL_EXI);
    set_cb_string(signed_info.SignatureMethod.Algorithm, ed448 ? ALGO_ED448 : ALGO_ECDSA_SHA512);
    signed_info.Reference.arrayLen = 1;
    auto& reference = signed_info.Reference.array[0];
    init_iso20_ReferenceType(&reference);
    reference.URI_isUsed = 1;
    set_cb_string(reference.URI, "#" + reference_id);
    reference.Transforms_isUsed = 1;
    set_cb_string(reference.Transforms.Transform.Algorithm, ALGO_CANONICAL_EXI);
    set_cb_string(reference.DigestMethod.Algorithm, ed448 ? ALGO_SHAKE256 : ALGO_SHA512);
    std::memcpy(reference.DigestValue.bytes, digest.data(), digest.size());
    reference.DigestValue.bytesLen = static_cast<uint16_t>(digest.size());

    if (not encode_signed_info(signed_info, *fragment)) {
        return {};
    }
    std::vector<uint8_t> signature_value;
    if (ed448) {
        signature_value = ed448_sign(pkey.get(), fragment->buffer.data(), fragment->length);
    } else {
        std::vector<uint8_t> si_digest;
        if (sha512(fragment->buffer.data(), fragment->length, si_digest)) {
            signature_value = ecdsa_sign_rs(pkey.get(), si_digest);
        }
    }
    ERR_clear_error();
    if (signature_value.empty() or signature_value.size() > sizeof(signature.SignatureValue.CONTENT.bytes)) {
        logf_error("PnC: failed to sign the SignedInfo");
        return {};
    }
    std::memcpy(signature.SignatureValue.CONTENT.bytes, signature_value.data(), signature_value.size());
    signature.SignatureValue.CONTENT.bytesLen = static_cast<uint16_t>(signature_value.size());
    header->Signature_isUsed = 1;

    exi_bitstream_t stream;
    exi_bitstream_init(&stream, fragment->buffer.data(), fragment->buffer.size(), 0, nullptr);
    if (encode_iso20_exiDocument(&stream, doc.get()) != 0) {
        logf_error("PnC: failed to encode the signed request");
        return {};
    }
    const auto len = exi_bitstream_get_length(&stream);
    return std::vector<uint8_t>(fragment->buffer.data(), fragment->buffer.data() + len);
}

} // namespace iso15118::d20::crypto
