#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>

#include <evse_security/crypto/openssl/openssl_crypto_supplier.hpp>
#include <evse_security/crypto/openssl/openssl_types.hpp>
#include <optional>

#include <openssl/evp.h>
#include <openssl/x509v3.h>

// #define OUTPUT_CSR

using namespace evse_security;

namespace {

static std::string getFile(const std::string name) {
    std::ifstream file(name);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

class OpenSSLSupplierTest : public testing::Test {
protected:
    static void SetUpTestSuite() {
        std::system("./create-pki.sh");
    }
};

TEST_F(OpenSSLSupplierTest, generate_key_RSA_TPM20) {
    KeyGenerationInfo info = {
        CryptoKeyType::RSA_TPM20, false, std::nullopt, std::nullopt, std::nullopt,
    };
    KeyHandle_ptr key;
    auto res = OpenSSLSupplier::generate_key(info, key);
    ASSERT_TRUE(res);
}

TEST_F(OpenSSLSupplierTest, generate_key_RSA_3072) {
    KeyGenerationInfo info = {
        CryptoKeyType::RSA_3072, false, std::nullopt, std::nullopt, std::nullopt,
    };
    KeyHandle_ptr key;
    auto res = OpenSSLSupplier::generate_key(info, key);
    ASSERT_TRUE(res);
}

TEST_F(OpenSSLSupplierTest, generate_key_EC_prime256v1) {
    KeyGenerationInfo info = {
        CryptoKeyType::EC_prime256v1, false, std::nullopt, std::nullopt, std::nullopt,
    };
    KeyHandle_ptr key;
    auto res = OpenSSLSupplier::generate_key(info, key);
    ASSERT_TRUE(res);
}

TEST_F(OpenSSLSupplierTest, generate_key_EC_EC_secp384r1) {
    KeyGenerationInfo info = {
        CryptoKeyType::EC_secp384r1, false, std::nullopt, std::nullopt, std::nullopt,
    };
    KeyHandle_ptr key;
    auto res = OpenSSLSupplier::generate_key(info, key);
    ASSERT_TRUE(res);
}

TEST_F(OpenSSLSupplierTest, load_certificates) {
    auto file = getFile("pki/cert_path.pem");
    auto res = OpenSSLSupplier::load_certificates(file, EncodingFormat::PEM);
    ASSERT_EQ(res.size(), 2);
}

TEST_F(OpenSSLSupplierTest, x509_check_private_key) {
    auto cert_leaf = getFile("pki/server_cert.pem");
    auto res_leaf = OpenSSLSupplier::load_certificates(cert_leaf, EncodingFormat::PEM);
    auto cert = res_leaf[0].get();
    auto key = getFile("pki/server_priv.pem");
    auto res = OpenSSLSupplier::x509_check_private_key(cert, key, std::nullopt);
    ASSERT_TRUE(res == KeyValidationResult::Valid);
}

TEST_F(OpenSSLSupplierTest, x509_verify_certificate_chain) {
    auto cert_path = getFile("pki/cert_path.pem");
    auto cert_leaf = getFile("pki/server_cert.pem");

    auto res_path = OpenSSLSupplier::load_certificates(cert_path, EncodingFormat::PEM);
    auto res_leaf = OpenSSLSupplier::load_certificates(cert_leaf, EncodingFormat::PEM);

    std::vector<X509Handle*> parents;
    std::vector<X509Handle*> empty_untrusted;

    for (auto& i : res_path) {
        parents.push_back(i.get());
    }

    auto res = OpenSSLSupplier::x509_verify_certificate_chain(res_leaf[0].get(), parents, empty_untrusted, true,
                                                              std::nullopt, "pki/root_cert.pem");
    ASSERT_EQ(res, CertificateValidationResult::Valid);
}

TEST_F(OpenSSLSupplierTest, x509_generate_csr) {
    std::string csr;
    CertificateSigningRequestInfo csr_info = {
        0,
        "UK",
        "Pionix",
        "0123456789",
        .dns_name = std::nullopt,
        .ip_address = std::nullopt,
        {CryptoKeyType::EC_prime256v1, false, std::nullopt, "pki/csr_key.pem", std::nullopt}};
    auto res = OpenSSLSupplier::x509_generate_csr(csr_info, csr);
    ASSERT_EQ(res, CertificateSignRequestResult::Valid);

    std::ofstream out("csr.pem");
    out << csr;
    out.close();

    ASSERT_GT(csr.size(), 0);
}

TEST_F(OpenSSLSupplierTest, x509_generate_csr_dns) {
    std::string csr;
    CertificateSigningRequestInfo csr_info = {
        0,
        "UK",
        "Pionix",
        "0123456789",
        .dns_name = "cs.pionix.de",
        .ip_address = std::nullopt,
        {CryptoKeyType::EC_prime256v1, false, std::nullopt, "pki/csr_key.pem", std::nullopt}};
    auto res = OpenSSLSupplier::x509_generate_csr(csr_info, csr);
    ASSERT_EQ(res, CertificateSignRequestResult::Valid);

#ifdef OUTPUT_CSR
    std::ofstream out("csr_dns.pem");
    out << csr;
    out.close();
#endif

    ASSERT_GT(csr.size(), 0);
}

TEST_F(OpenSSLSupplierTest, x509_generate_csr_ip) {
    std::string csr;
    CertificateSigningRequestInfo csr_info = {
        0,
        "UK",
        "Pionix",
        "0123456789",
        .dns_name = std::nullopt,
        .ip_address = "127.0.0.1",
        {CryptoKeyType::EC_prime256v1, false, std::nullopt, "pki/csr_key.pem", std::nullopt}};
    auto res = OpenSSLSupplier::x509_generate_csr(csr_info, csr);
    ASSERT_EQ(res, CertificateSignRequestResult::Valid);

#ifdef OUTPUT_CSR
    std::ofstream out("csr_ip.pem");
    out << csr;
    out.close();
#endif

    ASSERT_GT(csr.size(), 0);
}

TEST_F(OpenSSLSupplierTest, x509_generate_csr_dns_ip) {
    std::string csr;
    CertificateSigningRequestInfo csr_info = {
        0,
        "UK",
        "Pionix",
        "0123456789",
        .dns_name = "cs.pionix.de",
        .ip_address = "127.0.0.1",
        {CryptoKeyType::EC_prime256v1, false, std::nullopt, "pki/csr_key.pem", std::nullopt}};
    auto res = OpenSSLSupplier::x509_generate_csr(csr_info, csr);
    ASSERT_EQ(res, CertificateSignRequestResult::Valid);

#ifdef OUTPUT_CSR
    std::ofstream out("csr_dns_ip.pem");
    out << csr;
    out.close();
#endif

    ASSERT_GT(csr.size(), 0);
}

struct TestCertificate {
    EVP_PKEY_ptr key;
    X509_ptr cert;
};

void add_extension(X509* cert, X509* issuer, int nid, const char* value) {
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, issuer, cert, nullptr, nullptr, 0);
    X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value);
    ASSERT_NE(extension, nullptr);
    X509_add_ext(cert, extension, -1);
    X509_EXTENSION_free(extension);
}

