// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <stdexcept>

#include "ocpp_sink.hpp"

using namespace telemetry_router;
using nlohmann::json;
using namespace std::chrono_literals;

namespace {

class FakeOcppClient : public IOcppClient {
public:
    std::vector<OcppWriteStatus> set_variables(const std::vector<OcppWrite>& writes) override {
        batches.push_back(writes);
        if (fail) {
            throw std::runtime_error("ocpp is gone");
        }
        std::vector<OcppWriteStatus> statuses;
        for (const auto& write : writes) {
            const auto it = status_by_variable.find(write.variable_name);
            statuses.push_back(it != status_by_variable.end() ? it->second : default_status);
        }
        return statuses;
    }

    std::vector<std::vector<OcppWrite>> batches;
    std::map<std::string, OcppWriteStatus> status_by_variable;
    OcppWriteStatus default_status{OcppWriteStatus::Accepted};
    bool fail{false};
};

struct Fixture {
    Fixture() {
        producer.id = "powermeter_1";
        producer.type = "GenericPowermeter";
        producer.evse = 2;
        producer.connector = 1;
        temperature.name = "temperature";
        temperature.kind = Kind::Gauge;
        temperature.value_type = ValueType::Number;
        state.name = "state";
        state.kind = Kind::State;
        state.value_type = ValueType::String;
        state.enum_values = {"Idle", "Charging"};
        event.name = "diagnostics";
        event.kind = Kind::Event;
        event.value_type = ValueType::Object;
    }

    std::unique_ptr<OcppSink> make_sink(IOcppClient* ocpp_client, json options = json::object()) {
        OcppSinkOptions defaults;
        defaults.flush_interval = 1000ms;
        defaults.min_interval = 10s;
        defaults.run_flush_thread = false;
        defaults.clock = [this] { return now; };
        auto sink = std::make_unique<OcppSink>("csms", options, ocpp_client, defaults);
        sink->enable();
        return sink;
    }

    static Target target(json options) {
        return {"csms", std::move(options)};
    }

    void submit(OcppSink& sink, const ElementDeclaration& element, const SinkBinding& binding, json value) {
        sink.submit(Sample{producer, element, 0, value}, binding);
    }

    FakeOcppClient client;
    Producer producer;
    ElementDeclaration temperature;
    ElementDeclaration state;
    ElementDeclaration event;
    std::chrono::steady_clock::time_point now{std::chrono::steady_clock::time_point() + 1000s};
};

const json TEMPERATURE_TARGET = {{"component", {{"name", "Telemetry"}, {"instance", "${module_id}"}}},
                                 {"variable", {{"name", "Temperature"}}}};

} // namespace

TEST_CASE("OCPP targets resolve names and EVSE from the producer", "[telemetry_router]") {
    Fixture f;
    auto sink = f.make_sink(&f.client);

    auto binding = sink->bind(Fixture::target(TEMPERATURE_TARGET), f.producer, f.temperature);
    f.submit(*sink, f.temperature, *binding, 41.25);
    sink->flush();

    REQUIRE(f.client.batches.size() == 1);
    const auto& write = f.client.batches.at(0).at(0);
    CHECK(write.component_name == "Telemetry");
    CHECK(write.component_instance == std::optional<std::string>("powermeter_1"));
    CHECK(write.evse == std::optional<int>(2));
    CHECK(write.connector == std::optional<int>(1));
    CHECK(write.variable_name == "Temperature");
    CHECK_FALSE(write.variable_instance.has_value());
    CHECK(write.value == "41.25");

    SECTION("Fixed and omitted EVSE") {
        auto fixed = sink->bind(Fixture::target({{"component", {{"name", "A"}, {"evse", 3}, {"connector", "none"}}},
                                                 {"variable", {{"name", "V"}}}}),
                                f.producer, f.temperature);
        auto station = sink->bind(Fixture::target({{"component", {{"name", "B"}, {"evse", "none"}, {"connector", 1}}},
                                                   {"variable", {{"name", "V"}}}}),
                                  f.producer, f.temperature);
        f.submit(*sink, f.temperature, *fixed, 1);
        f.submit(*sink, f.temperature, *station, 1);
        sink->flush();
        REQUIRE(f.client.batches.size() == 2);
        CHECK(f.client.batches.at(1).at(0).evse == std::optional<int>(3));
        CHECK_FALSE(f.client.batches.at(1).at(0).connector.has_value());
        CHECK_FALSE(f.client.batches.at(1).at(1).evse.has_value());
        CHECK_FALSE(f.client.batches.at(1).at(1).connector.has_value());
    }
}

