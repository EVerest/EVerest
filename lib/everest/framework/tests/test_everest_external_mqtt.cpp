// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <catch2/catch_test_macros.hpp>

#include <framework/everest.hpp>
#include <tests/mock_mqtt_abstraction.hpp>
#include <utils/config.hpp>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

namespace {

constexpr auto EXTERNAL_TOPIC = "external/cmd";

json minimal_serialized_config() {
    return {{"module_config",
             {{"standalone", false},
              {"module_name", "TestModule"},
              {"module_id", "test_module"},
              {"configuration_parameters", json::object()},
              {"telemetry_enabled", false},
              {"connections", json::object()},
              {"mapping", nullptr}}},
            {"manifests",
             {{"TestModule",
               {{"enable_external_mqtt", true}, {"provides", json::object()}, {"requires", json::object()}}}}},
            {"module_names", {{"test_module", "TestModule"}}}};
}

class EverestFixture {
public:
    EverestFixture() :
        mqtt(std::make_shared<Everest::tests::MockMQTTAbstraction>()),
        config(Everest::MQTTSettings{}, minimal_serialized_config()),
        everest("test_module", config, false, mqtt, "telemetry/", false) {
    }

    void deliver(const json& data) {
        const auto& handlers = mqtt->registered_handlers();
        const auto it = handlers.find(EXTERNAL_TOPIC);
        REQUIRE(it != handlers.end());
        (*it->second->handler)(EXTERNAL_TOPIC, data);
    }

    std::shared_ptr<Everest::tests::MockMQTTAbstraction> mqtt;
    Everest::Config config;
    Everest::Everest everest;
};

} // namespace

TEST_CASE("External MQTT handler exceptions do not escape the framework", "[everest][external_mqtt]") {
    EverestFixture fixture;

    SECTION("std::exception") {
        fixture.everest.provide_external_mqtt_handler(
            EXTERNAL_TOPIC, StringHandler([](const std::string&) { throw std::runtime_error("bad payload"); }));
        CHECK_NOTHROW(fixture.deliver(json("not json")));
    }

    SECTION("non-std exception") {
        fixture.everest.provide_external_mqtt_handler(EXTERNAL_TOPIC,
                                                      StringHandler([](const std::string&) { throw 42; }));
        CHECK_NOTHROW(fixture.deliver(json("payload")));
    }

    SECTION("string pair handler") {
        fixture.everest.provide_external_mqtt_handler(
            EXTERNAL_TOPIC,
            StringPairHandler([](const std::string&, const std::string&) { throw std::runtime_error("bad payload"); }));
        CHECK_NOTHROW(fixture.deliver(json("payload")));
    }
}

TEST_CASE("External MQTT string handler ignores non-string data", "[everest][external_mqtt]") {
    EverestFixture fixture;
    std::optional<std::string> received;

    fixture.everest.provide_external_mqtt_handler(
        EXTERNAL_TOPIC, StringHandler([&received](const std::string& data) { received = data; }));

    CHECK_NOTHROW(fixture.deliver(json{{"key", 1}}));
    CHECK_FALSE(received.has_value());

    fixture.deliver(json("payload"));
    REQUIRE(received.has_value());
    CHECK(received.value() == "payload");
}

TEST_CASE("External MQTT string pair handler receives non-string data serialized", "[everest][external_mqtt]") {
    EverestFixture fixture;
    std::optional<std::pair<std::string, std::string>> received;

    fixture.everest.provide_external_mqtt_handler(
        EXTERNAL_TOPIC, StringPairHandler([&received](const std::string& topic, const std::string& data) {
            received = std::make_pair(topic, data);
        }));

    fixture.deliver(json{{"key", 1}});
    REQUIRE(received.has_value());
    CHECK(received->first == EXTERNAL_TOPIC);
    CHECK(received->second == R"({"key":1})");
}