// Valid from now + not_before_days until now + not_after_days, self-signed if issuer is nullptr
TestCertificate make_certificate(const std::string& common_name, const TestCertificate* issuer, long not_before_days,
                                 long not_after_days, bool is_ca) {
    TestCertificate result{EVP_PKEY_ptr(EVP_EC_gen("P-256")), X509_ptr(X509_new())};
    X509* cert = result.cert.get();
    X509* issuer_cert = issuer != nullptr ? issuer->cert.get() : cert;
    EVP_PKEY* issuer_key = issuer != nullptr ? issuer->key.get() : result.key.get();

    X509_set_version(cert, X509_VERSION_3);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_time_adj_ex(X509_getm_notBefore(cert), static_cast<int>(not_before_days), 0, nullptr);
    X509_time_adj_ex(X509_getm_notAfter(cert), static_cast<int>(not_after_days), 0, nullptr);
    X509_NAME_add_entry_by_txt(X509_get_subject_name(cert), "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(common_name.c_str()), -1, -1, 0);
    X509_set_issuer_name(cert, X509_get_subject_name(issuer_cert));
    X509_set_pubkey(cert, result.key.get());
    if (is_ca) {
        add_extension(cert, issuer_cert, NID_basic_constraints, "critical,CA:TRUE");
        add_extension(cert, issuer_cert, NID_key_usage, "critical,keyCertSign,cRLSign");
    }
    X509_sign(cert, issuer_key, EVP_sha256());
    return result;
}

X509Handle_ptr to_handle(const TestCertificate& certificate) {
    return std::make_unique<X509HandleOpenSSL>(X509_dup(certificate.cert.get()));
}

CertificateValidationResult verify_chain(const TestCertificate& leaf, const TestCertificate& intermediate,
                                         const TestCertificate& root, bool allow_future_certificates) {
    const auto leaf_handle = to_handle(leaf);
    const auto intermediate_handle = to_handle(intermediate);
    const auto root_handle = to_handle(root);
    return OpenSSLSupplier::x509_verify_certificate_chain(leaf_handle.get(), {root_handle.get()},
                                                          {intermediate_handle.get()}, allow_future_certificates,
                                                          std::nullopt, std::nullopt);
}

TEST(OpenSSLSupplierValidityTest, valid_chain_accepted) {
    const auto root = make_certificate("Root", nullptr, -1, 3650, true);
    const auto intermediate = make_certificate("Intermediate", &root, -1, 365, true);
    const auto leaf = make_certificate("Leaf", &intermediate, -1, 30, false);

    EXPECT_EQ(verify_chain(leaf, intermediate, root, false), CertificateValidationResult::Valid);
    EXPECT_EQ(verify_chain(leaf, intermediate, root, true), CertificateValidationResult::Valid);
}

TEST(OpenSSLSupplierValidityTest, expired_leaf_rejected) {
    const auto root = make_certificate("Root", nullptr, -1, 3650, true);
    const auto intermediate = make_certificate("Intermediate", &root, -1, 365, true);
    const auto leaf = make_certificate("Leaf", &intermediate, -30, -1, false);

    EXPECT_EQ(verify_chain(leaf, intermediate, root, false), CertificateValidationResult::Expired);
    EXPECT_EQ(verify_chain(leaf, intermediate, root, true), CertificateValidationResult::Expired);
}

TEST(OpenSSLSupplierValidityTest, expired_intermediate_rejected) {
    const auto root = make_certificate("Root", nullptr, -1, 3650, true);
    const auto intermediate = make_certificate("Intermediate", &root, -30, -1, true);
    const auto leaf = make_certificate("Leaf", &intermediate, -1, 30, false);

    EXPECT_EQ(verify_chain(leaf, intermediate, root, false), CertificateValidationResult::Expired);
    EXPECT_EQ(verify_chain(leaf, intermediate, root, true), CertificateValidationResult::Expired);
}

TEST(OpenSSLSupplierValidityTest, expired_root_rejected) {
    const auto root = make_certificate("Root", nullptr, -30, -1, true);
    const auto intermediate = make_certificate("Intermediate", &root, -1, 365, true);
    const auto leaf = make_certificate("Leaf", &intermediate, -1, 30, false);

    EXPECT_EQ(verify_chain(leaf, intermediate, root, false), CertificateValidationResult::Expired);
    EXPECT_EQ(verify_chain(leaf, intermediate, root, true), CertificateValidationResult::Expired);
}

TEST(OpenSSLSupplierValidityTest, future_leaf_accepted_only_when_allowed) {
    const auto root = make_certificate("Root", nullptr, -1, 3650, true);
    const auto intermediate = make_certificate("Intermediate", &root, -1, 365, true);
    const auto leaf = make_certificate("Leaf", &intermediate, 30, 60, false);

    EXPECT_NE(verify_chain(leaf, intermediate, root, false), CertificateValidationResult::Valid);
    EXPECT_EQ(verify_chain(leaf, intermediate, root, true), CertificateValidationResult::Valid);
}

TEST(OpenSSLSupplierValidityTest, future_leaf_with_expired_root_rejected) {
    const auto root = make_certificate("Root", nullptr, -30, -1, true);
    const auto intermediate = make_certificate("Intermediate", &root, -1, 365, true);
    const auto leaf = make_certificate("Leaf", &intermediate, 30, 60, false);

    EXPECT_EQ(verify_chain(leaf, intermediate, root, true), CertificateValidationResult::Expired);
}

TEST(OpenSSLSupplierValidityTest, future_leaf_with_intermediate_expiring_before_leaf_rejected) {
    const auto root = make_certificate("Root", nullptr, -1, 3650, true);
    const auto intermediate = make_certificate("Intermediate", &root, -1, 10, true);
    const auto leaf = make_certificate("Leaf", &intermediate, 30, 60, false);

    EXPECT_EQ(verify_chain(leaf, intermediate, root, true), CertificateValidationResult::Expired);
}

TEST(OpenSSLSupplierValidityTest, future_leaf_with_future_intermediate_accepted) {
    const auto root = make_certificate("Root", nullptr, -1, 3650, true);
    const auto intermediate = make_certificate("Intermediate", &root, 10, 365, true);
    const auto leaf = make_certificate("Leaf", &intermediate, 30, 60, false);

    EXPECT_NE(verify_chain(leaf, intermediate, root, false), CertificateValidationResult::Valid);
    EXPECT_EQ(verify_chain(leaf, intermediate, root, true), CertificateValidationResult::Valid);
}

} // namespace
