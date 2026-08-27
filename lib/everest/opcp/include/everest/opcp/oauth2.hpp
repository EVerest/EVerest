// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <everest/opcp/environment.hpp>
#include <everest/opcp/http_client.hpp>

/// \file oauth2.hpp
/// OAuth2 client-credentials flow of the OPCP access token service (`POST /oauth/token`).
namespace opcp {

struct TokenResult {
    bool ok{false};
    long http_status{0};
    std::string error; ///< transport error, OAuth2 `error_description`, or parse error

    std::string access_token;
    std::string token_type;
    long expires_in{0};
    std::string scope;

    BearerToken bearer() const {
        return BearerToken{access_token};
    }
};

/// Requests a token with `grant_type=client_credentials`. Never throws.
TokenResult fetch_access_token(HttpClientInterface& http, const Environment& env, const std::string& client_id,
                               const std::string& client_secret);

/// Decodes the (unverified) payload of a JWT for display purposes. Returns an empty object when the token
/// is not a JWT.
nlohmann::json decode_jwt_payload(const std::string& token);

/// Extracts role claims from a decoded payload. Hubject uses namespaced claims such as
/// `https://eu.plugncharge-qa.hubject.com/role: ["CPO", ...]`, so every key ending in "/role" is collected.
std::vector<std::string> jwt_roles(const nlohmann::json& payload);

} // namespace opcp
