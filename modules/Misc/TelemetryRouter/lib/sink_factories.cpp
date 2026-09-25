// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "log_sink.hpp"
#include "ocpp_sink.hpp"
#include "sink.hpp"

namespace telemetry_router {

std::map<std::string, SinkFactory, std::less<>> default_sink_factories() {
    std::map<std::string, SinkFactory, std::less<>> factories;
    factories["log"] = [](const SinkConfig& config, const SinkEnvironment&) {
        return std::make_unique<LogSink>(config.name, config.options);
    };
    factories["ocpp"] = [](const SinkConfig& config, const SinkEnvironment& environment) {
        OcppSinkOptions defaults;
        defaults.flush_interval = std::chrono::milliseconds(environment.ocpp_flush_interval_ms);
        defaults.min_interval = std::chrono::seconds(environment.ocpp_min_interval_s);
        return std::make_unique<OcppSink>(config.name, config.options, environment.ocpp_client, defaults);
    };
    return factories;
}

} // namespace telemetry_router
