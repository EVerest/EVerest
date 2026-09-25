// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "catalog.hpp"
#include "rules.hpp"

namespace telemetry_router {

struct Sample {
    const Producer& producer;
    const ElementDeclaration& element;
    std::int64_t timestamp_ms;
    const nlohmann::json& value;
};

/// Sink-specific, precomputed mapping of one element to one target
class SinkBinding {
public:
    virtual ~SinkBinding() = default;
};

class BindError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Sink {
public:
    explicit Sink(std::string name) : m_name(std::move(name)) {
    }
    virtual ~Sink() = default;

    const std::string& name() const {
        return m_name;
    }

    virtual std::string_view type() const = 0;

    /// \brief Map an element to this sink according to the options of a rule target
    /// \throws BindError if the element cannot be represented or the options are invalid
    virtual std::unique_ptr<SinkBinding> bind(const Target& target, const Producer& producer,
                                              const ElementDeclaration& element) = 0;

    /// \brief Hand over a sample; called on the receiver thread, must not block
    virtual void submit(const Sample& sample, const SinkBinding& binding) = 0;

    /// \brief Forget a binding, e.g. when a producer declares its elements again
    virtual void release(const SinkBinding& binding) {
        (void)binding;
    }

    /// \brief Start delivering; called when all modules are ready
    virtual void enable() {
    }

    virtual void stop() {
    }

private:
    std::string m_name;
};

class IOcppClient;

/// Defaults and services available to sinks
struct SinkEnvironment {
    IOcppClient* ocpp_client{nullptr};
    std::int64_t ocpp_flush_interval_ms{1000};
    std::int64_t ocpp_min_interval_s{10};
};

using SinkFactory = std::function<std::unique_ptr<Sink>(const SinkConfig& config, const SinkEnvironment& environment)>;

/// \brief Sink types available in this build, by type name
std::map<std::string, SinkFactory, std::less<>> default_sink_factories();

} // namespace telemetry_router
