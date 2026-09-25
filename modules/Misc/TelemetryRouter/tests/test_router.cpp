// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <everest/utils/yaml_loader.hpp>
#include <utils/telemetry/wire.hpp>

#include "router.hpp"

using namespace telemetry_router;
using nlohmann::json;
namespace wire = everest::telemetry::wire;

namespace {

struct Received {
    std::string sink;
    std::string producer;
    std::string element;
    std::int64_t timestamp_ms;
    json value;
    json options;
};

struct RecordingBinding : SinkBinding {
    json options;
};

class RecordingSink : public Sink {
public:
    RecordingSink(std::string name, std::vector<Received>& received, std::vector<std::string>& events) :
        Sink(std::move(name)), m_received(received), m_events(events) {
    }

    std::string_view type() const override {
        return "recording";
    }

    std::unique_ptr<SinkBinding> bind(const Target& target, const Producer&,
                                      const ElementDeclaration& element) override {
        if (element.kind == Kind::Event and name() == "numbers_only") {
            throw BindError("no events");
        }
        auto binding = std::make_unique<RecordingBinding>();
        binding->options = target.options;
        return binding;
    }

    void submit(const Sample& sample, const SinkBinding& binding) override {
        m_received.push_back({name(), sample.producer.id, sample.element.name, sample.timestamp_ms, sample.value,
                              static_cast<const RecordingBinding&>(binding).options});
    }

    void release(const SinkBinding&) override {
        m_events.push_back("release " + name());
    }

    void enable() override {
        m_events.push_back("enable " + name());
    }

    void stop() override {
        m_events.push_back("stop " + name());
    }

private:
    std::vector<Received>& m_received;
    std::vector<std::string>& m_events;
};

const auto CATALOG = json::parse(R"({
    "evse_manager_1": {
        "module_type": "EvseManager",
        "mapping": {"evse": 1},
        "elements": {
            "temperature": {"kind": "gauge", "type": "number", "description": "Temperature"},
            "plug_ins": {"kind": "counter", "type": "integer", "description": "Plug-ins"},
            "state": {"kind": "state", "type": "string", "enum": ["Idle", "Charging"], "description": "State"},
            "session": {"kind": "event", "type": "object", "description": "Session"}
        }
    }
})")
                         .get<everest::telemetry::TelemetryCatalog>();

struct Fixture {
    explicit Fixture(json rules_document) {
        factories["recording"] = [this](const SinkConfig& config, const SinkEnvironment&) {
            return std::make_unique<RecordingSink>(config.name, received, events);
        };
        ElementCatalog catalog({4, 16});
        catalog.seed(CATALOG);
        router =
            std::make_unique<Router>(std::move(catalog), parse_rules(rules_document), factories, SinkEnvironment{});
    }

    void send(const std::vector<std::uint8_t>& datagram) {
        router->handle_datagram(datagram.data(), datagram.size());
    }

    void send(const json& payload) {
        send(wire::encode_payload(payload));
    }

    void sample(const std::string& producer, const std::string& element, json value) {
        send(wire::encode(wire::Sample{producer, element, 1700000000000, std::move(value)}));
    }

    std::map<std::string, SinkFactory, std::less<>> factories;
    std::vector<Received> received;
    std::vector<std::string> events;
    std::unique_ptr<Router> router;
};

json document(json rules, json default_action = "drop") {
    auto result =
        json::parse(R"({"sinks": {"numbers_only": {"type": "recording"}, "everything": {"type": "recording"}}})");
    result["default_action"] = std::move(default_action);
    result["rules"] = std::move(rules);
    return result;
}

const json STANDARD_RULES = document({
    {{"name", "numbers"},
     {"match", {{"kind", {"gauge", "counter"}}}},
     {"forward", {{{"sink", "numbers_only"}, {"label", "${element}"}}, {{"sink", "everything"}}}}},
    {{"name", "rest"}, {"match", {{"module_type", "EvseManager"}}}, {"forward", {{{"sink", "everything"}}}}},
});

} // namespace

