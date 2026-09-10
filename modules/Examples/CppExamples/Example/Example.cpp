// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "Example.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/provider.h>
#include <opentelemetry/sdk/metrics/view/view_registry.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/batch_span_processor_options.h>
#include <opentelemetry/sdk/trace/provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/semconv/service_attributes.h>

namespace {
bool is_log_interval_in_range(const int& value) {
    return value >= 1 and value <= 60;
}

bool is_valid_enum_test(const std::string& value) {
    if (value == "one" or value == "two" or value == "three") {
        return true;
    }
    return false;
}
} // namespace

namespace module {

void Example::init() {
    // the on_xxx_changed handlers are registered before init() is called and
    // run on a different thread, so every access to config/rw_config has to
    // hold config_mutex
    {
        std::scoped_lock lock(config_mutex);
        original_config = rw_config;
    }

    // before the implementations are initialized, so that they find the providers
    init_opentelemetry();

    invoke_init(*p_example);
    invoke_init(*p_store);
}

void Example::ready() {
    invoke_ready(*p_example);
    invoke_ready(*p_store);

    mqtt.publish("external/topic", "data");
}

void Example::shutdown() {
    invoke_shutdown(*p_example);
    invoke_shutdown(*p_store);

    shutdown_opentelemetry();
}

// Installs the OpenTelemetry SDK for this module process. Endpoints, intervals
// and timeouts follow the standard OTEL_* environment variables (see
// docs/index.rst), so the only EVerest specific part is the resource: every
// exported signal carries the module type and the module id, which is what
// lets a backend tell the modules of one EVerest instance apart.
//
// In a real integration this would live in the framework (once per module
// process, where the module is loaded) instead of in every module.
void Example::init_opentelemetry() {
    namespace otlp = opentelemetry::exporter::otlp;
    namespace sdk = opentelemetry::sdk;
    namespace semconv = opentelemetry::semconv;

    const auto resource = sdk::resource::Resource::Create({
        {semconv::service::kServiceNamespace, "everest"},
        {semconv::service::kServiceName, info.name},
        {semconv::service::kServiceInstanceId, info.id},
    });

    tracer_provider = sdk::trace::TracerProviderFactory::Create(
        sdk::trace::BatchSpanProcessorFactory::Create(otlp::OtlpHttpExporterFactory::Create(),
                                                      sdk::trace::BatchSpanProcessorOptions{}),
        resource);
    sdk::trace::Provider::SetTracerProvider(tracer_provider);

    // interval and timeout come from OTEL_METRIC_EXPORT_INTERVAL/_TIMEOUT; the
    // SDK silently falls back to 60s if the timeout is not below the interval
    sdk::metrics::PeriodicExportingMetricReaderOptions reader_options;
    reader_options.export_timeout_millis =
        std::min(reader_options.export_timeout_millis, reader_options.export_interval_millis / 2);

    meter_provider =
        sdk::metrics::MeterProviderFactory::Create(std::make_unique<sdk::metrics::ViewRegistry>(), resource);
    meter_provider->AddMetricReader(sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
        otlp::OtlpHttpMetricExporterFactory::Create(), reader_options));
    sdk::metrics::Provider::SetMeterProvider(meter_provider);
}

// Flushes what the exporters still have queued and stops their worker threads.
// Bounded so that an unreachable collector cannot stall the EVerest shutdown.
void Example::shutdown_opentelemetry() {
    constexpr auto timeout = std::chrono::seconds(2);
    if (tracer_provider) {
        tracer_provider->Shutdown(timeout);
    }
    if (meter_provider) {
        meter_provider->Shutdown(timeout);
    }
}

Everest::config::ConfigChangeResult Example::on_log_interval_changed(const int& new_interval) {
    std::scoped_lock lock(config_mutex);
    const std::string log_prefix = "Cfg Update for 'log_interval' | ";
    std::string desc = "rejected, as out-of-range";
    Everest::config::ConfigChangeResult ret =
        Everest::config::ConfigChangeResult::Rejected("New value is out of range.");
    int old_value = rw_config.log_interval;

    if (is_log_interval_in_range(new_interval)) {
        if (new_interval < rw_config.log_interval) {
            rw_config.log_interval = new_interval;
            desc = "accepted";
            ret = Everest::config::ConfigChangeResult::Accepted();
        } else if (new_interval > rw_config.log_interval) {
            desc = "accepted for next reboot";
            ret = Everest::config::ConfigChangeResult::AcceptedRebootRequired();
        }
    }

    EVLOG_info << log_prefix << "old == '" << std::to_string(old_value) << "', new == '" << std::to_string(new_interval)
               << "' " << desc;
    return ret;
}

Everest::config::ConfigChangeResult Example::on_enum_test_changed(const std::string& new_value) {
    std::scoped_lock lock(config_mutex);
    const std::string log_prefix = "Cfg Update for 'enum_test' | ";
    std::string desc = "rejected, as invalid enum value";
    Everest::config::ConfigChangeResult ret =
        Everest::config::ConfigChangeResult::Rejected("New value is invalid enum value.");
    const std::string old_value = rw_config.enum_test;

    if (is_valid_enum_test(new_value)) {
        rw_config.enum_test = new_value;
        ret = Everest::config::ConfigChangeResult::Accepted();
        desc = "accepted";
    }

    EVLOG_info << log_prefix << "old == '" << old_value << "', new == '" << new_value << "' " << desc;
    return ret;
}

} // namespace module
