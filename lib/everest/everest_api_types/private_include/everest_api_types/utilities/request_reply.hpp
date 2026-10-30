// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest_api_types/utilities/Topics.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace everest::lib::API {

namespace detail {
// headers.replyTo is optional; without it the request is executed and its reply discarded. A request that is not
// a JSON object, or a replyTo that Topics::is_valid_reply_to refuses, throws, so the request is rejected before it
// is executed.
inline std::optional<std::string> request_reply_to(nlohmann::json const& msg) {
    if (not msg.is_object()) {
        throw std::invalid_argument("request is not a JSON object");
    }
    auto headers = msg.find("headers");
    if (headers == msg.end()) {
        return std::nullopt;
    }
    if (not headers->is_object()) {
        throw std::invalid_argument("headers is not an object");
    }
    auto reply_to = headers->find("replyTo");
    if (reply_to == headers->end()) {
        return std::nullopt;
    }
    auto topic = reply_to->get<std::string>();
    if (not Topics::is_valid_reply_to(topic)) {
        throw std::invalid_argument("replyTo is not a valid reply topic: '" + topic + "'");
    }
    return topic;
}
} // namespace detail

// Single-pass parsing of RequestReply command messages. The payload subtree is
// converted directly into PayloadT; generic::RequestReply in contrast stores the
// payload as a string, forcing a serialize/parse round trip on every command.
// PayloadT's from_json must be visible at the point of instantiation, i.e. the
// caller needs to include the json_codec.hpp of the payload's type domain.
template <class PayloadT>
bool deserialize_request(std::string_view data, std::optional<std::string>& reply_to, PayloadT& payload) noexcept {
    try {
        auto msg = nlohmann::json::parse(data.begin(), data.end());
        reply_to = detail::request_reply_to(msg);
        payload = msg.at("payload").get<PayloadT>();
        return true;
    } catch (...) {
        return false;
    }
}

// Overload for commands without a payload.
inline bool deserialize_request(std::string_view data, std::optional<std::string>& reply_to) noexcept {
    try {
        auto msg = nlohmann::json::parse(data.begin(), data.end());
        reply_to = detail::request_reply_to(msg);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace everest::lib::API
