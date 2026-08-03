// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace everest::lib::API {

namespace detail {
inline std::string request_reply_to(nlohmann::json const& msg) {
    if (auto it = msg.find("headers"); it != msg.end() && it->contains("replyTo")) {
        return (*it)["replyTo"].get<std::string>();
    }
    return {};
}
} // namespace detail

// Single-pass parsing of RequestReply command messages. The payload subtree is
// converted directly into PayloadT; generic::RequestReply in contrast stores the
// payload as a string, forcing a serialize/parse round trip on every command.
// PayloadT's from_json must be visible at the point of instantiation, i.e. the
// caller needs to include the json_codec.hpp of the payload's type domain.
template <class PayloadT>
bool deserialize_request(std::string_view data, std::string& reply_to, PayloadT& payload) noexcept {
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
inline bool deserialize_request(std::string_view data, std::string& reply_to) noexcept {
    try {
        auto msg = nlohmann::json::parse(data.begin(), data.end());
        reply_to = detail::request_reply_to(msg);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace everest::lib::API
