// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "log_sink.hpp"

#include <everest/logging.hpp>
#include <fmt/format.h>

namespace telemetry_router {

LogSink::LogSink(std::string name, const nlohmann::json& options) :
    Sink(std::move(name)), m_max_per_second(options.value("max_per_second", 20)) {
    for (const auto& [key, value] : options.items()) {
        if (key != "max_per_second") {
            throw RulesError(fmt::format("sinks.{}: unknown option '{}' for a log sink", this->name(), key));
        }
    }
}

std::string_view LogSink::type() const {
    return "log";
}

std::unique_ptr<SinkBinding> LogSink::bind(const Target& target, const Producer&, const ElementDeclaration&) {
    if (not target.options.empty()) {
        throw BindError(fmt::format("log sink '{}' takes no target options", name()));
    }
    return std::make_unique<SinkBinding>();
}

void LogSink::submit(const Sample& sample, const SinkBinding&) {
    const auto now = std::chrono::steady_clock::now();
    if (now - m_window_start >= std::chrono::seconds(1)) {
        if (m_suppressed > 0) {
            EVLOG_info << fmt::format("[{}] {} telemetry samples not logged", name(), m_suppressed);
        }
        m_window_start = now;
        m_lines_in_window = 0;
        m_suppressed = 0;
    }
    if (m_lines_in_window >= m_max_per_second) {
        ++m_suppressed;
        return;
    }
    ++m_lines_in_window;
    EVLOG_info << fmt::format("[{}] {}.{} = {}", name(), sample.producer.id, sample.element.name, sample.value.dump());
}

} // namespace telemetry_router
