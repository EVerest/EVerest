// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <framework/telemetry.hpp>
#include <utils/telemetry/catalog.hpp>
#include <utils/telemetry/wire.hpp>

namespace telemetry_router {

using everest::telemetry::ElementDeclaration;
using everest::telemetry::ElementDeclarations;
using everest::telemetry::Kind;
using everest::telemetry::ValueType;

struct Producer {
    std::string id;
    std::string type;
    std::optional<int> evse;
    std::optional<int> connector;
    bool dynamic{false};
    ElementDeclarations elements;
};

struct CatalogLimits {
    std::size_t max_dynamic_producers{32};
    std::size_t max_dynamic_elements{1024};
};

/// \brief Telemetry elements known to the router: seeded from the manifests, extended by declare messages
class ElementCatalog {
public:
    explicit ElementCatalog(CatalogLimits limits = {});

    /// \brief Add the producers of the framework telemetry catalog
    void seed(const everest::telemetry::TelemetryCatalog& catalog);

    struct DeclareResult {
        bool accepted{false};
        std::string producer_id;
        std::string reason;
        std::vector<std::string> warnings;
    };

    /// \brief Add or replace the elements of a producer that has no manifest
    DeclareResult declare(const everest::telemetry::wire::Declare& message);

    const Producer* find_producer(std::string_view id) const;
    const ElementDeclaration* find_element(std::string_view producer_id, std::string_view element) const;
    const std::map<std::string, Producer, std::less<>>& producers() const;
    std::size_t dynamic_producer_count() const;
    std::size_t dynamic_element_count() const;

private:
    CatalogLimits m_limits;
    std::map<std::string, Producer, std::less<>> m_producers;
};

} // namespace telemetry_router