TEST_CASE("Samples are routed to the sinks selected by the rules", "[telemetry_router]") {
    Fixture f(STANDARD_RULES);
    CHECK(f.router->bind_all(true).empty());

    f.sample("evse_manager_1", "temperature", 41.2);
    f.sample("evse_manager_1", "state", "Charging");
    f.sample("evse_manager_1", "session", {{"id", 1}});

    REQUIRE(f.received.size() == 4);
    CHECK(f.received.at(0).sink == "numbers_only");
    CHECK(f.received.at(0).value == 41.2);
    CHECK(f.received.at(0).timestamp_ms == 1700000000000);
    CHECK(f.received.at(0).options == json({{"label", "${element}"}}));
    CHECK(f.received.at(1).sink == "everything");
    CHECK(f.received.at(2).element == "state");
    CHECK(f.received.at(3).value == json({{"id", 1}}));

    const auto& statistics = f.router->statistics();
    CHECK(statistics.samples == 3);
    CHECK(statistics.forwarded == 4);
    CHECK(f.router->sinks_of("evse_manager_1", "temperature") ==
          std::vector<std::string>{"numbers_only", "everything"});

    f.router->enable_sinks();
    f.router->stop_sinks();
    CHECK(f.events ==
          std::vector<std::string>{"enable everything", "enable numbers_only", "stop everything", "stop numbers_only"});
}

TEST_CASE("Invalid, unknown and unrouted samples are counted and dropped", "[telemetry_router]") {
    Fixture f(document({{{"match", {{"element", "temperature"}}}, {"forward", {{{"sink", "everything"}}}}}}));
    f.router->bind_all(true);

    f.sample("unknown_module", "temperature", 1.0);
    f.sample("evse_manager_1", "unknown", 1.0);
    f.sample("evse_manager_1", "temperature", "not a number");
    f.sample("evse_manager_1", "state", "NotInTheEnum");
    f.sample("evse_manager_1", "plug_ins", 1);
    f.send({{"t", "s"}, {"m", "evse_manager_1"}, {"e", "temperature"}});
    f.send({{"t", "x"}});
    const std::vector<std::uint8_t> garbage{'x', 'y', 'z', 'w', '!'};
    f.router->handle_datagram(garbage.data(), garbage.size());
    f.router->handle_truncated();

    CHECK(f.received.empty());
    const auto& statistics = f.router->statistics();
    CHECK(statistics.datagrams == 9);
    CHECK(statistics.unknown_producer == 1);
    CHECK(statistics.unknown_element == 1);
    CHECK(statistics.invalid_value == 2);
    CHECK(statistics.dropped_by_rules == 1);
    CHECK(statistics.decode_errors == 3);
    CHECK(statistics.truncated == 1);
}

TEST_CASE("Bind errors fail strict rules and are reported otherwise", "[telemetry_router]") {
    const auto rules = document({{{"name", "all"}, {"forward", {{{"sink", "numbers_only"}}}}},
                                 {{"name", "never"}, {"match", {{"module_id", "nobody"}}}, {"action", "drop"}}});
    SECTION("Strict") {
        Fixture f(rules);
        CHECK_THROWS_WITH(f.router->bind_all(true),
                          Catch::Matchers::ContainsSubstring("rule 'all' (rules[0]): element 'session' of "
                                                             "'evse_manager_1' to sink 'numbers_only': no events"));
    }
    SECTION("Lenient") {
        Fixture f(rules);
        const auto problems = f.router->bind_all(false);
        REQUIRE(problems.size() == 2);
        CHECK_THAT(problems.at(0), Catch::Matchers::ContainsSubstring("no events"));
        CHECK_THAT(problems.at(1), Catch::Matchers::ContainsSubstring("rule 'never' (rules[1]) matches no telemetry"));
        f.sample("evse_manager_1", "session", json::object());
        f.sample("evse_manager_1", "temperature", 1.0);
        CHECK(f.received.size() == 1);
    }
}

