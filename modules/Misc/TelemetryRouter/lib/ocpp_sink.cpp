// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "ocpp_sink.hpp"

#include <algorithm>
#include <cmath>

#include <everest/logging.hpp>
#include <fmt/format.h>

namespace telemetry_router {

namespace {

constexpr std::size_t OCPP_MAX_NAME_LENGTH = 50;

const std::set<std::string> SINK_OPTIONS = {"flush_interval_ms", "min_interval_s", "max_batch", "dead_retry_s"};
const std::set<std::string> TARGET_OPTIONS = {"component", "variable", "min_interval_s",
                                              "deadband",  "decimals", "scale"};

std::string address_label(const OcppWrite& address) {
    auto label = address.component_name;
    if (address.component_instance.has_value()) {
        label += "[" + address.component_instance.value() + "]";
    }
    if (address.evse.has_value()) {
        label += fmt::format(" EVSE {}", address.evse.value());
        if (address.connector.has_value()) {
            label += fmt::format(" connector {}", address.connector.value());
        }
    }
    label += " / " + address.variable_name;
    if (address.variable_instance.has_value()) {
        label += "[" + address.variable_instance.value() + "]";
    }
    return label;
}

std::string checked_name(const nlohmann::json& object, const char* key, const Producer& producer,
                         const ElementDeclaration& element, const std::string& where) {
    const auto it = object.find(key);
    if (it == object.end() or not it->is_string()) {
        throw BindError(fmt::format("{}.{} must be a string", where, key));
    }
    std::string value;
    try {
        value = substitute(it->get<std::string>(), producer, element);
    } catch (const RulesError& e) {
        throw BindError(fmt::format("{}.{}: {}", where, key, e.what()));
    }
    if (value.empty() or value.size() > OCPP_MAX_NAME_LENGTH) {
        throw BindError(fmt::format("{}.{} resolves to '{}', which must have 1 to {} characters", where, key, value,
                                    OCPP_MAX_NAME_LENGTH));
    }
    return value;
}

std::optional<std::string> optional_name(const nlohmann::json& object, const char* key, const Producer& producer,
                                         const ElementDeclaration& element, const std::string& where) {
    if (not object.contains(key)) {
        return std::nullopt;
    }
    return checked_name(object, key, producer, element, where);
}

// "auto" takes the id from the producer's mapping, "none" omits it, a number sets it
std::optional<int> resolve_id(const nlohmann::json& object, const char* key, std::optional<int> from_mapping,
                              const std::string& where) {
    const auto value = object.value(key, nlohmann::json("auto"));
    if (value == "auto") {
        return (from_mapping.has_value() and from_mapping.value() > 0) ? from_mapping : std::nullopt;
    }
    if (value == "none") {
        return std::nullopt;
    }
    if (value.is_number_integer() and value.get<int>() > 0) {
        return value.get<int>();
    }
    throw BindError(fmt::format("{}.{} must be 'auto', 'none' or a positive integer", where, key));
}

std::chrono::seconds seconds_option(const nlohmann::json& options, const char* key, std::chrono::seconds fallback) {
    const auto it = options.find(key);
    if (it == options.end()) {
        return fallback;
    }
    if (not it->is_number_integer() or it->get<std::int64_t>() < 0) {
        throw BindError(fmt::format("{} must be a non-negative integer", key));
    }
    return std::chrono::seconds(it->get<std::int64_t>());
}

} // namespace

struct OcppSink::Entry {
    OcppWrite address;
    std::string label;
    std::string source;
    std::chrono::seconds min_interval{0};
    double deadband{0.0};

    std::optional<std::string> pending;
    std::optional<double> pending_number;
    std::uint64_t pending_generation{0};
    std::optional<std::string> last_sent;
    std::optional<double> last_sent_number;
    std::optional<std::chrono::steady_clock::time_point> last_sent_at;
    std::optional<std::chrono::steady_clock::time_point> dead_until;

