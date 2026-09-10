// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Shared plumbing for the management-API handler tests: building a request envelope, invoking a
// registered handler and reading back the reply.

#pragma once

#include <string>

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include <tests/mock_mqtt_abstraction.hpp>

namespace Everest::tests {

constexpr auto REPLY_TO = "test/reply";

/// \brief Builds the on-the-wire request envelope and wraps it as a JSON string value.
///
/// The command topics live under the "everest_api" base (not the everest/ prefix), so the
/// external-MQTT dispatcher hands the handler the raw request as a JSON string value instead of a
/// parsed object; the tests reproduce that by wrapping the envelope in nlohmann::json(dump()).
inline nlohmann::json make_request(const nlohmann::json& payload, const std::string& reply_to = REPLY_TO) {
    nlohmann::json envelope;
    envelope["headers"]["replyTo"] = reply_to;
    envelope["payload"] = payload;
    return nlohmann::json(envelope.dump());
}

/// \brief Same as make_request(), but with no headers.replyTo at all.
inline nlohmann::json make_request_without_reply_to(const nlohmann::json& payload) {
    nlohmann::json envelope;
    envelope["payload"] = payload;
    return nlohmann::json(envelope.dump());
}

/// \brief Invokes the handler the API registered on \p topic.
inline void invoke(MockMQTTAbstraction& mock, const std::string& topic, const nlohmann::json& request) {
    REQUIRE(mock.registered_handlers().count(topic) == 1);
    (*mock.registered_handlers().at(topic)->handler)(topic, request);
}

/// \brief Parses the reply the API published (publish() stored it as a JSON string value).
inline nlohmann::json last_reply(const MockMQTTAbstraction& mock) {
    REQUIRE_FALSE(mock.published().empty());
    return nlohmann::json::parse(mock.published().back().second.get<std::string>());
}

} // namespace Everest::tests
