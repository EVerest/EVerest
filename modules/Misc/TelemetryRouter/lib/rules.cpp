// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "rules.hpp"

#include <algorithm>
#include <cstdlib>
#include <set>

#include <fnmatch.h>

#include <fmt/format.h>

namespace telemetry_router {

namespace {

const std::set<std::string> RULE_KEYS = {"name", "match", "action", "forward", "continue"};
const std::set<std::string> MATCH_KEYS = {"module_id", "module_type", "element", "kind"};
const std::set<std::string> DOCUMENT_KEYS = {"version", "sinks", "default_action", "rules"};

void check_keys(const nlohmann::json& object, const std::set<std::string>& allowed, const std::string& where) {
    for (const auto& [key, value] : object.items()) {
        if (allowed.count(key) == 0) {
            throw RulesError(fmt::format("{}: unknown key '{}'", where, key));
        }
    }
}

std::vector<std::string> string_list(const nlohmann::json& value, const std::string& where) {
    if (value.is_string()) {
        return {value.get<std::string>()};
    }
    if (value.is_array() and not value.empty() and
        std::all_of(value.begin(), value.end(), [](const auto& entry) { return entry.is_string(); })) {
        return value.get<std::vector<std::string>>();
    }
    throw RulesError(fmt::format("{}: expected a string or a non-empty list of strings", where));
}

// ${env:NAME} in sink options, resolved once at load time
nlohmann::json substitute_environment(const nlohmann::json& value, const std::string& where) {
    if (value.is_object()) {
        auto result = nlohmann::json::object();
        for (const auto& [key, entry] : value.items()) {
            result[key] = substitute_environment(entry, where);
        }
        return result;
    }
    if (value.is_array()) {
        auto result = nlohmann::json::array();
        for (const auto& entry : value) {
            result.push_back(substitute_environment(entry, where));
        }
        return result;
    }
    if (not value.is_string()) {
        return value;
    }
    auto text = value.get<std::string>();
    std::string result;
    std::size_t position = 0;
    while (true) {
        const auto start = text.find("${env:", position);
        if (start == std::string::npos) {
            result += text.substr(position);
            break;
        }
        const auto end = text.find('}', start);
        if (end == std::string::npos) {
            throw RulesError(fmt::format("{}: unterminated '${{env:' in '{}'", where, text));
        }
        const auto name = text.substr(start + 6, end - start - 6);
        const char* environment_value = std::getenv(name.c_str());
        if (environment_value == nullptr) {
            throw RulesError(fmt::format("{}: environment variable '{}' is not set", where, name));
        }
        result += text.substr(position, start - position) + environment_value;
        position = end + 1;
    }
    return result;
}

std::vector<Target> parse_targets(const nlohmann::json& value, const std::string& where) {
    if (not value.is_array() or value.empty()) {
        throw RulesError(fmt::format("{}: forward must be a non-empty list of targets", where));
    }
    std::vector<Target> targets;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto& entry = value.at(i);
        const auto target_where = fmt::format("{}: forward[{}]", where, i);
        if (not entry.is_object() or not entry.contains("sink") or not entry.at("sink").is_string()) {
            throw RulesError(fmt::format("{}: a target needs a sink name", target_where));
        }
        Target target;
        target.sink = entry.at("sink").get<std::string>();
        target.options = entry;
        target.options.erase("sink");
        targets.push_back(std::move(target));
    }
    return targets;
}

Match parse_match(const nlohmann::json& value, const std::string& where) {
    Match match;
    if (value.is_null()) {
        return match;
    }
    if (not value.is_object()) {
        throw RulesError(fmt::format("{}: match must be an object", where));
    }
    check_keys(value, MATCH_KEYS, where + ": match");
    if (value.contains("module_id")) {
        match.module_ids = string_list(value.at("module_id"), where + ": match.module_id");
    }
    if (value.contains("module_type")) {
        match.module_types = string_list(value.at("module_type"), where + ": match.module_type");
    }
    if (value.contains("element")) {
        match.elements = string_list(value.at("element"), where + ": match.element");
    }
    if (value.contains("kind")) {
        for (const auto& kind_name : string_list(value.at("kind"), where + ": match.kind")) {
            const auto kind = everest::telemetry::kind_from_string(kind_name);
            if (not kind.has_value()) {
                throw RulesError(fmt::format("{}: match.kind: unknown kind '{}'", where, kind_name));
            }
            match.kinds.push_back(kind.value());
        }
    }
    return match;
}

bool any_glob_matches(const std::vector<std::string>& patterns, const std::string& value) {
    return patterns.empty() or std::any_of(patterns.begin(), patterns.end(),
                                           [&value](const auto& pattern) { return glob_match(pattern, value); });
}

} // namespace

std::string Rule::label() const {
    return name.empty() ? fmt::format("rules[{}]", index) : fmt::format("rule '{}' (rules[{}])", name, index);
}