    bool released{false};
    bool reported_dead{false};
    bool reported_rejected{false};
    bool reported_invalid{false};
    bool reported_reboot{false};
};

struct OcppSink::Binding : SinkBinding {
    std::size_t entry{0};
    NumberFormat format;
};

OcppSink::OcppSink(std::string name, const nlohmann::json& options, IOcppClient* client, OcppSinkOptions defaults) :
    Sink(std::move(name)), m_client(client), m_options(std::move(defaults)) {
    for (const auto& [key, value] : options.items()) {
        if (SINK_OPTIONS.count(key) == 0) {
            throw RulesError(fmt::format("sinks.{}: unknown option '{}' for an ocpp sink", this->name(), key));
        }
        if (not value.is_number_integer() or value.get<std::int64_t>() < 0) {
            throw RulesError(fmt::format("sinks.{}: {} must be a non-negative integer", this->name(), key));
        }
    }
    if (options.contains("flush_interval_ms")) {
        m_options.flush_interval = std::chrono::milliseconds(options.at("flush_interval_ms").get<std::int64_t>());
    }
    if (options.contains("min_interval_s")) {
        m_options.min_interval = std::chrono::seconds(options.at("min_interval_s").get<std::int64_t>());
    }
    if (options.contains("max_batch")) {
        m_options.max_batch = std::max<std::size_t>(1, options.at("max_batch").get<std::size_t>());
    }
    if (options.contains("dead_retry_s")) {
        m_options.dead_retry = std::chrono::seconds(options.at("dead_retry_s").get<std::int64_t>());
    }
    if (m_options.flush_interval.count() <= 0) {
        throw RulesError(fmt::format("sinks.{}: flush_interval_ms must be positive", this->name()));
    }
}

OcppSink::~OcppSink() {
    stop();
}

std::string_view OcppSink::type() const {
    return "ocpp";
}

std::unique_ptr<SinkBinding> OcppSink::bind(const Target& target, const Producer& producer,
                                            const ElementDeclaration& element) {
    if (element.kind == Kind::Event) {
        throw BindError(fmt::format("OCPP sink '{}' accepts only gauge, counter and state elements, '{}' is an event",
                                    name(), element.name));
    }
    const auto& options = target.options;
    for (const auto& [key, value] : options.items()) {
        if (TARGET_OPTIONS.count(key) == 0) {
            throw BindError(fmt::format("unknown option '{}' for OCPP sink '{}'", key, name()));
        }
    }
    const auto component = options.value("component", nlohmann::json::object());
    const auto variable = options.value("variable", nlohmann::json::object());
    if (not component.is_object() or not variable.is_object()) {
        throw BindError(fmt::format("OCPP sink '{}' needs component and variable objects", name()));
    }

    Entry entry;
    entry.address.component_name = checked_name(component, "name", producer, element, "component");
    entry.address.component_instance = optional_name(component, "instance", producer, element, "component");
    entry.address.evse = resolve_id(component, "evse", producer.evse, "component");
    entry.address.connector = entry.address.evse.has_value()
                                  ? resolve_id(component, "connector", producer.connector, "component")
                                  : std::nullopt;
    entry.address.variable_name = checked_name(variable, "name", producer, element, "variable");
    entry.address.variable_instance = optional_name(variable, "instance", producer, element, "variable");
    entry.label = address_label(entry.address);
    entry.source = producer.id + "." + element.name;
    entry.min_interval = seconds_option(options, "min_interval_s", m_options.min_interval);

    auto binding = std::make_unique<Binding>();
    if (options.contains("deadband")) {
        if (not options.at("deadband").is_number() or options.at("deadband").get<double>() < 0.0) {
            throw BindError("deadband must be a non-negative number");
        }
        entry.deadband = options.at("deadband").get<double>();
    }
    if (options.contains("decimals")) {
        if (not options.at("decimals").is_number_integer() or options.at("decimals").get<int>() < 0) {
            throw BindError("decimals must be a non-negative integer");
        }
        binding->format.decimals = options.at("decimals").get<int>();
    }
    if (options.contains("scale")) {
        if (not options.at("scale").is_number()) {
            throw BindError("scale must be a number");
        }
        binding->format.scale = options.at("scale").get<double>();
    }
    // deadband, decimals and scale only affect numeric elements, so one rule can cover elements of all kinds
    if (element.value_type != ValueType::Number and element.value_type != ValueType::Integer) {
        entry.deadband = 0.0;
        binding->format = NumberFormat{};
    }

    const std::lock_guard lock(m_mutex);
    const auto [existing, inserted] = m_entry_by_address.emplace(entry.label, m_entries.size());
    if (not inserted) {
        throw BindError(fmt::format("OCPP variable {} is already written by {}", entry.label,
                                    m_entries.at(existing->second).source));
    }
    binding->entry = m_entries.size();
    m_entries.push_back(std::move(entry));
    return binding;
}

void OcppSink::submit(const Sample& sample, const SinkBinding& sink_binding) {
    const auto& binding = static_cast<const Binding&>(sink_binding);
    const auto text = format_ocpp_value(sample.element, sample.value, binding.format);

    const std::lock_guard lock(m_mutex);
    auto& entry = m_entries.at(binding.entry);
    if (entry.released) {
        return;
    }
    if (not text.has_value()) {
        if (not entry.reported_invalid) {
            entry.reported_invalid = true;
            EVLOG_warning << fmt::format("OCPP sink '{}': value {} of {} cannot be written to {}", name(),
                                         sample.value.dump(), entry.source, entry.label);
        }
        return;
    }

    std::optional<double> number;
    if (sample.value.is_number()) {
        number = sample.value.get<double>() * binding.format.scale;
    }
    const bool unchanged = entry.last_sent == text or
                           (entry.deadband > 0.0 and number.has_value() and entry.last_sent_number.has_value() and
                            std::fabs(number.value() - entry.last_sent_number.value()) <= entry.deadband);
    if (unchanged) {
        entry.pending.reset();
        return;
    }
    entry.pending = text;
    entry.pending_number = number;
    ++entry.pending_generation;
}

void OcppSink::release(const SinkBinding& sink_binding) {
    const auto& binding = static_cast<const Binding&>(sink_binding);
    const std::lock_guard lock(m_mutex);
    auto& entry = m_entries.at(binding.entry);
    m_entry_by_address.erase(entry.label);
    entry.released = true;
    entry.pending.reset();
}

void OcppSink::enable() {
    {
        const std::lock_guard lock(m_mutex);
        m_enabled = true;
        m_stop = false;
    }
    if (m_options.run_flush_thread and not m_thread.joinable()) {
        m_thread = std::thread([this] { run(); });
    }
}

void OcppSink::stop() {
    {
        const std::lock_guard lock(m_mutex);
        m_stop = true;
        m_enabled = false;
    }
    m_stop_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void OcppSink::run() {
    std::unique_lock lock(m_mutex);
    while (not m_stop) {
        m_stop_cv.wait_for(lock, m_options.flush_interval, [this] { return m_stop; });
        if (m_stop) {
            break;
        }
        lock.unlock();
        flush();
        lock.lock();
    }
}

void OcppSink::flush() {
    const auto now = m_options.clock();
    auto due = collect_due(now);
    for (std::size_t offset = 0; offset < due.size(); offset += m_options.max_batch) {
        const auto end = std::min(due.size(), offset + m_options.max_batch);
        write_batch(std::vector<std::size_t>(due.begin() + offset, due.begin() + end), now);
        const std::lock_guard lock(m_mutex);
        if (m_backoff_until.has_value()) {
            break;
        }
    }
}

std::vector<std::size_t> OcppSink::collect_due(std::chrono::steady_clock::time_point now) {
    const std::lock_guard lock(m_mutex);
    std::vector<std::size_t> due;
    if (not m_enabled) {
        return due;
    }
    if (m_backoff_until.has_value()) {
        if (now < m_backoff_until.value()) {
            return due;
        }
        m_backoff_until.reset();
    }
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const auto& entry = m_entries[i];
        if (not entry.pending.has_value()) {
            continue;
        }
        if (entry.dead_until.has_value() and now < entry.dead_until.value()) {
            continue;
        }
        if (entry.last_sent_at.has_value() and now - entry.last_sent_at.value() < entry.min_interval) {
            continue;
        }
        due.push_back(i);
    }
    return due;
}

void OcppSink::write_batch(const std::vector<std::size_t>& batch, std::chrono::steady_clock::time_point now) {
    std::vector<OcppWrite> writes;
    std::vector<std::uint64_t> generations;
    std::vector<std::string> labels;
    {
        const std::lock_guard lock(m_mutex);
        for (const auto index : batch) {
            auto write = m_entries[index].address;
            write.value = m_entries[index].pending.value_or("");
            writes.push_back(std::move(write));
            generations.push_back(m_entries[index].pending_generation);
            labels.push_back(m_entries[index].label);
        }
    }

    std::vector<OcppWriteStatus> statuses;
    if (m_client == nullptr) {
        for (std::size_t i = 0; i < writes.size(); ++i) {
            EVLOG_info << fmt::format("OCPP sink '{}' has no OCPP connection, would write {} = {}", name(), labels[i],
                                      writes[i].value);
        }
        statuses.assign(writes.size(), OcppWriteStatus::Accepted);
    } else {
        try {
            statuses = m_client->set_variables(writes);
        } catch (const std::exception& e) {
            const std::lock_guard lock(m_mutex);
            if (m_backoff.count() == 0) {
                EVLOG_warning << fmt::format("OCPP sink '{}': set_variables failed: {}", name(), e.what());
            }
            m_backoff = std::min<std::chrono::milliseconds>(
                m_backoff.count() == 0 ? m_options.flush_interval : m_backoff * 2, m_options.max_backoff);
            m_backoff_until = now + m_backoff;
            return;
        }
    }
    statuses.resize(writes.size(), OcppWriteStatus::Rejected);

    const std::lock_guard lock(m_mutex);
    const bool all_rejected =
        std::all_of(statuses.begin(), statuses.end(), [](auto status) { return status == OcppWriteStatus::Rejected; });
    if (all_rejected and not m_any_accepted) {
        // before the first accepted write, OCPP rejects everything while it is still starting
        m_backoff = std::min<std::chrono::milliseconds>(
            m_backoff.count() == 0 ? m_options.flush_interval : m_backoff * 2, m_options.max_backoff);
        m_backoff_until = now + m_backoff;
        EVLOG_debug << fmt::format("OCPP sink '{}': all writes rejected, retrying in {} ms", name(), m_backoff.count());
        return;
    }

    for (std::size_t i = 0; i < batch.size(); ++i) {
        auto& entry = m_entries[batch[i]];
        const bool pending_unchanged = entry.pending_generation == generations[i];
        switch (statuses[i]) {
        case OcppWriteStatus::RebootRequired:
            if (not entry.reported_reboot) {
                entry.reported_reboot = true;
                EVLOG_warning << fmt::format("OCPP sink '{}': writing {} requires a reboot", name(), entry.label);
            }
            [[fallthrough]];
        case OcppWriteStatus::Accepted:
            m_any_accepted = true;
            m_backoff = std::chrono::milliseconds(0);
            entry.last_sent = writes[i].value;
            entry.last_sent_number = pending_unchanged ? entry.pending_number : std::nullopt;
            entry.last_sent_at = now;
            entry.dead_until.reset();
            entry.reported_dead = false;
            entry.reported_rejected = false;
            if (pending_unchanged) {
                entry.pending.reset();
            }
            break;
        case OcppWriteStatus::UnknownComponent:
        case OcppWriteStatus::UnknownVariable:
        case OcppWriteStatus::NotSupportedAttributeType:
            entry.dead_until = now + m_options.dead_retry;
            if (not entry.reported_dead) {
                entry.reported_dead = true;
                EVLOG_error << fmt::format(
                    "OCPP sink '{}': the device model has no writable variable {} for {}; add it to a component "
                    "config file. Retrying in {} s",
                    name(), entry.label, entry.source, m_options.dead_retry.count());
            }
            break;
        case OcppWriteStatus::Rejected:
            if (not entry.reported_rejected) {
                entry.reported_rejected = true;
                EVLOG_warning << fmt::format("OCPP sink '{}': the device model rejected {} = {} from {}", name(),
                                             entry.label, writes[i].value, entry.source);
            }
            if (pending_unchanged) {
                entry.pending.reset();
            }
            break;
        }
    }
}

} // namespace telemetry_router
