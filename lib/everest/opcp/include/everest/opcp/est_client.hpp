// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>
#include <vector>

#include <everest/opcp/environment.hpp>
#include <everest/opcp/http_client.hpp>
#include <everest/opcp/types.hpp>

/// \file est_client.hpp
/// RFC 7030 (Enrollment over Secure Transport) subset offered by the V2G PKI services: simpleenroll,
/// simplereenroll and cacerts, all addressed per sub-CA and ISO version.
namespace opcp {

struct EstResult {
    bool ok{false};
    long http_status{0};
    /// True when the server refused the credentials (401/403) - retrying with the same auth is pointless
    bool auth_rejected{false};
    std::string error;
    std::string url;
    /// Certificates contained in the PKCS#7 response, PEM encoded, in response order
    std::vector<std::string> certificates_pem;
};

class EstClient {
public:
    EstClient(HttpClientInterface& http, Environment env);

    /// Initial enrollment: POST base64 DER PKCS#10, authenticated with an OAuth2 bearer token
    EstResult simple_enroll(IsoVersion version, const std::string& csr_pem, const Auth& auth);

    /// Renewal: same body, authenticated with the currently installed leaf as TLS client certificate
    EstResult simple_reenroll(IsoVersion version, const std::string& csr_pem, const Auth& auth);

    /// CPO sub-CA chain for the given ISO version (curve derived from the version)
    EstResult cacerts(IsoVersion version, const Auth& auth);

    const Environment& environment() const {
        return env;
    }

private:
    EstResult post_csr(const std::string& url, const std::string& csr_pem, const Auth& auth);
    static EstResult from_response(const std::string& url, const HttpResponse& response);

    HttpClientInterface& http;
    Environment env;
};

} // namespace opcp
