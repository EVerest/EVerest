// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <mutex>

#include <opentelemetry/sdk/logs/read_write_log_record.h>

#include "otel_sink.hpp"

using namespace telemetry_router;
using nlohmann::json;
namespace otel = opentelemetry;

namespace {

struct ExportedPoint {
    std::string metric;
    std::string unit;
    bool sum;
    std::map<std::string, std::string> attributes;
    double value;
};

struct ExportedLog {
    std::string body;
    std::string event_name;
    std::map<std::string, std::string> attributes;
    std::int64_t timestamp_ms;
};

struct Captured {
    std::mutex mutex;
    std::vector<ExportedPoint> points;
    std::vector<ExportedLog> logs;
    std::vector<std::string> urls;
    std::map<std::string, std::string> headers;
};

std::string to_text(const otel::sdk::common::OwnedAttributeValue& value) {
    if (const auto* text = otel::nostd::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto* integer = otel::nostd::get_if<std::int64_t>(&value)) {
        return std::to_string(*integer);
    }
    if (const auto* flag = otel::nostd::get_if<bool>(&value)) {
        return *flag ? "true" : "false";
    }
    if (const auto* number = otel::nostd::get_if<double>(&value)) {
        return std::to_string(*number);
    }
    return "?";
}

double to_double(const otel::sdk::metrics::ValueType& value) {
    if (const auto* integer = otel::nostd::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*integer);
    }
    return otel::nostd::get<double>(value);
}

class CapturingMetricExporter : public otel::sdk::metrics::PushMetricExporter {
public:
    explicit CapturingMetricExporter(std::shared_ptr<Captured> captured) : m_captured(std::move(captured)) {
    }

    otel::sdk::common::ExportResult Export(const otel::sdk::metrics::ResourceMetrics& data) noexcept override {
        const std::lock_guard lock(m_captured->mutex);
        for (const auto& scope : data.scope_metric_data_) {
            for (const auto& metric : scope.metric_data_) {
                for (const auto& point : metric.point_data_attr_) {
                    ExportedPoint exported{
                        metric.instrument_descriptor.name_, metric.instrument_descriptor.unit_, false, {}, 0.0};
                    for (const auto& [key, value] : point.attributes) {
                        exported.attributes[key] = to_text(value);
                    }
                    if (const auto* sum = otel::nostd::get_if<otel::sdk::metrics::SumPointData>(&point.point_data)) {
                        exported.sum = true;
                        exported.value = to_double(sum->value_);
                    } else if (const auto* last =
                                   otel::nostd::get_if<otel::sdk::metrics::LastValuePointData>(&point.point_data)) {
                        exported.value = to_double(last->value_);
                    }
                    m_captured->points.push_back(exported);
                }
            }
        }
        return otel::sdk::common::ExportResult::kSuccess;
    }

    otel::sdk::metrics::AggregationTemporality
    GetAggregationTemporality(otel::sdk::metrics::InstrumentType) const noexcept override {
        return otel::sdk::metrics::AggregationTemporality::kCumulative;
    }

    bool ForceFlush(std::chrono::microseconds) noexcept override {
        return true;
    }

    bool Shutdown(std::chrono::microseconds) noexcept override {
        return true;
    }

private:
    std::shared_ptr<Captured> m_captured;
};

class CapturingLogExporter : public otel::sdk::logs::LogRecordExporter {
public:
    explicit CapturingLogExporter(std::shared_ptr<Captured> captured) : m_captured(std::move(captured)) {
    }

    std::unique_ptr<otel::sdk::logs::Recordable> MakeRecordable() noexcept override {
        return std::make_unique<otel::sdk::logs::ReadWriteLogRecord>();
    }

