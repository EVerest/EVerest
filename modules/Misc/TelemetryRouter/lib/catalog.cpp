// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "catalog.hpp"

#include <fmt/format.h>

namespace telemetry_router {

ElementCatalog::ElementCatalog(CatalogLimits limits) : m_limits(limits) {
}

void ElementCatalog::seed(const everest::telemetry::TelemetryCatalog& catalog) {
    for (const auto& [module_id, declaration] : catalog) {
        Producer producer;
        producer.id = module_id;
        producer.type = declaration.module_type;
        if (declaration.mapping.has_value()) {
            producer.evse = declaration.mapping->evse;
            producer.connector = declaration.mapping->connector;
        }
        producer.elements = declaration.elements;
        m_producers[module_id] = std::move(producer);
    }
}

ElementCatalog::DeclareResult ElementCatalog::declare(const everest::telemetry::wire::Declare& message) {
    DeclareResult result;
    result.producer_id = message.module_id;
    for (const auto& invalid : message.invalid_elements) {
        result.warnings.push_back(fmt::format("Declaration of '{}': ignoring {}", message.module_id, invalid));
    }

    if (message.module_id.empty()) {
        result.reason = "declaration without module id";
        return result;
    }
    const auto existing = m_producers.find(message.module_id);
    if (existing != m_producers.end() and not existing->second.dynamic) {
        result.reason = "the module id belongs to an EVerest module";
        return result;
    }
    if (existing == m_producers.end() and dynamic_producer_count() >= m_limits.max_dynamic_producers) {
        result.reason = fmt::format("limit of {} dynamic producers reached", m_limits.max_dynamic_producers);
        return result;
    }
    if (message.elements.empty()) {
        result.reason = "declaration without valid elements";
        return result;
    }
    const auto replaced_count = existing != m_producers.end() ? existing->second.elements.size() : 0;
    if (dynamic_element_count() - replaced_count + message.elements.size() > m_limits.max_dynamic_elements) {
        result.reason = fmt::format("limit of {} dynamic elements reached", m_limits.max_dynamic_elements);
        return result;
    }

    Producer producer;
    producer.id = message.module_id;
    producer.type = message.module_type;
    producer.dynamic = true;
    producer.elements = message.elements;
    m_producers[message.module_id] = std::move(producer);
    result.accepted = true;
    return result;
}

const Producer* ElementCatalog::find_producer(std::string_view id) const {
    const auto it = m_producers.find(id);
    return it == m_producers.end() ? nullptr : &it->second;
}

const ElementDeclaration* ElementCatalog::find_element(std::string_view producer_id, std::string_view element) const {
    const auto* producer = find_producer(producer_id);
    if (producer == nullptr) {
        return nullptr;
    }
    const auto it = producer->elements.find(element);
    return it == producer->elements.end() ? nullptr : &it->second;
}

const std::map<std::string, Producer, std::less<>>& ElementCatalog::producers() const {
    return m_producers;
}

std::size_t ElementCatalog::dynamic_producer_count() const {
    std::size_t count = 0;
    for (const auto& [id, producer] : m_producers) {
        count += producer.dynamic ? 1 : 0;
    }
    return count;
}

std::size_t ElementCatalog::dynamic_element_count() const {
    std::size_t count = 0;
    for (const auto& [id, producer] : m_producers) {
        count += producer.dynamic ? producer.elements.size() : 0;
    }
    return count;
}

} // namespace telemetry_router