TEST_CASE("OCPP targets that cannot work are rejected when binding", "[telemetry_router]") {
    Fixture f;
    auto sink = f.make_sink(&f.client);
    CHECK_THROWS_WITH(sink->bind(Fixture::target(TEMPERATURE_TARGET), f.producer, f.event),
                      Catch::Matchers::ContainsSubstring("accepts only gauge, counter and state"));
    CHECK_THROWS_AS(sink->bind(Fixture::target({{"variable", {{"name", "V"}}}}), f.producer, f.temperature), BindError);
    CHECK_THROWS_AS(
        sink->bind(Fixture::target({{"component", {{"name", std::string(51, 'x')}}}, {"variable", {{"name", "V"}}}}),
                   f.producer, f.temperature),
        BindError);
    CHECK_THROWS_AS(sink->bind(Fixture::target({{"component", {{"name", "${nope}"}}}, {"variable", {{"name", "V"}}}}),
                               f.producer, f.temperature),
                    BindError);
    CHECK_THROWS_AS(
        sink->bind(Fixture::target({{"component", {{"name", "C"}, {"evse", 0}}}, {"variable", {{"name", "V"}}}}),
                   f.producer, f.temperature),
        BindError);
    CHECK_NOTHROW(sink->bind(
        Fixture::target({{"component", {{"name", "C"}}}, {"variable", {{"name", "Numeric options"}}}, {"decimals", 2}}),
        f.producer, f.state));
    CHECK_THROWS_AS(
        sink->bind(Fixture::target({{"component", {{"name", "C"}}}, {"variable", {{"name", "V"}}}, {"colour", 2}}),
                   f.producer, f.temperature),
        BindError);

    auto first = sink->bind(Fixture::target(TEMPERATURE_TARGET), f.producer, f.temperature);
    CHECK_THROWS_WITH(sink->bind(Fixture::target(TEMPERATURE_TARGET), f.producer, f.temperature),
                      Catch::Matchers::ContainsSubstring("already written by powermeter_1.temperature"));
    sink->release(*first);
    CHECK_NOTHROW(sink->bind(Fixture::target(TEMPERATURE_TARGET), f.producer, f.temperature));
}

TEST_CASE("OCPP writes are coalesced and rate limited", "[telemetry_router]") {
    Fixture f;
    auto sink = f.make_sink(&f.client);
    auto options = TEMPERATURE_TARGET;
    options["deadband"] = 0.5;
    auto binding = sink->bind(Fixture::target(options), f.producer, f.temperature);

    f.submit(*sink, f.temperature, *binding, 40.0);
    f.submit(*sink, f.temperature, *binding, 41.0);
    sink->flush();
    REQUIRE(f.client.batches.size() == 1);
    CHECK(f.client.batches.at(0).at(0).value == "41");

    f.now += 20s;
    f.submit(*sink, f.temperature, *binding, 41.0);
    f.submit(*sink, f.temperature, *binding, 41.4);
    sink->flush();
    CHECK(f.client.batches.size() == 1);

    f.submit(*sink, f.temperature, *binding, 43.0);
    f.now += 1s;
    sink->flush();
    REQUIRE(f.client.batches.size() == 2);
    CHECK(f.client.batches.at(1).at(0).value == "43");

    f.submit(*sink, f.temperature, *binding, 44.0);
    f.now += 5s;
    sink->flush();
    CHECK(f.client.batches.size() == 2);
    f.now += 5s;
    sink->flush();
    REQUIRE(f.client.batches.size() == 3);
    CHECK(f.client.batches.at(2).at(0).value == "44");
}

