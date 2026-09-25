// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "otel_sink.hpp"

#include <algorithm>
#include <set>
#include <variant>

#include <opentelemetry/common/key_value_iterable_view.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>
#include <opentelemetry/logs/severity.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor_factory.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor_options.h>
#include <opentelemetry/sdk/logs/logger_provider_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/view/view_registry.h>
#include <opentelemetry/sdk/resource/resource.h>

#include <fmt/format.h>

namespace telemetry_router {

namespace otel = opentelemetry;

namespace {

using Attributes = std::map<std::string, std::string>;

const std::set<std::string> SINK_OPTIONS = {"endpoint",      "headers",           "export_interval_ms",  "resource",
                                            "stale_after_s", "states_as_metrics", "flatten_event_fields"};
const std::set<std::string> TARGET_OPTIONS = {"metric_name"};

constexpr auto DEFAULT_ENDPOINT = "http://localhost:4318";
constexpr std::int64_t DEFAULT_EXPORT_INTERVAL_MS = 30000;

std::map<std::string, std::string> string_map(const nlohmann::json& options, const char* key,
                                              const std::string& where) {
    std::map<std::string, std::string> result;
    const auto it = options.find(key);
    if (it == options.end()) {
        return result;
    }
    if (not it->is_object()) {
        throw RulesError(fmt::format("{}: {} must be a mapping of strings", where, key));
    }
    for (const auto& [name, value] : it->items()) {
        if (not value.is_string()) {
            throw RulesError(fmt::format("{}: {}.{} must be a string", where, key, name));
        }
        result[name] = value.get<std::string>();
    }
    return result;
}

Attributes producer_attributes(const Producer& producer, const ElementDeclaration& element) {
    Attributes attributes = {
        {"everest.module.id", producer.id}, {"everest.module.type", producer.type}, {"everest.element", element.name}};
    if (producer.evse.has_value()) {
        attributes["everest.evse.id"] = std::to_string(producer.evse.value());
        if (producer.connector.has_value()) {
            attributes["everest.connector.id"] = std::to_string(producer.connector.value());
        }
    }
    return attributes;
}

otel::common::SystemTimestamp to_timestamp(std::int64_t unix_ms) {
    return otel::common::SystemTimestamp(std::chrono::system_clock::time_point(std::chrono::milliseconds(unix_ms)));
}

} // namespace

struct OtelSink::Series {
    Attributes attributes;
    std::variant<std::monostate, std::int64_t, double> value;
    std::chrono::steady_clock::time_point updated;
};

struct OtelSink::Instrument {
    std::string name;
    Kind kind;
    bool integer;
    bool state_set;
    std::chrono::seconds stale_after;
    std::mutex* mutex;
    otel::nostd::shared_ptr<otel::metrics::ObservableInstrument> handle;
    std::map<std::string, Series> series;
};

struct OtelSink::Binding : SinkBinding {
    Instrument* instrument{nullptr};
    std::string series_key;
    Attributes attributes;
    std::string event_name;
    bool log{false};
    mutable std::optional<std::string> last_state;
};

OtelExporterFactory otlp_http_exporter_factory() {
    OtelExporterFactory factory;
    factory.metrics = [](const std::string& url, const std::map<std::string, std::string>& headers) {
        otel::exporter::otlp::OtlpHttpMetricExporterOptions options;
        options.url = url;
        options.http_headers.insert(headers.begin(), headers.end());
        options.aggregation_temporality = otel::exporter::otlp::PreferredAggregationTemporality::kCumulative;
        return otel::exporter::otlp::OtlpHttpMetricExporterFactory::Create(options);
    };
    factory.logs = [](const std::string& url, const std::map<std::string, std::string>& headers) {
        otel::exporter::otlp::OtlpHttpLogRecordExporterOptions options;
        options.url = url;
        options.http_headers.insert(headers.begin(), headers.end());
        return otel::exporter::otlp::OtlpHttpLogRecordExporterFactory::Create(options);
    };
    return factory;
}

OtelSink::OtelSink(std::string name, const nlohmann::json& options, OtelExporterFactory exporters) :
    Sink(std::move(name)) {
    const auto where = fmt::format("sinks.{}", this->name());
    for (const auto& [key, value] : options.items()) {
        if (SINK_OPTIONS.count(key) == 0) {
            throw RulesError(fmt::format("{}: unknown option '{}' for an otel sink", where, key));
        }
    }
    auto endpoint = options.value("endpoint", std::string(DEFAULT_ENDPOINT));
    while (not endpoint.empty() and endpoint.back() == '/') {
        endpoint.pop_back();
    }
    const auto headers = string_map(options, "headers", where);
    const auto export_interval =
        std::chrono::milliseconds(options.value("export_interval_ms", DEFAULT_EXPORT_INTERVAL_MS));
    if (export_interval.count() <= 0) {
        throw RulesError(fmt::format("{}: export_interval_ms must be positive", where));
    }
    m_stale_after = std::chrono::seconds(options.value("stale_after_s", 300));
    m_states_as_metrics = options.value("states_as_metrics", false);
    m_flatten_event_fields = options.value("flatten_event_fields", true);

    otel::sdk::resource::ResourceAttributes resource_attributes = {{"service.name", "everest"}};
    for (const auto& [key, value] : string_map(options, "resource", where)) {
        resource_attributes.SetAttribute(key, value);
    }
    const auto resource = otel::sdk::resource::Resource::Create(resource_attributes);

    otel::sdk::metrics::PeriodicExportingMetricReaderOptions reader_options;
    reader_options.export_interval_millis = export_interval;
    reader_options.export_timeout_millis = std::min(export_interval / 2, std::chrono::milliseconds(10000));
    auto reader = otel::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
        exporters.metrics(endpoint + "/v1/metrics", headers), reader_options);
    m_meter_provider = otel::sdk::metrics::MeterProviderFactory::Create(
        std::make_unique<otel::sdk::metrics::ViewRegistry>(), resource);
    m_meter_provider->AddMetricReader(std::move(reader));
    m_meter = m_meter_provider->GetMeter("everest.telemetry_router");

