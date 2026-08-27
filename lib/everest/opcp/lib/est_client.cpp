// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/opcp/est_client.hpp>

#include <everest/opcp/pkcs.hpp>

namespace opcp {

EstClient::EstClient(HttpClientInterface& http_, Environment env_) : http(http_), env(std::move(env_)) {
}

EstResult EstClient::from_response(const std::string& url, const HttpResponse& response) {
    EstResult result;
    result.url = url;
    result.http_status = response.status;
    if (response.auth_rejected()) {
        result.auth_rejected = true;
        result.error = "HTTP " + std::to_string(response.status) + " (credentials rejected)";
        if (!response.body.empty()) {
            result.error += ": " + response.body;
        }
        return result;
    }
    if (!response.ok()) {
        result.error = "HTTP " + std::to_string(response.status);
        if (!response.body.empty()) {
            result.error += ": " + response.body;
        }
        return result;
    }
    result.certificates_pem = pkcs7_base64_to_pem_certificates(response.body);
    if (result.certificates_pem.empty()) {
        result.error = "response is not a PKCS#7 certs-only structure (content-type '" + response.content_type + "')";
        return result;
    }
    result.ok = true;
    return result;
}

EstResult EstClient::post_csr(const std::string& url, const std::string& csr_pem, const Auth& auth) {
    EstResult result;
    result.url = url;

    const std::string body = pem_csr_to_base64_der(csr_pem);
    if (body.empty()) {
        result.error = "CSR is not PEM encoded";
        return result;
    }

    HttpRequest request;
    request.method = "POST";
    request.url = url;
    request.headers = {{"Content-Type", "application/pkcs10"},
                       {"Content-Transfer-Encoding", "base64"},
                       {"Accept", "application/pkcs7-mime"}};
    request.body = body;
    request.auth = auth;

    try {
        return from_response(url, http.perform(request));
    } catch (const HttpError& e) {
        result.error = e.what();
        return result;
    }
}

EstResult EstClient::simple_enroll(IsoVersion version, const std::string& csr_pem, const Auth& auth) {
    return post_csr(env.simpleenroll_url(version), csr_pem, auth);
}

EstResult EstClient::simple_reenroll(IsoVersion version, const std::string& csr_pem, const Auth& auth) {
    return post_csr(env.simplereenroll_url(version), csr_pem, auth);
}

EstResult EstClient::cacerts(IsoVersion version, const Auth& auth) {
    const std::string url = env.cacerts_url(version);
    EstResult result;
    result.url = url;

    HttpRequest request;
    request.method = "GET";
    request.url = url;
    request.headers = {{"Content-Transfer-Encoding", "base64"}, {"Accept", "application/pkcs7-mime"}};
    request.auth = auth;

    try {
        return from_response(url, http.perform(request));
    } catch (const HttpError& e) {
        result.error = e.what();
        return result;
    }
}

} // namespace opcp