TEST_CASE("OCPP writes are batched and nothing is written before the sink is enabled", "[telemetry_router]") {
    Fixture f;
    OcppSinkOptions defaults;
    defaults.run_flush_thread = false;
    defaults.clock = [&f] { return f.now; };
    OcppSink sink("csms", {{"max_batch", 2}}, &f.client, defaults);

    std::vector<std::unique_ptr<SinkBinding>> bindings;
    for (int i = 0; i < 5; ++i) {
        bindings.push_back(sink.bind(
            Fixture::target({{"component", {{"name", "C"}}}, {"variable", {{"name", "V" + std::to_string(i)}}}}),
            f.producer, f.temperature));
        f.submit(sink, f.temperature, *bindings.back(), i);
    }
    sink.flush();
    CHECK(f.client.batches.empty());

    sink.enable();
    sink.flush();
    REQUIRE(f.client.batches.size() == 3);
    CHECK(f.client.batches.at(0).size() == 2);
    CHECK(f.client.batches.at(2).size() == 1);
    sink.stop();
}

TEST_CASE("Unknown OCPP variables are retried rarely", "[telemetry_router]") {
    Fixture f;
    auto sink = f.make_sink(&f.client, {{"dead_retry_s", 600}});
    auto missing = sink->bind(Fixture::target({{"component", {{"name", "C"}}}, {"variable", {{"name", "Missing"}}}}),
                              f.producer, f.temperature);
    auto present = sink->bind(Fixture::target(TEMPERATURE_TARGET), f.producer, f.temperature);
    f.client.status_by_variable["Missing"] = OcppWriteStatus::UnknownVariable;

    f.submit(*sink, f.temperature, *missing, 1);
    f.submit(*sink, f.temperature, *present, 1);
    sink->flush();
    REQUIRE(f.client.batches.size() == 1);

    f.submit(*sink, f.temperature, *missing, 2);
    f.now += 60s;
    sink->flush();
    CHECK(f.client.batches.size() == 1);

    f.now += 600s;
    sink->flush();
    REQUIRE(f.client.batches.size() == 2);
    CHECK(f.client.batches.at(1).at(0).variable_name == "Missing");
    CHECK(f.client.batches.at(1).at(0).value == "2");
}

TEST_CASE("OCPP sinks back off while OCPP is not ready or unreachable", "[telemetry_router]") {
    Fixture f;
    auto sink = f.make_sink(&f.client);
    auto binding = sink->bind(Fixture::target(TEMPERATURE_TARGET), f.producer, f.temperature);
    f.submit(*sink, f.temperature, *binding, 1);

    SECTION("Everything rejected before the first accepted write") {
        f.client.default_status = OcppWriteStatus::Rejected;
        sink->flush();
        sink->flush();
        CHECK(f.client.batches.size() == 1);
        f.now += 1s;
        f.client.default_status = OcppWriteStatus::Accepted;
        sink->flush();
        REQUIRE(f.client.batches.size() == 2);
        CHECK(f.client.batches.at(1).at(0).value == "1");
    }
    SECTION("A rejected value after the first accepted write is dropped") {
        sink->flush();
        f.client.default_status = OcppWriteStatus::Rejected;
        f.submit(*sink, f.temperature, *binding, 2);
        f.now += 10s;
        sink->flush();
        REQUIRE(f.client.batches.size() == 2);
        f.now += 10s;
        sink->flush();
        CHECK(f.client.batches.size() == 2);
    }
    SECTION("The OCPP call fails") {
        f.client.fail = true;
        sink->flush();
        sink->flush();
        CHECK(f.client.batches.size() == 1);
        f.client.fail = false;
        f.now += 1s;
        sink->flush();
        CHECK(f.client.batches.size() == 2);
    }
}

TEST_CASE("OCPP sinks check values against the element", "[telemetry_router]") {
    Fixture f;
    auto sink = f.make_sink(&f.client);
    auto binding = sink->bind(Fixture::target({{"component", {{"name", "C"}}}, {"variable", {{"name", "State"}}}}),
                              f.producer, f.state);
    f.submit(*sink, f.state, *binding, "Unknown");
    sink->flush();
    CHECK(f.client.batches.empty());
    f.submit(*sink, f.state, *binding, "Charging");
    sink->flush();
    REQUIRE(f.client.batches.size() == 1);
    CHECK(f.client.batches.at(0).at(0).value == "Charging");
}

TEST_CASE("OCPP sinks without an OCPP connection only log", "[telemetry_router]") {
    Fixture f;
    auto sink = f.make_sink(nullptr);
    auto binding = sink->bind(Fixture::target(TEMPERATURE_TARGET), f.producer, f.temperature);
    f.submit(*sink, f.temperature, *binding, 1);
    CHECK_NOTHROW(sink->flush());
}