    otel::sdk::common::ExportResult
    Export(const otel::nostd::span<std::unique_ptr<otel::sdk::logs::Recordable>>& records) noexcept override {
        const std::lock_guard lock(m_captured->mutex);
        for (const auto& record : records) {
            const auto& log = static_cast<const otel::sdk::logs::ReadWriteLogRecord&>(*record);
            ExportedLog exported{
                to_text(log.GetBody()),
                std::string(log.GetEventName()),
                {},
                std::chrono::duration_cast<std::chrono::milliseconds>(log.GetTimestamp().time_since_epoch()).count()};
            for (const auto& [key, value] : log.GetAttributes()) {
                exported.attributes[key] = to_text(value);
            }
            m_captured->logs.push_back(exported);
        }
        return otel::sdk::common::ExportResult::kSuccess;
    }

    bool ForceFlush(std::chrono::microseconds) noexcept override {
        return true;
    }

    bool Shutdown(std::chrono::microseconds) noexcept override {
        return true;
    }

private:
    std::shared_ptr<Captured> m_captured;
};

OtelExporterFactory capturing_factory(const std::shared_ptr<Captured>& captured) {
    OtelExporterFactory factory;
    factory.metrics = [captured](const std::string& url, const std::map<std::string, std::string>& headers) {
        captured->urls.push_back(url);
        captured->headers = headers;
        return std::make_unique<CapturingMetricExporter>(captured);
    };
    factory.logs = [captured](const std::string& url, const std::map<std::string, std::string>&) {
        captured->urls.push_back(url);
        return std::make_unique<CapturingLogExporter>(captured);
    };
    return factory;
}

ElementDeclaration element(const std::string& name, Kind kind, ValueType value_type) {
    ElementDeclaration declaration;
    declaration.name = name;
    declaration.kind = kind;
    declaration.value_type = value_type;
    declaration.description = name;
    return declaration;
}

