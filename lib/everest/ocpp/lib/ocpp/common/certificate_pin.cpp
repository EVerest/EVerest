// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/common/certificate_pin.hpp>

#include <everest/logging.hpp>

#include <evse_security/certificate/x509_wrapper.hpp>

#include <ocpp/common/evse_security_impl.hpp>

namespace ocpp {

bool certificate_matches(const std::string& certificate, const CertificateHashDataType& expected) {
    try {
        const evse_security::X509Wrapper wrapper(certificate, evse_security::EncodingFormat::PEM);
        if (!wrapper.is_selfsigned()) {
            EVLOG_warning << "Only a self-signed certificate can be identified by its hash data, and this one is not";
            return false;
        }

        const auto actual = conversions::to_ocpp(wrapper.get_certificate_hash_data());
        if (actual.hashAlgorithm != expected.hashAlgorithm) {
            // The charging station computes SHA256, so hash data naming another algorithm cannot
            // match anything it holds. Saying so is more use than silently answering false.
            EVLOG_warning << "Certificate hash data asks for "
                          << conversions::hash_algorithm_enum_type_to_string(expected.hashAlgorithm)
                          << " but the charging station identifies certificates by "
                          << conversions::hash_algorithm_enum_type_to_string(actual.hashAlgorithm);
            return false;
        }

        return actual.issuerNameHash == expected.issuerNameHash and actual.issuerKeyHash == expected.issuerKeyHash and
               actual.serialNumber == expected.serialNumber;
    } catch (const std::exception& e) {
        EVLOG_warning << "Could not identify a certificate by its hash data: " << e.what();
        return false;
    }
}

} // namespace ocpp
