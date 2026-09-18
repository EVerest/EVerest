// SPDX-License-Identifier: Apache-2.0
// Copyright Contributors to the EVerest Project.

#include <algorithm>
#include <everest/logging.hpp>
#include <evse_security/evse_types.hpp>
#include <evse_security/utils/evse_filesystem_types.hpp>
#include <evse_security/utils/load_ctl.hpp>
#include <filesystem>
#include <map>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/x509.h>
#include <vector>

namespace fs = std::filesystem;
namespace evse_security {
void load_ctl(const fs::path& cert_dir, const std::map<CaCertificateType, fs::path>& ca_bundle_path_map) {

    // DC to CA type mapping
    static const std::map<std::string, CaCertificateType> dc_to_ca_type = {{"V2G", CaCertificateType::V2G},
                                                                           {"CSMS", CaCertificateType::CSMS},
                                                                           {"MO", CaCertificateType::MO},
                                                                           {"MF", CaCertificateType::MF}};

    auto getDC = [](X509_NAME* name) -> std::string {
        int idx = X509_NAME_get_index_by_NID(name, NID_domainComponent, 0);
        if (idx < 0)
            return {};
        ASN1_STRING* val = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, idx));
        return std::string((const char*)ASN1_STRING_get0_data(val), ASN1_STRING_length(val));
    };

    std::map<CaCertificateType, BIO*> bundle_bios;
    std::map<CaCertificateType, std::FILE*> bundle_files;

    for (const auto& [ca_type, bundle_path] : ca_bundle_path_map) {
        std::FILE* f = std::fopen(bundle_path.c_str(), "a");
        if (!f) {
            EVLOG_error << "Failed to open bundle file: " << bundle_path;
            continue;
        }
        bundle_files[ca_type] = f;
        bundle_bios[ca_type] = BIO_new_fp(f, BIO_NOCLOSE);
    }

    // Iterate CTL directory
    for (const auto& entry : fs::directory_iterator(cert_dir)) {
        const fs::path& file_path = entry.path();
        const auto ext = file_path.extension();

        BIO* bio = BIO_new_file(file_path.c_str(), "rb");
        if (!bio) {
            EVLOG_error << "Error opening file: " << file_path;
            continue;
        }

        std::vector<X509*> extracted_certs;

        PKCS7* pkcs7 = nullptr;
        if (ext == ".der") {
            // DER files: try DER first, PEM fallback
            pkcs7 = d2i_PKCS7_bio(bio, nullptr);
            if (!pkcs7) {
                BIO_seek(bio, 0);
                pkcs7 = PEM_read_bio_PKCS7(bio, nullptr, nullptr, nullptr);
            }
        } else if (ext == ".ctl") {
            pkcs7 = d2i_PKCS7_bio(bio, nullptr);
            if (!pkcs7) {
                BIO_seek(bio, 0);
                pkcs7 = PEM_read_bio_PKCS7(bio, nullptr, nullptr, nullptr);
            }
        } else if (ext == ".p7b" || ext == ".p7c") {
            // p7b/p7c are more commonly PEM encoded
            pkcs7 = PEM_read_bio_PKCS7(bio, nullptr, nullptr, nullptr);
            if (!pkcs7) {
                BIO_seek(bio, 0);
                pkcs7 = d2i_PKCS7_bio(bio, nullptr);
            }
        }

        if (pkcs7) {
            STACK_OF(X509)* certs = nullptr;
            if (PKCS7_type_is_signed(pkcs7) && pkcs7->d.sign) {
                certs = pkcs7->d.sign->cert;
            } else if (PKCS7_type_is_signedAndEnveloped(pkcs7) && pkcs7->d.signed_and_enveloped) {
                certs = pkcs7->d.signed_and_enveloped->cert;
            }

            if (certs && sk_X509_num(certs) > 0) {
                EVLOG_info << "Extracted " << sk_X509_num(certs) << " certificates from PKCS7: " << file_path;
                for (int i = 0; i < sk_X509_num(certs); ++i) {
                    X509* cert = sk_X509_value(certs, i);
                    X509_up_ref(cert); // increment ref count before PKCS7_free
                    extracted_certs.push_back(cert);
                }
            } else {
                EVLOG_warning << "PKCS7 file contains no certificates: " << file_path;
            }

            PKCS7_free(pkcs7); // now safe to free, certs have their own ref
        }

        // Try PEM X509 if nothing extracted yet
        if (extracted_certs.empty()) {
            BIO_seek(bio, 0);
            X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
            if (cert) {
                EVLOG_info << "Extracted PEM certificate from: " << file_path;
                extracted_certs.push_back(cert); // already owns ref from PEM_read
            }
        }

        // Try DER X509 if still nothing
        if (extracted_certs.empty()) {
            BIO_seek(bio, 0);
            X509* cert = d2i_X509_bio(bio, nullptr);
            if (cert) {
                EVLOG_info << "Extracted DER certificate from: " << file_path;
                extracted_certs.push_back(cert); // already owns ref from d2i
            }
        }

        if (extracted_certs.empty()) {
            EVLOG_warning << "Could not read any certificate from file: " << file_path;
            ERR_print_errors_fp(stderr);
        }

        // Sort each cert by DC into the correct bundle
        for (X509* cert : extracted_certs) {
            X509_NAME* subject = X509_get_subject_name(cert);
            std::string dc = getDC(subject);

            // Uppercase for case-insensitive comparison
            std::transform(dc.begin(), dc.end(), dc.begin(), ::toupper);

            auto it = dc_to_ca_type.find(dc);
            if (it != dc_to_ca_type.end()) {
                auto bio_it = bundle_bios.find(it->second);
                if (bio_it != bundle_bios.end()) {
                    EVLOG_info << "Adding cert with DC=" << dc << " to "
                               << conversions::ca_certificate_type_to_string(it->second) << " bundle";
                    PEM_write_bio_X509(bio_it->second, cert);
                }
            } else {
                EVLOG_warning << "Could not determine CA type for cert with DC='" << dc << "' in file: " << file_path;
            }

            // Release our reference
            X509_free(cert);
        }

        extracted_certs.clear();
        BIO_free(bio);
    }

    // Flush and close all bundle files
    for (auto& [ca_type, bio] : bundle_bios) {
        BIO_flush(bio);
        BIO_free(bio);
    }
    for (auto& [ca_type, f] : bundle_files) {
        std::fclose(f);
    }
}
} // namespace evse_security
