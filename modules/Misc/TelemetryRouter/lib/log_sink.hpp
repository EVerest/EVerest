// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <cstdint>

#include "sink.hpp"

namespace telemetry_router {

/// \brief Logs samples, limited to max_per_second lines; for debugging rules and producers
class LogSink : public Sink {
public:
    LogSink(std::string name, const nlohmann::json& options);

    std::string_view type() const override;
    std::unique_ptr<SinkBinding> bind(const Target& target, const Producer& producer,
                                      const ElementDeclaration& element) override;
    void submit(const Sample& sample, const SinkBinding& binding) override;

private:
    std::int64_t m_max_per_second;
    std::chrono::steady_clock::time_point m_window_start;
    std::int64_t m_lines_in_window{0};
    std::int64_t m_suppressed{0};
};

} // namespace telemetry_router