    otel::sdk::logs::BatchLogRecordProcessorOptions processor_options;
    processor_options.schedule_delay_millis = std::min(export_interval, std::chrono::milliseconds(5000));
    auto processor = otel::sdk::logs::BatchLogRecordProcessorFactory::Create(
        exporters.logs(endpoint + "/v1/logs", headers), processor_options);
    m_logger_provider = otel::sdk::logs::LoggerProviderFactory::Create(std::move(processor), resource);
    m_logger = m_logger_provider->GetLogger("everest.telemetry_router", "everest.telemetry_router");
}

OtelSink::~OtelSink() {
    stop();
}

std::string_view OtelSink::type() const {
    return "otel";
}

OtelSink::Instrument& OtelSink::instrument_for(const std::string& metric_name, const ElementDeclaration& element,
                                               bool state_set) {
    const bool integer = state_set or element.value_type == ValueType::Integer;
    const auto existing = m_instruments.find(metric_name);
    if (existing != m_instruments.end()) {
        const auto& instrument = *existing->second;
        if (instrument.kind != element.kind or instrument.integer != integer or instrument.state_set != state_set) {
            throw BindError(
                fmt::format("metric '{}' is already used by an element of another kind or type", metric_name));
        }
        return *existing->second;
    }

    auto instrument = std::make_unique<Instrument>();
    instrument->name = metric_name;
    instrument->kind = element.kind;
    instrument->integer = integer;
    instrument->state_set = state_set;
    instrument->stale_after = m_stale_after;
    instrument->mutex = &m_mutex;
    const auto unit = element.unit.value_or(state_set ? std::string("1") : std::string());
    if (element.kind == Kind::Counter) {
        instrument->handle = integer ? m_meter->CreateInt64ObservableCounter(metric_name, element.description, unit)
                                     : m_meter->CreateDoubleObservableCounter(metric_name, element.description, unit);
    } else {
        instrument->handle = integer ? m_meter->CreateInt64ObservableGauge(metric_name, element.description, unit)
                                     : m_meter->CreateDoubleObservableGauge(metric_name, element.description, unit);
    }
    instrument->handle->AddCallback(&OtelSink::observe, instrument.get());
    auto& result = *instrument;
    m_instruments.emplace(metric_name, std::move(instrument));
    return result;
}

std::unique_ptr<SinkBinding> OtelSink::bind(const Target& target, const Producer& producer,
                                            const ElementDeclaration& element) {
    for (const auto& [key, value] : target.options.items()) {
        if (TARGET_OPTIONS.count(key) == 0) {
            throw BindError(fmt::format("unknown option '{}' for otel sink '{}'", key, name()));
        }
    }
    std::string metric_name =
        fmt::format("everest.{}.{}", producer.type.empty() ? producer.id : producer.type, element.name);
    if (target.options.contains("metric_name")) {
        if (not target.options.at("metric_name").is_string()) {
            throw BindError("metric_name must be a string");
        }
        try {
            metric_name = substitute(target.options.at("metric_name").get<std::string>(), producer, element);
        } catch (const RulesError& e) {
            throw BindError(fmt::format("metric_name: {}", e.what()));
        }
    }

    auto binding = std::make_unique<Binding>();
    binding->attributes = producer_attributes(producer, element);
    binding->series_key = producer.id;
    binding->event_name = metric_name;

    const bool state_metric = element.kind == Kind::State and m_states_as_metrics and
                              (element.value_type == ValueType::Boolean or not element.enum_values.empty());
    binding->log = element.kind == Kind::State or element.kind == Kind::Event;

    const std::lock_guard lock(m_mutex);
    if (element.kind == Kind::Gauge or element.kind == Kind::Counter or state_metric) {
        auto& instrument = instrument_for(metric_name, element, state_metric);
        binding->instrument = &instrument;
        if (state_metric and element.value_type == ValueType::String) {
            for (const auto& value : element.enum_values) {
                auto& series = instrument.series[producer.id + "\n" + value];
                series.attributes = binding->attributes;
                series.attributes["everest.state"] = value;
            }
        } else {
            instrument.series[producer.id].attributes = binding->attributes;
        }
    }
    return binding;
}

