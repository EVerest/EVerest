// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/detail/crypto.hpp>

#include <cstddef>
#include <string>

#include <openssl/obj_mac.h>

namespace iso15118::ev::crypto {

bool has_cpo_domain_component(const X509* cert) {
    if (cert == nullptr) {
        return false;
    }
    const X509_NAME* subject = X509_get_subject_name(cert);
    if (subject == nullptr) {
        return false;
    }
    int idx = -1;
    while ((idx = X509_NAME_get_index_by_NID(subject, NID_domainComponent, idx)) >= 0) {
        const X509_NAME_ENTRY* entry = X509_NAME_get_entry(subject, idx);
        const ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
        if (data == nullptr) {
            continue;
        }
        const std::string dc(reinterpret_cast<const char*>(ASN1_STRING_get0_data(data)),
                             static_cast<std::size_t>(ASN1_STRING_length(data)));
        if (dc == "CPO") {
            return true;
        }
    }
    return false;
}

} // namespace iso15118::ev::crypto