RulesConfig parse_rules(const nlohmann::json& document) {
    if (not document.is_object()) {
        throw RulesError("rules file: expected a mapping at the top level");
    }
    check_keys(document, DOCUMENT_KEYS, "rules file");
    if (document.contains("version") and document.at("version") != 1) {
        throw RulesError("rules file: unsupported version, expected 1");
    }

    RulesConfig config;
    const auto sinks = document.value("sinks", nlohmann::json::object());
    if (not sinks.is_object()) {
        throw RulesError("sinks: expected a mapping");
    }
    for (const auto& [name, options] : sinks.items()) {
        const auto where = fmt::format("sinks.{}", name);
        if (not options.is_object() or not options.contains("type") or not options.at("type").is_string()) {
            throw RulesError(fmt::format("{}: a sink needs a type", where));
        }
        SinkConfig sink;
        sink.name = name;
        sink.type = options.at("type").get<std::string>();
        sink.options = substitute_environment(options, where);
        sink.options.erase("type");
        config.sinks.push_back(std::move(sink));
    }

    if (document.contains("default_action")) {
        const auto& default_action = document.at("default_action");
        if (default_action.is_object() and default_action.contains("forward")) {
            config.default_forward = parse_targets(default_action.at("forward"), "default_action");
        } else if (default_action != "drop") {
            throw RulesError("default_action: expected 'drop' or {forward: [...]}");
        }
    }

    const auto rules = document.value("rules", nlohmann::json::array());
    if (not rules.is_array()) {
        throw RulesError("rules: expected a list");
    }
    for (std::size_t i = 0; i < rules.size(); ++i) {
        const auto& entry = rules.at(i);
        Rule rule;
        rule.index = i;
        if (not entry.is_object()) {
            throw RulesError(fmt::format("{}: expected a mapping", rule.label()));
        }
        rule.name = entry.value("name", std::string{});
        const auto where = rule.label();
        check_keys(entry, RULE_KEYS, where);
        rule.match = parse_match(entry.value("match", nlohmann::json()), where);
        const bool has_action = entry.contains("action");
        const bool has_forward = entry.contains("forward");
        if (has_action == has_forward) {
            throw RulesError(fmt::format("{}: needs either 'action: drop' or 'forward'", where));
        }
        if (has_action) {
            if (entry.at("action") != "drop") {
                throw RulesError(fmt::format("{}: the only action is 'drop'", where));
            }
            rule.drop = true;
        } else {
            rule.forward = parse_targets(entry.at("forward"), where);
        }
        if (entry.contains("continue")) {
            if (not entry.at("continue").is_boolean() or rule.drop) {
                throw RulesError(fmt::format("{}: continue must be a boolean on a forwarding rule", where));
            }
            rule.continue_matching = entry.at("continue").get<bool>();
        }
        config.rules.push_back(std::move(rule));
    }

    std::set<std::string> sink_names;
    for (const auto& sink : config.sinks) {
        sink_names.insert(sink.name);
    }
    const auto check_targets = [&sink_names](const std::vector<Target>& targets, const std::string& where) {
        for (const auto& target : targets) {
            if (sink_names.count(target.sink) == 0) {
                throw RulesError(fmt::format("{}: unknown sink '{}'", where, target.sink));
            }
        }
    };
    check_targets(config.default_forward, "default_action");
    for (const auto& rule : config.rules) {
        check_targets(rule.forward, rule.label());
    }
    return config;
}

bool glob_match(const std::string& pattern, const std::string& value) {
    return ::fnmatch(pattern.c_str(), value.c_str(), 0) == 0;
}

bool matches(const Match& match, const Producer& producer, const ElementDeclaration& element) {
    return any_glob_matches(match.module_ids, producer.id) and any_glob_matches(match.module_types, producer.type) and
           any_glob_matches(match.elements, element.name) and
           (match.kinds.empty() or
            std::find(match.kinds.begin(), match.kinds.end(), element.kind) != match.kinds.end());
}

std::vector<Route> evaluate(const RulesConfig& rules, const Producer& producer, const ElementDeclaration& element) {
    std::vector<Route> routes;
    for (const auto& rule : rules.rules) {
        if (not matches(rule.match, producer, element)) {
            continue;
        }
        if (rule.drop) {
            return routes;
        }
        for (const auto& target : rule.forward) {
            routes.push_back({&rule, &target});
        }
        if (not rule.continue_matching) {
            return routes;
        }
    }
    for (const auto& target : rules.default_forward) {
        routes.push_back({nullptr, &target});
    }
    return routes;
}

std::string substitute(const std::string& text, const Producer& producer, const ElementDeclaration& element) {
    std::string result;
    std::size_t position = 0;
    while (true) {
        const auto start = text.find("${", position);
        if (start == std::string::npos) {
            result += text.substr(position);
            return result;
        }
        const auto end = text.find('}', start);
        if (end == std::string::npos) {
            throw RulesError(fmt::format("unterminated '${{' in '{}'", text));
        }
        const auto variable = text.substr(start + 2, end - start - 2);
        std::string replacement;
        if (variable == "module_id") {
            replacement = producer.id;
        } else if (variable == "module_type") {
            replacement = producer.type;
        } else if (variable == "element") {
            replacement = element.name;
        } else if (variable == "unit") {
            replacement = element.unit.value_or("");
        } else {
            throw RulesError(fmt::format("unknown variable '${{{}}}' in '{}'", variable, text));
        }
        result += text.substr(position, start - position) + replacement;
        position = end + 1;
    }
}

} // namespace telemetry_router
