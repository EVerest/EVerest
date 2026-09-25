// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "catalog.hpp"

namespace telemetry_router {

class RulesError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Match {
    std::vector<std::string> module_ids;
    std::vector<std::string> module_types;
    std::vector<std::string> elements;
    std::vector<Kind> kinds;
};

/// One forwarding target of a rule: a sink name plus sink-specific options
struct Target {
    std::string sink;
    nlohmann::json options;
};

struct Rule {
    std::string name;
    std::size_t index{0};
    Match match;
    bool drop{false};
    std::vector<Target> forward;
    bool continue_matching{false};

    /// \returns "rule 'name' (rules[index])" for messages
    std::string label() const;
};

struct SinkConfig {
    std::string name;
    std::string type;
    nlohmann::json options;
};

struct RulesConfig {
    std::vector<SinkConfig> sinks;
    std::vector<Target> default_forward;
    std::vector<Rule> rules;
};

/// \brief Parse a rules document (the YAML rules file as JSON)
/// \throws RulesError describing the first problem found
RulesConfig parse_rules(const nlohmann::json& document);

/// \brief fnmatch(3) glob matching
bool glob_match(const std::string& pattern, const std::string& value);

bool matches(const Match& match, const Producer& producer, const ElementDeclaration& element);

/// \brief A target selected for an element, together with the rule that selected it (nullptr: default action)
struct Route {
    const Rule* rule;
    const Target* target;
};

/// \brief Evaluate the rules in order: the first matching rule decides, unless it sets continue
std::vector<Route> evaluate(const RulesConfig& rules, const Producer& producer, const ElementDeclaration& element);

/// \brief Replace ${module_id}, ${module_type}, ${element} and ${unit} in \p text
/// \throws RulesError on unknown or unterminated variables
std::string substitute(const std::string& text, const Producer& producer, const ElementDeclaration& element);

} // namespace telemetry_router
