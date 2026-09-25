// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <utils/telemetry/module_telemetry.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

#include <everest/logging.hpp>
#include <fmt/format.h>

#include <utils/telemetry/wire.hpp>

namespace everest::telemetry {

class Element {
public:
    Element(std::string name, Kind kind, ValueType value_type) :
        name(std::move(name)), kind(kind), value_type(value_type) {
    }

    const std::string name;
    const Kind kind;
    const ValueType value_type;

    std::mutex counter_mutex;
    std::int64_t integer_total{0};
    double number_total{0.0};

    std::atomic<bool> warned_invalid_value{false};
    std::atomic<bool> warned_negative_delta{false};
};

namespace {

constexpr auto DROP_WARNING_INTERVAL = std::chrono::seconds(60);

std::int64_t to_unix_ms(std::chrono::system_clock::time_point time_point) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch()).count();
}

bool is_invalid_number(const nlohmann::json& value) {
    return value.is_number_float() and not std::isfinite(value.get<double>());
}

} // namespace

struct ModuleTelemetry::Impl {
    std::string module_id;
    std::map<std::string, std::unique_ptr<Element>, std::less<>> elements;
    std::unique_ptr<DatagramSender> sender;

    std::atomic<std::uint64_t> sent{0};
    std::atomic<std::uint64_t> dropped_would_block{0};
    std::atomic<std::uint64_t> dropped_no_receiver{0};
    std::atomic<std::uint64_t> dropped_too_big{0};
    std::atomic<std::uint64_t> dropped_error{0};
    std::atomic<std::uint64_t> dropped_invalid_value{0};

    std::mutex warning_mutex;
    std::optional<std::chrono::steady_clock::time_point> last_drop_warning;
    std::uint64_t drops_at_last_warning{0};

    std::uint64_t total_dropped() const {
        return dropped_would_block + dropped_no_receiver + dropped_too_big + dropped_error + dropped_invalid_value;
    }

    void account(SendResult result, const Element& element) {
        switch (result) {
        case SendResult::Ok:
            ++sent;
            return;
        case SendResult::WouldBlock:
            ++dropped_would_block;
            break;
        case SendResult::NoReceiver:
            ++dropped_no_receiver;
            break;
        case SendResult::TooBig:
            ++dropped_too_big;
            break;
        case SendResult::Error:
            ++dropped_error;
            break;
        }
        warn_about_drop(result, element);
    }

