// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "router.hpp"

#include <algorithm>

#include <everest/logging.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <type_traits>
#include <variant>

#include <utils/telemetry/wire.hpp>

namespace telemetry_router {

namespace wire = everest::telemetry::wire;

Router::Router(ElementCatalog catalog, RulesConfig rules,
               const std::map<std::string, SinkFactory, std::less<>>& factories, const SinkEnvironment& environment) :
    m_catalog(std::move(catalog)), m_rules(std::move(rules)) {
    for (const auto& sink_config : m_rules.sinks) {
        const auto factory = factories.find(sink_config.type);
        if (factory == factories.end()) {
            throw RulesError(fmt::format("sinks.{}: sink type '{}' is not available in this build", sink_config.name,
                                         sink_config.type));
        }
        m_sinks[sink_config.name] = factory->second(sink_config, environment);
    }
}

std::vector<std::string> Router::bind_all(bool strict) {
    std::vector<std::string> errors;
    for (const auto& [producer_id, producer] : m_catalog.producers()) {
        for (const auto& [element_name, element] : producer.elements) {
            m_routes[{producer_id, element_name}] = bind_element(producer, element, errors);
        }
    }
    if (strict and not errors.empty()) {
        throw RulesError(fmt::format("{}", fmt::join(errors, "\n")));
    }
    for (const auto& rule : m_rules.rules) {
        if (m_used_rules.count(&rule) == 0) {
            errors.push_back(fmt::format("{} matches no telemetry element declared by an active module", rule.label()));
        }
    }
    return errors;
}

std::vector<Router::BoundRoute> Router::bind_element(const Producer& producer, const ElementDeclaration& element,
                                                     std::vector<std::string>& errors) {
    std::vector<BoundRoute> routes;
    for (const auto& route : evaluate(m_rules, producer, element)) {
        if (route.rule != nullptr) {
            m_used_rules.insert(route.rule);
        }
        auto& sink = *m_sinks.at(route.target->sink);
        try {
            routes.push_back({&sink, sink.bind(*route.target, producer, element)});
        } catch (const BindError& e) {
            errors.push_back(fmt::format("{}: element '{}' of '{}' to sink '{}': {}",
                                         route.rule != nullptr ? route.rule->label() : std::string("default_action"),
                                         element.name, producer.id, sink.name(), e.what()));
        }
    }
    return routes;
}

void Router::handle_datagram(const std::uint8_t* data, std::size_t size) {
    ++m_statistics.datagrams;
    const auto decoded = wire::decode(data, size);
    if (not decoded.message.has_value()) {
        ++m_statistics.decode_errors;
        EVLOG_debug << "Dropping telemetry datagram: " << wire::to_string(decoded.error.value());
        return;
    }
    std::visit(
        [this](const auto& message) {
            if constexpr (std::is_same_v<std::decay_t<decltype(message)>, wire::Sample>) {
                handle_sample(message);
            } else {
                handle_declare(message);
            }
        },
        decoded.message.value());
}

void Router::handle_truncated() {
    ++m_statistics.datagrams;
    ++m_statistics.truncated;
}

void Router::handle_sample(const wire::Sample& message) {
    ++m_statistics.samples;

    const auto& producer_id = message.module_id;
    const auto& element_name = message.element;
    const auto* producer = m_catalog.find_producer(producer_id);
    if (producer == nullptr) {
        ++m_statistics.unknown_producer;
        return;
    }
    const auto element = producer->elements.find(element_name);
    if (element == producer->elements.end()) {
        ++m_statistics.unknown_element;
        if (m_reported_unknown.emplace(producer_id, element_name).second) {
            EVLOG_warning << fmt::format("Dropping telemetry element '{}' of '{}': it is not declared", element_name,
                                         producer_id);
        }
        return;
    }
    if (not value_fits(element->second, message.value)) {
        ++m_statistics.invalid_value;
        return;
    }

    const auto routes = m_routes.find({producer_id, element_name});
    if (routes == m_routes.end() or routes->second.empty()) {
        ++m_statistics.dropped_by_rules;
        return;
    }
    const Sample sample{*producer, element->second, message.timestamp_ms, message.value};
    for (const auto& route : routes->second) {
        route.sink->submit(sample, *route.binding);
        ++m_statistics.forwarded;
    }
}

void Router::handle_declare(const wire::Declare& message) {
    auto result = m_catalog.declare(message);
    for (const auto& warning : result.warnings) {
        EVLOG_warning << warning;
    }
    if (not result.accepted) {
        ++m_statistics.declares_rejected;
        EVLOG_warning << fmt::format("Rejecting telemetry declaration of '{}': {}", result.producer_id, result.reason);
        return;
    }
    ++m_statistics.declares_accepted;

    for (auto it = m_routes.begin(); it != m_routes.end();) {
        if (it->first.first != result.producer_id) {
            ++it;
            continue;
        }
        for (const auto& route : it->second) {
            route.sink->release(*route.binding);
        }
        it = m_routes.erase(it);
    }
    const auto& producer = *m_catalog.find_producer(result.producer_id);
    std::vector<std::string> errors;
    std::set<std::string> sink_names;
    for (const auto& [element_name, element] : producer.elements) {
        auto routes = bind_element(producer, element, errors);
        for (const auto& route : routes) {
            sink_names.insert(route.sink->name());
        }
        m_routes[{producer.id, element_name}] = std::move(routes);
    }
    for (const auto& error : errors) {
        EVLOG_error << error;
    }
    EVLOG_info << fmt::format("Accepted telemetry declaration of '{}' with {} elements, forwarded to: {}", producer.id,
                              producer.elements.size(),
                              sink_names.empty() ? "none" : fmt::format("{}", fmt::join(sink_names, ", ")));
}

bool Router::value_fits(const ElementDeclaration& element, const nlohmann::json& value) const {
    switch (element.value_type) {
    case ValueType::Number:
        return value.is_number();
    case ValueType::Integer:
        return value.is_number_integer();
    case ValueType::Boolean:
        return value.is_boolean();
    case ValueType::String:
        return value.is_string() and (element.enum_values.empty() or
                                      std::find(element.enum_values.begin(), element.enum_values.end(),
                                                value.get_ref<const std::string&>()) != element.enum_values.end());
    case ValueType::Object:
        return value.is_object();
    }
    return false;
}

void Router::enable_sinks() {
    for (auto& [name, sink] : m_sinks) {
        sink->enable();
    }
}

void Router::stop_sinks() {
    for (auto& [name, sink] : m_sinks) {
        sink->stop();
    }
}

const RouterStatistics& Router::statistics() const {
    return m_statistics;
}

const ElementCatalog& Router::catalog() const {
    return m_catalog;
}

std::vector<std::string> Router::sinks_of(const std::string& producer_id, const std::string& element) const {
    std::vector<std::string> names;
    const auto routes = m_routes.find({producer_id, element});
    if (routes != m_routes.end()) {
        for (const auto& route : routes->second) {
            names.push_back(route.sink->name());
        }
    }
    return names;
}

} // namespace telemetry_router
