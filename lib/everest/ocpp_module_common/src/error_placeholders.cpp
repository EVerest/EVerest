// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/error_placeholders.hpp>

#include <utils/date.hpp>
#include <utils/error.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

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

} // namespace

bool is_error_placeholder(std::string_view name) {
    return find_placeholder(name).has_value();
}

std::string substitute_error_placeholders(const Everest::error::Error& error, std::string_view pattern) {
    std::string result;
    result.reserve(pattern.size());

    std::size_t pos = 0;
    while (pos < pattern.size()) {
        const auto open = pattern.find(PLACEHOLDER_OPEN, pos);
        if (open == std::string_view::npos) {
            break;
        }
        const auto name_begin = open + PLACEHOLDER_OPEN.size();
        const auto close = pattern.find(PLACEHOLDER_CLOSE, name_begin);
        if (close == std::string_view::npos) {
            break;
        }

        result.append(pattern, pos, open - pos);
        if (const auto value_of = find_placeholder(pattern.substr(name_begin, close - name_begin))) {
            result += value_of.value()(error);
        } else {
            result.append(pattern, open, close - open + 1);
        }
        pos = close + 1;
    }
    result.append(pattern, pos);
    return result;
}

} // namespace ocpp_module_common
