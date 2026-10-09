// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/error_placeholders.hpp>

#include <utils/date.hpp>
#include <utils/error.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace ocpp_module_common {

namespace {

constexpr std::string_view PLACEHOLDER_OPEN = "${";
constexpr char PLACEHOLDER_CLOSE = '}';

using Error = Everest::error::Error;
using PlaceholderValue = std::string (*)(const Error&);

std::string evse_of(const Error& error) {
    const auto& mapping = error.origin.mapping;
    return mapping.has_value() ? std::to_string(mapping->evse) : std::string{};
}

std::string connector_of(const Error& error) {
    const auto& mapping = error.origin.mapping;
    return mapping.has_value() && mapping->connector.has_value() ? std::to_string(mapping->connector.value())
                                                                 : std::string{};
}

const std::array<std::pair<std::string_view, PlaceholderValue>, 14> PLACEHOLDERS{{
    {"type", [](const Error& error) { return error.type; }},
    {"sub_type", [](const Error& error) { return error.sub_type; }},
    {"message", [](const Error& error) { return error.message; }},
    {"description", [](const Error& error) { return error.description; }},
    {"vendor_id", [](const Error& error) { return error.vendor_id; }},
    {"origin", [](const Error& error) { return error.origin.to_string(); }},
    {"origin_module", [](const Error& error) { return error.origin.module_id; }},
    {"origin_implementation", [](const Error& error) { return error.origin.implementation_id; }},
    {"evse", evse_of},
    {"connector", connector_of},
    {"severity", [](const Error& error) { return Everest::error::severity_to_string(error.severity); }},
    {"state", [](const Error& error) { return Everest::error::state_to_string(error.state); }},
    {"timestamp", [](const Error& error) { return Everest::Date::to_rfc3339(error.timestamp); }},
    {"uuid", [](const Error& error) { return error.uuid.to_string(); }},
}};

std::optional<PlaceholderValue> find_placeholder(std::string_view name) {
    const auto it = std::find_if(PLACEHOLDERS.begin(), PLACEHOLDERS.end(),
                                 [name](const auto& placeholder) { return placeholder.first == name; });
    if (it == PLACEHOLDERS.end()) {
        return std::nullopt;
    }
    return it->second;
}

struct Token {
    enum class Kind {
        Text,
        Placeholder,
        UnknownPlaceholder,
        Unterminated,
    };
    Kind kind;
    std::string_view raw;
    PlaceholderValue value_of{nullptr};
};

/// \returns \p pattern split into text and ${name} placeholders, in order
std::vector<Token> tokenize(std::string_view pattern) {
    std::vector<Token> tokens;
    std::size_t pos = 0;
    while (pos < pattern.size()) {
        const auto open = pattern.find(PLACEHOLDER_OPEN, pos);
        if (open == std::string_view::npos) {
            break;
        }
        const auto name_begin = open + PLACEHOLDER_OPEN.size();
        const auto close = pattern.find(PLACEHOLDER_CLOSE, name_begin);
        if (close == std::string_view::npos) {
            tokens.push_back({Token::Kind::Text, pattern.substr(pos, open - pos)});
            tokens.push_back({Token::Kind::Unterminated, pattern.substr(open)});
            return tokens;
        }

        tokens.push_back({Token::Kind::Text, pattern.substr(pos, open - pos)});
        const auto raw = pattern.substr(open, close - open + 1);
        if (const auto value_of = find_placeholder(pattern.substr(name_begin, close - name_begin))) {
            tokens.push_back({Token::Kind::Placeholder, raw, value_of.value()});
        } else {
            tokens.push_back({Token::Kind::UnknownPlaceholder, raw});
        }
        pos = close + 1;
    }
    tokens.push_back({Token::Kind::Text, pattern.substr(pos)});
    return tokens;
}

} // namespace

PlaceholderCheck check_error_placeholders(std::string_view pattern) {
    PlaceholderCheck check;
    for (const auto& token : tokenize(pattern)) {
        if (token.kind == Token::Kind::Placeholder) {
            continue;
        }
        check.static_length += token.raw.size();
        if (token.kind == Token::Kind::UnknownPlaceholder) {
            check.unknown.emplace_back(token.raw);
        } else if (token.kind == Token::Kind::Unterminated) {
            check.unterminated = true;
        }
    }
    return check;
}

std::string substitute_error_placeholders(const Everest::error::Error& error, std::string_view pattern) {
    std::string result;
    result.reserve(pattern.size());
    for (const auto& token : tokenize(pattern)) {
        if (token.kind == Token::Kind::Placeholder) {
            result += token.value_of(error);
        } else {
            result += token.raw;
        }
    }
    return result;
}

} // namespace ocpp_module_common