TEST_CASE("Unavailable sink types are rejected", "[telemetry_router]") {
    ElementCatalog catalog;
    CHECK_THROWS_WITH(Router(std::move(catalog),
                             parse_rules(json::parse(R"({"sinks": {"backend": {"type": "otel_nope"}}})")),
                             default_sink_factories(), SinkEnvironment{}),
                      Catch::Matchers::ContainsSubstring("sink type 'otel_nope' is not available"));
}

TEST_CASE("Declared producers are routed like EVerest modules", "[telemetry_router]") {
    Fixture f(document({{{"match", {{"module_id", "ext:*"}}}, {"forward", {{{"sink", "numbers_only"}}}}}},
                       {{"forward", {{{"sink", "everything"}}}}}));
    f.router->bind_all(false);

    const json declare = {{"t", "d"},
                          {"module", "ext:meter"},
                          {"module_type", "ExternalMeter"},
                          {"elements",
                           {{"voltage", {{"kind", "gauge"}, {"type", "number"}, {"description", "V"}}},
                            {"log", {{"kind", "event"}, {"type", "object"}, {"description", "L"}}}}}};
    f.send(declare);
    CHECK(f.router->statistics().declares_accepted == 1);
    f.sample("ext:meter", "voltage", 230.1);
    f.sample("ext:meter", "log", json::object());
    REQUIRE(f.received.size() == 1);
    CHECK(f.received.at(0).sink == "numbers_only");

    f.send(declare);
    CHECK(f.events == std::vector<std::string>{"release numbers_only"});
    f.sample("ext:meter", "voltage", 230.2);
    CHECK(f.received.size() == 2);

    f.send({{"t", "d"}, {"module", "evse_manager_1"}, {"elements", {{"x", {{"kind", "gauge"}, {"type", "number"}}}}}});
    CHECK(f.router->statistics().declares_rejected == 1);
    f.sample("evse_manager_1", "x", 1.0);
    CHECK(f.router->statistics().unknown_element == 1);
}

TEST_CASE("The shipped example rules bind to the example module", "[telemetry_router]") {
    const auto catalog_json = json::parse(R"({
        "telemetry_example": {
            "module_type": "TelemetryExample",
            "mapping": {"evse": 1},
            "elements": {
                "temperature": {"kind": "gauge", "type": "number", "unit": "Celsius", "description": "T"},
                "supply_voltage_raw": {"kind": "gauge", "type": "integer", "description": "V"},
                "plug_ins": {"kind": "counter", "type": "integer", "description": "P"},
                "energy_delivered": {"kind": "counter", "type": "number", "unit": "Wh", "description": "E"},
                "firmware_state": {"kind": "state", "type": "string", "enum": ["Idle", "Measuring", "Error"],
                                   "description": "F"},
                "charge_mode": {"kind": "state", "type": "string", "enum": ["AC", "DC"], "description": "C"},
                "relay_closed": {"kind": "state", "type": "boolean", "description": "R"},
                "firmware_version": {"kind": "state", "type": "string", "description": "V"},
                "cp_event": {"kind": "event", "type": "object", "description": "E"}
            }
        }
    })");
    ElementCatalog catalog;
    catalog.seed(catalog_json);
    const auto rules = parse_rules(
        json(Everest::load_yaml(std::string(TELEMETRY_ROUTER_CONFIG_DIR) + "/telemetry_rules.example.yaml")));
    Router router(std::move(catalog), rules, default_sink_factories(), SinkEnvironment{});

    std::vector<std::string> problems;
    REQUIRE_NOTHROW(problems = router.bind_all(true));
    REQUIRE(problems.size() == 1);
    CHECK_THAT(problems.front(), Catch::Matchers::ContainsSubstring("silence-simulators"));
    CHECK(router.sinks_of("telemetry_example", "firmware_state") == std::vector<std::string>{"csms", "debug"});
    CHECK(router.sinks_of("telemetry_example", "cp_event") == std::vector<std::string>{"debug"});
}
