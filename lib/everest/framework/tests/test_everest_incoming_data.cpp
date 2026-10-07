// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <catch2/catch_test_macros.hpp>

#include <framework/everest.hpp>
#include <tests/mock_mqtt_abstraction.hpp>
#include <utils/config.hpp>
#include <utils/conversions.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

json value_schema() {
    return {{"type", "object"}, {"required", {"x"}}, {"properties", {{"x", {{"type", "integer"}}}}}};
}

json serialized_config() {
    const json interface = {
        {"cmds", {{"set_value", {{"arguments", {{"value", value_schema()}}}, {"result", {{"type", "null"}}}}}}},
        {"vars", {{"state", value_schema()}}},
        {"errors", json::object()}};
    const json manifest = {{"provides", {{"main", {{"interface", "test_if"}}}}},
                           {"requires", {{"r", {{"interface", "test_if"}}}}}};
    return {
        {"module_config",
         {{"standalone", false},
          {"module_name", "TestModule"},
          {"module_id", "test_module"},
          {"configuration_parameters", json::object()},
          {"telemetry_enabled", false},
          {"connections",
           {{"r", {{{"module_id", "test_module"}, {"implementation_id", "main"}, {"requirement", {{"id", "r"}}}}}}}},
          {"mapping", nullptr}}},
        {"manifests", {{"TestModule", manifest}}},
        {"interface_definitions", {{"test_if", interface}}},
        {"module_names", {{"test_module", "TestModule"}}}};
}

// Mirrors what generated code does: convert the json to a typed value before calling module code.
int convert_value(const json& data) {
    return data.at("x").get<int>();
}

class EverestFixture {
public:
    explicit EverestFixture(bool validate_schema = false) :
        mqtt(std::make_shared<Everest::tests::MockMQTTAbstraction>()),
        config(Everest::MQTTSettings{}, serialized_config()),
        everest("test_module", config, validate_schema, mqtt, "telemetry/", false) {
    }

    void deliver(const std::string& topic_suffix, const json& data) {
        for (const auto& [topic, handler] : mqtt->registered_handlers()) {
            if (topic.size() >= topic_suffix.size() &&
                topic.compare(topic.size() - topic_suffix.size(), topic_suffix.size(), topic_suffix) == 0) {
                (*handler->handler)(topic, data);
                return;
            }
        }
        FAIL("No handler registered for topic ending in " << topic_suffix);
    }

    std::vector<json> cmd_responses() const {
        std::vector<json> responses;
        for (const auto& [topic, payload] : mqtt->published()) {
            if (topic.find("/cmd/set_value/response/") != std::string::npos) {
                responses.push_back(payload.at("data").at("data"));
            }
        }
        return responses;
    }

    std::shared_ptr<Everest::tests::MockMQTTAbstraction> mqtt;
    Everest::Config config;
    Everest::Everest everest;
};

json cmd_message(const json& args) {
    return {{"id", "call-1"}, {"origin", "caller"}, {"args", args}};
}

std::string response_error_type(const json& response) {
    return response.at("error").at(Everest::conversions::ERROR_TYPE).get<std::string>();
}

} // namespace

TEST_CASE("Incoming var that fails conversion is dropped", "[everest][incoming_data]") {
    EverestFixture fixture;
    std::vector<int> received;
    fixture.everest.subscribe_var({"r", 0}, "state",
                                  [&received](const json& data) { received.push_back(convert_value(data)); });

    CHECK_NOTHROW(fixture.deliver("/var/state", json::object()));
    CHECK_NOTHROW(fixture.deliver("/var/state", json{{"x", "not an integer"}}));
    fixture.deliver("/var/state", json{{"x", 3}});

    REQUIRE(received.size() == 1);
    CHECK(received[0] == 3);
}

TEST_CASE("Module exception on a valid incoming var is propagated", "[everest][incoming_data]") {
    EverestFixture fixture;
    fixture.everest.subscribe_var({"r", 0}, "state", [](const json& data) {
        convert_value(data);
        throw std::runtime_error("module failure");
    });

    CHECK_THROWS_AS(fixture.deliver("/var/state", json{{"x", 3}}), std::runtime_error);
}

