// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <cstdlib>

#include <everest/utils/yaml_loader.hpp>

#include "rules.hpp"

using namespace telemetry_router;
using nlohmann::json;

namespace {

Producer make_producer(const std::string& id, const std::string& type) {
    Producer producer;
    producer.id = id;
    producer.type = type;
    return producer;
}

ElementDeclaration make_element(const std::string& name, Kind kind = Kind::Gauge) {
    ElementDeclaration element;
    element.name = name;
    element.kind = kind;
    element.unit = "V";
    return element;
}

json rules_document(json rules, json default_action = "drop") {
    auto result = json::parse(R"({"version": 1, "sinks": {"a": {"type": "log"}, "b": {"type": "log"}}})");
    result["default_action"] = std::move(default_action);
    result["rules"] = std::move(rules);
    return result;
}

std::vector<std::string> sinks_of(const RulesConfig& rules, const Producer& producer,
                                  const ElementDeclaration& element) {
    std::vector<std::string> sinks;
    for (const auto& route : evaluate(rules, producer, element)) {
        sinks.push_back(route.target->sink);
    }
    return sinks;
}

} // namespace

TEST_CASE("The shipped rules files parse", "[telemetry_router]") {
    const auto example = parse_rules(
        json(Everest::load_yaml(std::string(TELEMETRY_ROUTER_CONFIG_DIR) + "/telemetry_rules.example.yaml")));
    CHECK(example.sinks.size() == 2);
    CHECK(example.rules.size() == 3);
    CHECK(example.rules.at(0).drop);
    CHECK(example.rules.at(1).forward.size() == 2);
    CHECK(example.rules.at(1).forward.at(0).options.at("component").at("name") == "TelemetryExample");
    CHECK_FALSE(example.rules.at(1).forward.at(0).options.contains("sink"));

    const auto default_rules =
        parse_rules(json(Everest::load_yaml(std::string(TELEMETRY_ROUTER_CONFIG_DIR) + "/telemetry_rules.yaml")));
    CHECK(default_rules.rules.empty());
    CHECK(default_rules.default_forward.empty());
}

TEST_CASE("Invalid rules files are rejected with the location of the problem", "[telemetry_router]") {
    const auto error_of = [](const json& document) -> std::string {
        try {
            parse_rules(document);
        } catch (const RulesError& e) {
            return e.what();
        }
        return "";
    };
    const json forward = {{{"sink", "a"}}};

    CHECK_THAT(error_of(json::array()), Catch::Matchers::ContainsSubstring("top level"));
    CHECK_THAT(error_of({{"unknown", 1}}), Catch::Matchers::ContainsSubstring("unknown key 'unknown'"));
    CHECK_THAT(error_of({{"version", 2}}), Catch::Matchers::ContainsSubstring("version"));
    CHECK_THAT(error_of({{"sinks", {{"x", json::object()}}}}), Catch::Matchers::ContainsSubstring("sinks.x"));
    CHECK_THAT(error_of(rules_document({{{"name", "r"}, {"forward", {{{"sink", "missing"}}}}}})),
               Catch::Matchers::ContainsSubstring("rule 'r' (rules[0]): unknown sink 'missing'"));
    CHECK_THAT(error_of(rules_document({{{"action", "drop"}, {"forward", forward}}})),
               Catch::Matchers::ContainsSubstring("rules[0]: needs either"));
    CHECK_THAT(error_of(rules_document({{{"forward", forward}, {"match", {{"kind", "histogram"}}}}})),
               Catch::Matchers::ContainsSubstring("unknown kind 'histogram'"));
    CHECK_THAT(error_of(rules_document({{{"forward", forward}, {"match", {{"modul_id", "x"}}}}})),
               Catch::Matchers::ContainsSubstring("unknown key 'modul_id'"));
    CHECK_THAT(error_of(rules_document({{{"action", "drop"}, {"continue", true}}})),
               Catch::Matchers::ContainsSubstring("continue"));
    CHECK_THAT(error_of(rules_document(json::array(), "forward")),
               Catch::Matchers::ContainsSubstring("default_action"));
    CHECK_THAT(
        error_of(json::parse(R"({"sinks": {"x": {"type": "otel", "token": "${env:TELEMETRY_ROUTER_TEST_UNSET}"}}})")),
        Catch::Matchers::ContainsSubstring("TELEMETRY_ROUTER_TEST_UNSET"));
}

TEST_CASE("Environment variables are substituted in sink options", "[telemetry_router]") {
    ::setenv("TELEMETRY_ROUTER_TEST_TOKEN", "secret", 1);
    const auto rules = parse_rules(json::parse(R"({"sinks": {"x": {"type": "otel",
        "headers": {"Authorization": "Bearer ${env:TELEMETRY_ROUTER_TEST_TOKEN}"}}}})"));
    CHECK(rules.sinks.at(0).options.at("headers").at("Authorization") == "Bearer secret");
    CHECK_FALSE(rules.sinks.at(0).options.contains("type"));
}

TEST_CASE("Rules are evaluated first match first", "[telemetry_router]") {
    const auto meter = make_producer("powermeter_1", "GenericPowermeter");
    const auto simulator = make_producer("yeti_simulator", "YetiSimulator");
    const auto voltage = make_element("voltage");
    const auto state = make_element("state", Kind::State);

    const auto rules = parse_rules(rules_document(
        {
            {{"name", "drop-simulators"}, {"match", {{"module_id", "*_simulator"}}}, {"action", "drop"}},
            {{"name", "states"},
             {"match", {{"kind", {"state", "event"}}}},
             {"forward", {{{"sink", "a"}}}},
             {"continue", true}},
            {{"name", "meters"},
             {"match", {{"module_type", {"Other", "Generic*"}}, {"element", "volt*"}}},
             {"forward", {{{"sink", "a"}}, {{"sink", "b"}}}}},
        },
        {{"forward", {{{"sink", "b"}}}}}));

    CHECK(sinks_of(rules, meter, voltage) == std::vector<std::string>{"a", "b"});
    CHECK(sinks_of(rules, simulator, voltage).empty());
    CHECK(sinks_of(rules, meter, state) == std::vector<std::string>{"a", "b"});
    CHECK(sinks_of(rules, meter, make_element("current")) == std::vector<std::string>{"b"});

    const auto routes = evaluate(rules, meter, voltage);
    REQUIRE(routes.size() == 2);
    CHECK(routes.at(0).rule->name == "meters");
    CHECK(evaluate(rules, meter, make_element("current")).at(0).rule == nullptr);
}

TEST_CASE("Target templates are substituted", "[telemetry_router]") {
    const auto producer = make_producer("powermeter_1", "GenericPowermeter");
    const auto element = make_element("voltage");
    CHECK(substitute("${module_type}/${module_id}/${element} in ${unit}", producer, element) ==
          "GenericPowermeter/powermeter_1/voltage in V");
    CHECK(substitute("plain", producer, element) == "plain");
    CHECK_THROWS_AS(substitute("${nope}", producer, element), RulesError);
    CHECK_THROWS_AS(substitute("${module_id", producer, element), RulesError);
}
