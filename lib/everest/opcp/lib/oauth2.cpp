// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/opcp/oauth2.hpp>

#include <evse_security/evse_security.hpp>

#include <everest/opcp/pkcs.hpp>

namespace opcp {

TokenResult fetch_access_token(HttpClientInterface& http, const Environment& env, const std::string& client_id,
                               const std::string& client_secret) {
    TokenResult result;

    const nlohmann::json payload = {
        {"client_id", client_id},
        {"client_secret", client_secret},
        {"audience", env.audience.empty() ? env.api_base_url : env.audience},
        {"grant_type", "client_credentials"},
    };

    HttpRequest request;
    request.method = "POST";
    request.url = env.auth_url;
    request.headers = {{"Content-Type", "application/json"}, {"Accept", "application/json"}};
    request.body = payload.dump();

    HttpResponse response;
    try {
        response = http.perform(request);
    } catch (const HttpError& e) {
        result.error = e.what();
        return result;
    }
    result.http_status = response.status;

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(response.body);
    } catch (const nlohmann::json::exception&) {
        result.error = "token endpoint returned HTTP " + std::to_string(response.status) + " with a non-JSON body";
        return result;
    }

    if (!response.ok()) {
        result.error = "token endpoint returned HTTP " + std::to_string(response.status);
        if (body.contains("error_description")) {
            result.error += ": " + body["error_description"].get<std::string>();
        } else if (body.contains("error")) {
            result.error += ": " + body["error"].dump();
        }
        return result;
    }

    if (!body.contains("access_token") || !body["access_token"].is_string()) {
        result.error = "token response has no access_token";
        return result;
    }
    result.access_token = body["access_token"].get<std::string>();
    result.token_type = body.value("token_type", "Bearer");
    result.scope = body.value("scope", "");
    if (body.contains("expires_in") && body["expires_in"].is_number()) {
        result.expires_in = body["expires_in"].get<long>();
    }
    result.ok = true;
    return result;
}

nlohmann::json decode_jwt_payload(const std::string& token) {
    const auto first = token.find('.');
    if (first == std::string::npos) {
        return nlohmann::json::object();
    }
    const auto second = token.find('.', first + 1);
    if (second == std::string::npos) {
        return nlohmann::json::object();
    }
    std::string payload = strip_whitespace(token.substr(first + 1, second - first - 1));
    // base64url -> base64
    for (char& c : payload) {
        if (c == '-') {
            c = '+';
        } else if (c == '_') {
            c = '/';
        }
    }
    while (payload.size() % 4 != 0) {
        payload.push_back('=');
    }
    try {
        return nlohmann::json::parse(evse_security::EvseSecurity::base64_decode_to_string(payload));
    } catch (const std::exception&) {
        return nlohmann::json::object();
    }
}

std::vector<std::string> jwt_roles(const nlohmann::json& payload) {
    std::vector<std::string> roles;
    if (!payload.is_object()) {
        return roles;
    }
    for (const auto& [key, value] : payload.items()) {
        const bool role_key = key == "role" || key == "roles" ||
                              (key.size() > 5 && key.compare(key.size() - 5, 5, "/role") == 0) ||
                              (key.size() > 6 && key.compare(key.size() - 6, 6, "/roles") == 0);
        if (!role_key) {
            continue;
        }
        if (value.is_array()) {
            for (const auto& item : value) {
                if (item.is_string()) {
                    roles.push_back(item.get<std::string>());
                }
            }
        } else if (value.is_string()) {
            roles.push_back(value.get<std::string>());
        }
    }
    return roles;
}

} // namespace opcp