const ExportedPoint* find_point(const Captured& captured, const std::string& metric,
                                const std::map<std::string, std::string>& attributes = {}) {
    for (auto it = captured.points.rbegin(); it != captured.points.rend(); ++it) {
        if (it->metric != metric) {
            continue;
        }
        const bool all = std::all_of(attributes.begin(), attributes.end(), [&it](const auto& attribute) {
            const auto found = it->attributes.find(attribute.first);
            return found != it->attributes.end() and found->second == attribute.second;
        });
        if (all) {
            return &*it;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("OTel sinks export gauges and counters as metrics", "[telemetry_router][otel]") {
    auto captured = std::make_shared<Captured>();
    OtelSink sink("backend",
                  json::parse(R"({"endpoint": "http://collector:4318/", "headers": {"Authorization": "Bearer x"},
                                 "export_interval_ms": 60000})"),
                  capturing_factory(captured));
    CHECK(captured->urls ==
          std::vector<std::string>{"http://collector:4318/v1/metrics", "http://collector:4318/v1/logs"});
    CHECK(captured->headers.at("Authorization") == "Bearer x");

    Producer producer;
    producer.id = "evse_manager_1";
    producer.type = "EvseManager";
    producer.evse = 1;
    auto temperature = element("temperature", Kind::Gauge, ValueType::Number);
    temperature.unit = "Celsius";
    const auto plug_ins = element("plug_ins", Kind::Counter, ValueType::Integer);

    auto temperature_binding = sink.bind({"backend", json::object()}, producer, temperature);
    auto plug_ins_binding =
        sink.bind({"backend", {{"metric_name", "everest.plugins.${module_id}"}}}, producer, plug_ins);
    CHECK_THROWS_AS(sink.bind({"backend", {{"colour", 1}}}, producer, temperature), BindError);

    sink.submit({producer, temperature, 1700000000000, 41.5}, *temperature_binding);
    sink.submit({producer, plug_ins, 1700000000000, 7}, *plug_ins_binding);
    sink.flush();

    const std::lock_guard lock(captured->mutex);
    const auto* gauge = find_point(*captured, "everest.EvseManager.temperature");
    REQUIRE(gauge != nullptr);
    CHECK_FALSE(gauge->sum);
    CHECK(gauge->unit == "Celsius");
    CHECK(gauge->value == 41.5);
    CHECK(gauge->attributes.at("everest.module.id") == "evse_manager_1");
    CHECK(gauge->attributes.at("everest.module.type") == "EvseManager");
    CHECK(gauge->attributes.at("everest.evse.id") == "1");

    const auto* counter = find_point(*captured, "everest.plugins.evse_manager_1");
    REQUIRE(counter != nullptr);
    CHECK(counter->sum);
    CHECK(counter->value == 7.0);
}

TEST_CASE("OTel sinks export states and events as log records", "[telemetry_router][otel]") {
    auto captured = std::make_shared<Captured>();
    OtelSink sink("backend", {{"states_as_metrics", true}}, capturing_factory(captured));

    Producer producer;
    producer.id = "meter";
    producer.type = "Meter";
    auto state = element("state", Kind::State, ValueType::String);
    state.enum_values = {"Idle", "Charging"};
    const auto relay = element("relay", Kind::State, ValueType::Boolean);
    const auto session = element("session", Kind::Event, ValueType::Object);

    auto state_binding = sink.bind({"backend", json::object()}, producer, state);
    auto relay_binding = sink.bind({"backend", json::object()}, producer, relay);
    auto session_binding = sink.bind({"backend", json::object()}, producer, session);

    sink.submit({producer, state, 1700000000000, "Charging"}, *state_binding);
    sink.submit({producer, state, 1700000000500, "Charging"}, *state_binding);
    sink.submit({producer, relay, 1700000000000, true}, *relay_binding);
    sink.submit({producer, session, 1700000001000, {{"id", "s1"}, {"energy_wh", 1200}, {"meter", {{"a", 1}}}}},
                *session_binding);
    sink.flush();

    const std::lock_guard lock(captured->mutex);
    REQUIRE(captured->logs.size() == 3);
    CHECK(captured->logs.at(0).body == "Charging");
    CHECK(captured->logs.at(0).event_name == "everest.Meter.state");
    CHECK(captured->logs.at(0).attributes.at("event.name") == "everest.Meter.state");
    CHECK(captured->logs.at(0).timestamp_ms == 1700000000000);
    CHECK(captured->logs.at(1).body == "true");
    CHECK(json::parse(captured->logs.at(2).body).at("energy_wh") == 1200);
    CHECK(captured->logs.at(2).attributes.at("everest.event.id") == "s1");
    CHECK(captured->logs.at(2).attributes.at("everest.event.energy_wh") == "1200");
    CHECK(captured->logs.at(2).attributes.count("everest.event.meter") == 0);

    CHECK(find_point(*captured, "everest.Meter.state", {{"everest.state", "Charging"}})->value == 1.0);
    CHECK(find_point(*captured, "everest.Meter.state", {{"everest.state", "Idle"}})->value == 0.0);
    CHECK(find_point(*captured, "everest.Meter.relay")->value == 1.0);
}

TEST_CASE("OTel sinks reject invalid options", "[telemetry_router][otel]") {
    auto captured = std::make_shared<Captured>();
    CHECK_THROWS_AS(OtelSink("backend", {{"endpont", "x"}}, capturing_factory(captured)), RulesError);
    CHECK_THROWS_AS(OtelSink("backend", {{"headers", {{"a", 1}}}}, capturing_factory(captured)), RulesError);
    CHECK_THROWS_AS(OtelSink("backend", {{"export_interval_ms", 0}}, capturing_factory(captured)), RulesError);

    OtelSink sink("backend", json::object(), capturing_factory(captured));
    Producer producer;
    producer.id = "p";
    producer.type = "P";
    sink.bind({"backend", {{"metric_name", "shared"}}}, producer, element("a", Kind::Gauge, ValueType::Number));
    CHECK_THROWS_AS(
        sink.bind({"backend", {{"metric_name", "shared"}}}, producer, element("b", Kind::Counter, ValueType::Number)),
        BindError);
}