void OtelSink::submit(const Sample& sample, const SinkBinding& sink_binding) {
    const auto& binding = static_cast<const Binding&>(sink_binding);
    const auto now = std::chrono::steady_clock::now();
    bool state_changed = false;
    {
        const std::lock_guard lock(m_mutex);
        if (m_stopped) {
            return;
        }
        if (binding.log and sample.element.kind == Kind::State) {
            const auto text = sample.value.is_string() ? sample.value.get<std::string>() : sample.value.dump();
            state_changed = binding.last_state != text;
            binding.last_state = text;
        }
        if (binding.instrument != nullptr) {
            auto& instrument = *binding.instrument;
            if (instrument.state_set and sample.value.is_string()) {
                const auto current = binding.series_key + "\n" + sample.value.get<std::string>();
                for (auto& [key, series] : instrument.series) {
                    if (key.rfind(binding.series_key + "\n", 0) == 0) {
                        series.value = std::int64_t{key == current ? 1 : 0};
                        series.updated = now;
                    }
                }
            } else {
                auto& series = instrument.series[binding.series_key];
                if (sample.value.is_boolean()) {
                    series.value = std::int64_t{sample.value.get<bool>() ? 1 : 0};
                } else if (instrument.integer) {
                    series.value = sample.value.get<std::int64_t>();
                } else {
                    series.value = sample.value.get<double>();
                }
                series.updated = now;
            }
        }
    }
    if (binding.log and (sample.element.kind == Kind::Event or state_changed)) {
        emit_log(sample, binding);
    }
}

void OtelSink::emit_log(const Sample& sample, const Binding& binding) {
    auto record = m_logger->CreateLogRecord();
    if (not record) {
        return;
    }
    record->SetTimestamp(to_timestamp(sample.timestamp_ms));
    record->SetObservedTimestamp(std::chrono::system_clock::now());
    record->SetSeverity(otel::logs::Severity::kInfo);
    const auto body = sample.value.is_string() ? sample.value.get<std::string>() : sample.value.dump();
    record->SetBody(body);
    record->SetEventId(0, binding.event_name);
    record->SetAttribute("event.name", binding.event_name);
    for (const auto& [key, value] : binding.attributes) {
        record->SetAttribute(key, value);
    }
    if (m_flatten_event_fields and sample.element.kind == Kind::Event and sample.value.is_object()) {
        for (const auto& [key, value] : sample.value.items()) {
            const auto attribute = "everest.event." + key;
            if (value.is_string()) {
                record->SetAttribute(attribute, value.get<std::string>());
            } else if (value.is_boolean()) {
                record->SetAttribute(attribute, value.get<bool>());
            } else if (value.is_number_integer()) {
                record->SetAttribute(attribute, value.get<std::int64_t>());
            } else if (value.is_number()) {
                record->SetAttribute(attribute, value.get<double>());
            }
        }
    }
    m_logger->EmitLogRecord(std::move(record));
}

void OtelSink::observe(otel::metrics::ObserverResult result, void* state) {
    auto& instrument = *static_cast<Instrument*>(state);
    const auto now = std::chrono::steady_clock::now();
    const std::lock_guard lock(*instrument.mutex);
    for (const auto& [key, series] : instrument.series) {
        if (std::holds_alternative<std::monostate>(series.value) or now - series.updated > instrument.stale_after) {
            continue;
        }
        const otel::common::KeyValueIterableView<Attributes> attributes(series.attributes);
        if (const auto* integer = std::get_if<std::int64_t>(&series.value)) {
            if (auto observer =
                    otel::nostd::get_if<otel::nostd::shared_ptr<otel::metrics::ObserverResultT<std::int64_t>>>(
                        &result)) {
                (*observer)->Observe(*integer, attributes);
            }
        } else if (const auto* number = std::get_if<double>(&series.value)) {
            if (auto observer =
                    otel::nostd::get_if<otel::nostd::shared_ptr<otel::metrics::ObserverResultT<double>>>(&result)) {
                (*observer)->Observe(*number, attributes);
            }
        }
    }
}

void OtelSink::flush() {
    if (m_meter_provider) {
        m_meter_provider->ForceFlush();
    }
    if (m_logger_provider) {
        m_logger_provider->ForceFlush();
    }
}

void OtelSink::stop() {
    {
        const std::lock_guard lock(m_mutex);
        if (m_stopped) {
            return;
        }
        m_stopped = true;
    }
    flush();
    if (m_meter_provider) {
        m_meter_provider->Shutdown(std::chrono::seconds(2));
    }
    if (m_logger_provider) {
        m_logger_provider->Shutdown(std::chrono::seconds(2));
    }
}

} // namespace telemetry_router
