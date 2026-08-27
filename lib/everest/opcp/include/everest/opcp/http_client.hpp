// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

/// \file http_client.hpp
/// Minimal HTTP abstraction used by every OPCP endpoint client. The interface is virtual so the endpoint
/// clients and the enroller can be unit tested without a network; the libcurl implementation is the only
/// production one.
namespace opcp {

/// Request without credentials (the OAuth2 token endpoint itself)
struct NoAuth {};

/// `Authorization: Bearer <token>` as issued by `POST /oauth/token`
struct BearerToken {
    std::string token;
};

/// TLS client authentication with an installed leaf certificate (RFC 7030 re-enrollment). The paths point
/// into the evse_security store as returned by get_leaf_certificate_info(): the certificate file may hold the
/// full chain (leaf first) so the server can build the path to the V2G root.
struct ClientCertificate {
    std::string certificate_path;
    std::string key_path;
    std::optional<std::string> key_password;
};

using Auth = std::variant<NoAuth, BearerToken, ClientCertificate>;

inline bool is_client_certificate_auth(const Auth& auth) {
    return std::holds_alternative<ClientCertificate>(auth);
}

struct HttpRequest {
    std::string method{"GET"};
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    Auth auth{NoAuth{}};
};

struct HttpResponse {
    long status{0};
    std::string body;
    std::string content_type;

    bool ok() const {
        return status >= 200 && status < 300;
    }
    /// 401/403: credentials were not accepted. Distinguished so callers can stop retrying.
    bool auth_rejected() const {
        return status == 401 || status == 403;
    }
};

/// Transport level failure (DNS, TCP, TLS handshake, timeout). HTTP error statuses are NOT exceptions.
class HttpError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class HttpClientInterface {
public:
    virtual ~HttpClientInterface() = default;
    /// Performs the request. Throws HttpError on transport failures.
    virtual HttpResponse perform(const HttpRequest& request) = 0;
};

struct HttpClientOptions {
    /// PEM bundle used to verify the server certificate. Empty: libcurl / system default store.
    std::optional<std::string> server_ca_bundle;
    std::chrono::milliseconds timeout{std::chrono::seconds(30)};
    /// Enables libcurl verbose output on stderr (CLI debugging)
    bool verbose{false};
};

/// libcurl based implementation. Every request uses a fresh easy handle; TLS >= 1.2 and peer verification
/// are always enforced.
class CurlHttpClient : public HttpClientInterface {
public:
    explicit CurlHttpClient(HttpClientOptions options = {});
    ~CurlHttpClient() override;

    CurlHttpClient(const CurlHttpClient&) = delete;
    CurlHttpClient& operator=(const CurlHttpClient&) = delete;

    HttpResponse perform(const HttpRequest& request) override;

private:
    HttpClientOptions options;
};

} // namespace opcp
