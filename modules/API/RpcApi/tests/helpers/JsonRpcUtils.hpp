// SPDX-License-Identifier: Apache-2.0
// Copyright chargebyte GmbH and Contributors to EVerest
#ifndef JSON_RPC_UTILS_HPP
#define JSON_RPC_UTILS_HPP

#include "../../helpers/LimitDecimalPlaces.hpp"
#include <nlohmann/json.hpp>
#include <string_view>

constexpr std::string_view JSON_RPC_SPEC_VERSION{"2.0"};

namespace json_rpc_utils {

inline nlohmann::json create_json_rpc_request(const std::string& method, const nlohmann::json& params, int id) {
    nlohmann::json request;
    request["jsonrpc"] = JSON_RPC_SPEC_VERSION;
    request["method"] = method;
    request["params"] = params;
    request["id"] = id;
    return request;
}

inline nlohmann::json create_json_rpc_response(const nlohmann::json& result, int id) {
    auto tmp = result;
    nlohmann::json response;
    response["jsonrpc"] = JSON_RPC_SPEC_VERSION;
    helpers::round_floats_in_json(tmp);
    response["result"] = tmp;
    response["id"] = id;
    return response;
}

inline nlohmann::json create_json_rpc_error_response(int code, const std::string& message, int id) {
    nlohmann::json error_response;
    error_response["jsonrpc"] = JSON_RPC_SPEC_VERSION;
    error_response["error"]["code"] = code;
    error_response["error"]["message"] = message;
    error_response["id"] = id;
    return error_response;
}

// To check if single key-value pair is part of a JSON object. Key-value pair must be stored in a JSON object.
inline bool is_key_value_in_json_rpc_result(const nlohmann::json& json_obj, const nlohmann::json& json_key_value) {
    if (not json_key_value.is_object()) {
        throw std::invalid_argument("json_key_value must be a JSON object");
    }

    // Check if the JSON object contains the key-value pair in the result object of the JSON-RPC response
    if (json_obj.contains("result") && json_obj["result"].is_object()) {
        const auto& result_obj = json_obj["result"];
        if (result_obj.contains(json_key_value.begin().key()) &&
            result_obj[json_key_value.begin().key()] == json_key_value.begin().value()) {
            return true;
        }
    }
    return false;
}

} // namespace json_rpc_utils

#endif // JSON_RPC_UTILS_HPP
