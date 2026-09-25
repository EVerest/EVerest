// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

/// Telemetry handles used by generated module code.
///
/// ev-cli generates one handle per telemetry element declared in a module manifest. A handle bound to an
/// inactive telemetry context (telemetry disabled, or the element does not match the installed manifest) does
/// nothing and never serializes its value.
namespace everest::telemetry {

enum class Kind : std::uint8_t {
    Gauge,
    Counter,
    State,
    Event,
};

enum class ValueType : std::uint8_t {
    Number,
    Integer,
    Boolean,
    String,
    Object,
};

struct PublishOptions {
    /// Time of the measurement; the time of the call when empty
    std::optional<std::chrono::system_clock::time_point> timestamp;
};

/// Opaque per-element state owned by ModuleTelemetry
class Element;

struct SendStatistics {
    std::uint64_t sent{0};
    std::uint64_t dropped_would_block{0};
    std::uint64_t dropped_no_receiver{0};
    std::uint64_t dropped_too_big{0};
    std::uint64_t dropped_error{0};
    std::uint64_t dropped_invalid_value{0};

    std::uint64_t dropped() const {
        return dropped_would_block + dropped_no_receiver + dropped_too_big + dropped_error + dropped_invalid_value;
    }
};

/// \brief Type-agnostic telemetry context of one module process
///
/// A default constructed context is disabled: every element binds as inactive. Enabled contexts are created by
/// the framework (utils/telemetry/module_telemetry.hpp).
class ModuleTelemetry {
public:
    ModuleTelemetry();
    ~ModuleTelemetry();
    ModuleTelemetry(ModuleTelemetry&& other) noexcept;
    ModuleTelemetry& operator=(ModuleTelemetry&& other) noexcept;
    ModuleTelemetry(const ModuleTelemetry&) = delete;
    ModuleTelemetry& operator=(const ModuleTelemetry&) = delete;

    bool enabled() const;
    SendStatistics statistics() const;

    /// \returns the element, or nullptr if disabled, unknown, or its kind or value type does not match the manifest
    Element* bind(std::string_view name, Kind kind, ValueType value_type);
    void publish(Element* element, nlohmann::json&& value, const PublishOptions& options);
    void counter_add(Element* element, std::int64_t delta, const PublishOptions& options);
    void counter_add(Element* element, double delta, const PublishOptions& options);

    struct Impl;
    explicit ModuleTelemetry(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> m_impl;
};

namespace detail {

template <typename T> constexpr ValueType numeric_value_type() {
    static_assert(std::is_same_v<T, double> or std::is_same_v<T, std::int64_t>,
                  "numeric telemetry elements are double (type: number) or std::int64_t (type: integer)");
    return std::is_same_v<T, double> ? ValueType::Number : ValueType::Integer;
}

template <typename T> constexpr ValueType state_value_type() {
    return std::is_same_v<T, bool> ? ValueType::Boolean : ValueType::String;
}

class HandleBase {
protected:
    HandleBase(ModuleTelemetry& telemetry, std::string_view name, Kind kind, ValueType value_type) :
        m_telemetry(telemetry), m_element(telemetry.bind(name, kind, value_type)) {
    }

    bool active() const {
        return m_element != nullptr;
    }

    ModuleTelemetry& m_telemetry;
    Element* m_element;
};

} // namespace detail

/// \brief Current numeric value, e.g. a temperature
template <typename T> class Gauge : detail::HandleBase {
public:
    Gauge(ModuleTelemetry& telemetry, std::string_view name) :
        HandleBase(telemetry, name, Kind::Gauge, detail::numeric_value_type<T>()) {
    }

    void set(T value, const PublishOptions& options = {}) {
        if (active()) {
            m_telemetry.publish(m_element, nlohmann::json(value), options);
        }
    }
};

/// \brief Monotonic count; the framework keeps the running total and publishes it
template <typename T> class Counter : detail::HandleBase {
public:
    Counter(ModuleTelemetry& telemetry, std::string_view name) :
        HandleBase(telemetry, name, Kind::Counter, detail::numeric_value_type<T>()) {
    }

    void increase(T delta = 1, const PublishOptions& options = {}) {
        if (active()) {
            m_telemetry.counter_add(m_element, delta, options);
        }
    }
};

/// \brief Current discrete value: bool, std::string or a string enum
template <typename T> class State : detail::HandleBase {
public:
    State(ModuleTelemetry& telemetry, std::string_view name) :
        HandleBase(telemetry, name, Kind::State, detail::state_value_type<T>()) {
    }

    void set(const T& value, const PublishOptions& options = {}) {
        if (active()) {
            m_telemetry.publish(m_element, nlohmann::json(value), options);
        }
    }
};

/// \brief Structured occurrence, e.g. a finished session
template <typename T> class Event : detail::HandleBase {
public:
    Event(ModuleTelemetry& telemetry, std::string_view name) :
        HandleBase(telemetry, name, Kind::Event, ValueType::Object) {
    }

    void publish(const T& value, const PublishOptions& options = {}) {
        if (active()) {
            m_telemetry.publish(m_element, nlohmann::json(value), options);
        }
    }
};

std::string_view to_string(Kind kind);
std::string_view to_string(ValueType value_type);
std::optional<Kind> kind_from_string(std::string_view kind);
std::optional<ValueType> value_type_from_string(std::string_view value_type);

} // namespace everest::telemetry
