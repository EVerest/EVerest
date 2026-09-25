// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <opentelemetry/logs/logger.h>
#include <opentelemetry/metrics/async_instruments.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/sdk/logs/exporter.h>
#include <opentelemetry/sdk/logs/logger_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/push_metric_exporter.h>

#include "sink.hpp"

namespace telemetry_router {

/// Creates the exporters of an OtelSink; replaced in tests
struct OtelExporterFactory {
    std::function<std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter>(
        const std::string& url, const std::map<std::string, std::string>& headers)>
        metrics;
    std::function<std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter>(
        const std::string& url, const std::map<std::string, std::string>& headers)>
        logs;
};

/// \brief OTLP/HTTP exporters with protobuf encoding
OtelExporterFactory otlp_http_exporter_factory();

/// \brief Exports telemetry to an OpenTelemetry collector
///
/// Gauges and counters become observable gauges and cumulative counters, exported periodically with their
/// latest values. States become log records on every change, and additionally 0/1 gauges per value with
/// states_as_metrics. Events become log records whose body is the JSON value.
class OtelSink : public Sink {
public:
    OtelSink(std::string name, const nlohmann::json& options, OtelExporterFactory exporters);
    ~OtelSink() override;

    std::string_view type() const override;
    std::unique_ptr<SinkBinding> bind(const Target& target, const Producer& producer,
                                      const ElementDeclaration& element) override;
    void submit(const Sample& sample, const SinkBinding& binding) override;
    void stop() override;

    /// \brief Export collected metrics and queued log records now
    void flush();

private:
    struct Series;
    struct Instrument;
    struct Binding;

    static void observe(opentelemetry::metrics::ObserverResult result, void* state);
    Instrument& instrument_for(const std::string& metric_name, const ElementDeclaration& element, bool state_set);
    void emit_log(const Sample& sample, const Binding& binding);

    std::chrono::seconds m_stale_after{300};
    bool m_states_as_metrics{false};
    bool m_flatten_event_fields{true};

    std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> m_meter_provider;
    std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> m_logger_provider;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> m_meter;
    opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> m_logger;

    std::mutex m_mutex;
    std::map<std::string, std::unique_ptr<Instrument>> m_instruments;
    bool m_stopped{false};
};

} // namespace telemetry_router