    void warn_about_drop(SendResult result, const Element& element) {
        std::unique_lock lock(warning_mutex, std::try_to_lock);
        if (not lock.owns_lock()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (last_drop_warning.has_value() and now - last_drop_warning.value() < DROP_WARNING_INTERVAL) {
            return;
        }
        const auto dropped = total_dropped();
        EVLOG_warning << fmt::format("Telemetry of module '{}' is being dropped ({} for element '{}'); {} samples "
                                     "dropped since the last warning",
                                     module_id, to_string(result), element.name, dropped - drops_at_last_warning);
        last_drop_warning = now;
        drops_at_last_warning = dropped;
    }

    void send(Element& element, nlohmann::json value, const PublishOptions& options) {
        const auto timestamp = options.timestamp.value_or(std::chrono::system_clock::now());
        const auto datagram =
            wire::encode(wire::Sample{module_id, element.name, to_unix_ms(timestamp), std::move(value)});
        if (datagram.size() > wire::MAX_DATAGRAM_SIZE) {
            account(SendResult::TooBig, element);
            return;
        }
        account(sender->send(datagram.data(), datagram.size()), element);
    }
};

ModuleTelemetry::ModuleTelemetry() = default;
ModuleTelemetry::ModuleTelemetry(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}
ModuleTelemetry::~ModuleTelemetry() = default;
ModuleTelemetry::ModuleTelemetry(ModuleTelemetry&& other) noexcept = default;
ModuleTelemetry& ModuleTelemetry::operator=(ModuleTelemetry&& other) noexcept = default;

bool ModuleTelemetry::enabled() const {
    return m_impl != nullptr;
}

Element* ModuleTelemetry::bind(std::string_view name, Kind kind, ValueType value_type) {
    if (not m_impl) {
        return nullptr;
    }
    const auto it = m_impl->elements.find(name);
    if (it == m_impl->elements.end()) {
        EVLOG_error << fmt::format(
            "Telemetry element '{}' of module '{}' is not declared in the installed manifest; it stays inactive", name,
            m_impl->module_id);
        return nullptr;
    }
    auto& element = *it->second;
    if (element.kind != kind or element.value_type != value_type) {
        EVLOG_error << fmt::format("Telemetry element '{}' of module '{}' is compiled as {} of type {}, but the "
                                   "installed manifest declares {} of type {}; it stays inactive",
                                   name, m_impl->module_id, to_string(kind), to_string(value_type),
                                   to_string(element.kind), to_string(element.value_type));
        return nullptr;
    }
    return &element;
}

void ModuleTelemetry::publish(Element* element, nlohmann::json&& value, const PublishOptions& options) {
    if (not m_impl or element == nullptr) {
        return;
    }
    if (is_invalid_number(value)) {
        ++m_impl->dropped_invalid_value;
        if (not element->warned_invalid_value.exchange(true)) {
            EVLOG_warning << fmt::format("Dropping non-finite value of telemetry element '{}' of module '{}'",
                                         element->name, m_impl->module_id);
        }
        return;
    }
    m_impl->send(*element, std::move(value), options);
}

void ModuleTelemetry::counter_add(Element* element, std::int64_t delta, const PublishOptions& options) {
    if (not m_impl or element == nullptr) {
        return;
    }
    if (delta < 0) {
        if (not element->warned_negative_delta.exchange(true)) {
            EVLOG_warning << fmt::format("Ignoring negative increase of counter '{}' of module '{}'", element->name,
                                         m_impl->module_id);
        }
        return;
    }
    std::int64_t total = 0;
    {
        const std::lock_guard lock(element->counter_mutex);
        element->integer_total += delta;
        total = element->integer_total;
    }
    m_impl->send(*element, total, options);
}

void ModuleTelemetry::counter_add(Element* element, double delta, const PublishOptions& options) {
    if (not m_impl or element == nullptr) {
        return;
    }
    if (not std::isfinite(delta) or delta < 0.0) {
        if (not element->warned_negative_delta.exchange(true)) {
            EVLOG_warning << fmt::format("Ignoring negative or non-finite increase of counter '{}' of module '{}'",
                                         element->name, m_impl->module_id);
        }
        return;
    }
    double total = 0.0;
    {
        const std::lock_guard lock(element->counter_mutex);
        element->number_total += delta;
        total = element->number_total;
    }
    m_impl->send(*element, total, options);
}

SendStatistics ModuleTelemetry::statistics() const {
    SendStatistics statistics;
    if (not m_impl) {
        return statistics;
    }
    statistics.sent = m_impl->sent;
    statistics.dropped_would_block = m_impl->dropped_would_block;
    statistics.dropped_no_receiver = m_impl->dropped_no_receiver;
    statistics.dropped_too_big = m_impl->dropped_too_big;
    statistics.dropped_error = m_impl->dropped_error;
    statistics.dropped_invalid_value = m_impl->dropped_invalid_value;
    return statistics;
}

ModuleTelemetry make_module_telemetry(std::string module_id, const ElementDeclarations& elements,
                                      std::unique_ptr<DatagramSender> sender) {
    auto impl = std::make_unique<ModuleTelemetry::Impl>();
    impl->module_id = std::move(module_id);
    impl->sender = std::move(sender);
    for (const auto& [name, declaration] : elements) {
        impl->elements.emplace(name, std::make_unique<Element>(name, declaration.kind, declaration.value_type));
    }
    return ModuleTelemetry(std::move(impl));
}

ModuleTelemetry make_module_telemetry(const std::string& module_id, const ElementDeclarations& elements, bool enabled,
                                      const std::string& socket_path) {
    if (not enabled or elements.empty()) {
        return {};
    }
    try {
        return make_module_telemetry(module_id, elements, make_uds_datagram_sender(socket_path));
    } catch (const std::exception& e) {
        EVLOG_error << fmt::format("Telemetry of module '{}' is disabled: {}", module_id, e.what());
        return {};
    }
}

std::string_view to_string(Kind kind) {
    switch (kind) {
    case Kind::Gauge:
        return "gauge";
    case Kind::Counter:
        return "counter";
    case Kind::State:
        return "state";
    case Kind::Event:
        return "event";
    }
    return "unknown";
}

std::string_view to_string(ValueType value_type) {
    switch (value_type) {
    case ValueType::Number:
        return "number";
    case ValueType::Integer:
        return "integer";
    case ValueType::Boolean:
        return "boolean";
    case ValueType::String:
        return "string";
    case ValueType::Object:
        return "object";
    }
    return "unknown";
}

std::optional<Kind> kind_from_string(std::string_view kind) {
    for (const auto candidate : {Kind::Gauge, Kind::Counter, Kind::State, Kind::Event}) {
        if (to_string(candidate) == kind) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<ValueType> value_type_from_string(std::string_view value_type) {
    for (const auto candidate :
         {ValueType::Number, ValueType::Integer, ValueType::Boolean, ValueType::String, ValueType::Object}) {
        if (to_string(candidate) == value_type) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace everest::telemetry