TEST_CASE("Incoming cmd with arguments that fail conversion is answered with an error", "[everest][incoming_data]") {
    EverestFixture fixture;
    fixture.everest.provide_cmd("main", "set_value", [](const json& args) {
        convert_value(args.at("value"));
        return json{};
    });

    CHECK_NOTHROW(fixture.deliver("/cmd/set_value", cmd_message({{"value", {{"x", "not an integer"}}}})));
    CHECK_NOTHROW(fixture.deliver("/cmd/set_value", {{"id", "call-2"}, {"origin", "caller"}}));

    const auto responses = fixture.cmd_responses();
    REQUIRE(responses.size() == 2);
    for (const auto& response : responses) {
        CHECK(response_error_type(response) ==
              Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::SchemaValidationError));
    }
}

TEST_CASE("Module exception on a valid incoming cmd is propagated", "[everest][incoming_data]") {
    EverestFixture fixture;
    fixture.everest.provide_cmd("main", "set_value", [](const json& args) -> json {
        convert_value(args.at("value"));
        throw std::runtime_error("module failure");
    });

    CHECK_THROWS_AS(fixture.deliver("/cmd/set_value", cmd_message({{"value", {{"x", 3}}}})), std::runtime_error);

    const auto responses = fixture.cmd_responses();
    REQUIRE(responses.size() == 1);
    CHECK(response_error_type(responses[0]) ==
          Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::HandlerException));
}

TEST_CASE("Incoming cmd without a valid origin is ignored", "[everest][incoming_data]") {
    EverestFixture fixture;
    bool called = false;
    fixture.everest.provide_cmd("main", "set_value", [&called](const json&) {
        called = true;
        return json{};
    });

    const json value = {{"value", {{"x", 3}}}};
    CHECK_NOTHROW(fixture.deliver("/cmd/set_value", {{"id", "call-1"}, {"args", value}}));
    CHECK_NOTHROW(fixture.deliver("/cmd/set_value", {{"id", "call-1"}, {"origin", 5}, {"args", value}}));
    CHECK_NOTHROW(fixture.deliver("/cmd/set_value", {{"id", "call-1"}, {"origin", ""}, {"args", value}}));
    CHECK_NOTHROW(fixture.deliver("/cmd/set_value", {{"id", "call-1"}, {"origin", "a/#"}, {"args", value}}));
    CHECK_NOTHROW(fixture.deliver("/cmd/set_value", {{"origin", "caller"}, {"args", value}}));

    CHECK_FALSE(called);
    CHECK(fixture.cmd_responses().empty());
}

TEST_CASE("Incoming data is rejected before module code when schema validation is enabled",
          "[everest][incoming_data]") {
    EverestFixture fixture(true);
    bool var_called = false;
    bool cmd_called = false;
    fixture.everest.subscribe_var({"r", 0}, "state", [&var_called](const json&) { var_called = true; });
    fixture.everest.provide_cmd("main", "set_value", [&cmd_called](const json&) {
        cmd_called = true;
        return json{};
    });

    CHECK_NOTHROW(fixture.deliver("/var/state", json::object()));
    CHECK_NOTHROW(fixture.deliver("/cmd/set_value", cmd_message({{"value", json::object()}})));

    CHECK_FALSE(var_called);
    CHECK_FALSE(cmd_called);
    REQUIRE(fixture.cmd_responses().size() == 1);
}

TEST_CASE("Cmd result arriving after the call gave up is ignored", "[everest][cmd_result]") {
    EverestFixture fixture;
    // Once shutdown is processed, call_cmd gives up after a single wait step instead of the full timeout.
    fixture.deliver("shutdown", json::object());

    CHECK_THROWS_AS(fixture.everest.call_cmd({"r", 0}, "set_value", {{"value", {{"x", 1}}}}), Everest::Shutdown);

    std::string call_id;
    for (const auto& [topic, payload] : fixture.mqtt->published()) {
        if (topic.size() >= 14 && topic.compare(topic.size() - 14, 14, "/cmd/set_value") == 0) {
            call_id = payload.at("data").at("id").get<std::string>();
        }
    }
    REQUIRE_FALSE(call_id.empty());

    std::shared_ptr<TypedHandler> result_handler;
    std::string response_topic;
    for (const auto& [topic, handler] : fixture.mqtt->handler_history()) {
        if (handler->type == HandlerType::Result && handler->id == call_id) {
            result_handler = handler;
            response_topic = topic;
        }
    }
    REQUIRE(result_handler != nullptr);

    CHECK(fixture.mqtt->registered_handlers().count(response_topic) == 0);
    CHECK_NOTHROW((*result_handler->handler)(response_topic, json{{"id", call_id}, {"retval", nullptr}}));
}
