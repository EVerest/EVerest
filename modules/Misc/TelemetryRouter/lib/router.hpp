// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "catalog.hpp"
#include "rules.hpp"
#include "sink.hpp"

namespace telemetry_router {

struct RouterStatistics {
    std::uint64_t datagrams{0};
    std::uint64_t truncated{0};
    std::uint64_t decode_errors{0};
    std::uint64_t samples{0};
    std::uint64_t forwarded{0};
    std::uint64_t dropped_by_rules{0};
    std::uint64_t unknown_producer{0};
    std::uint64_t unknown_element{0};
    std::uint64_t invalid_value{0};
    std::uint64_t declares_accepted{0};
    std::uint64_t declares_rejected{0};
};

/// \brief Routes received telemetry to sinks according to the rules
///
/// All methods except enable_sinks() and stop_sinks() are called on the receiver thread only.
class Router {
public:
    /// \throws RulesError if a rule refers to a sink type that is not available
    Router(ElementCatalog catalog, RulesConfig rules, const std::map<std::string, SinkFactory, std::less<>>& factories,
           const SinkEnvironment& environment);

    /// \brief Bind the routes of all elements known at startup
    /// \returns problems found; with \p strict, errors are thrown as RulesError instead
    std::vector<std::string> bind_all(bool strict);

    void handle_datagram(const std::uint8_t* data, std::size_t size);
    void handle_truncated();

    void enable_sinks();
    void stop_sinks();

    const RouterStatistics& statistics() const;
    const ElementCatalog& catalog() const;

    /// \returns the names of the sinks an element is forwarded to
    std::vector<std::string> sinks_of(const std::string& producer_id, const std::string& element) const;

private:
    struct BoundRoute {
        Sink* sink;
        std::unique_ptr<SinkBinding> binding;
    };
    using RouteKey = std::pair<std::string, std::string>;

    /// \returns the routes of an element; bind errors are appended to \p errors
    std::vector<BoundRoute> bind_element(const Producer& producer, const ElementDeclaration& element,
                                         std::vector<std::string>& errors);
    void handle_sample(const everest::telemetry::wire::Sample& message);
    void handle_declare(const everest::telemetry::wire::Declare& message);
    bool value_fits(const ElementDeclaration& element, const nlohmann::json& value) const;

    ElementCatalog m_catalog;
    RulesConfig m_rules;
    std::map<std::string, std::unique_ptr<Sink>, std::less<>> m_sinks;
    std::map<RouteKey, std::vector<BoundRoute>> m_routes;
    std::set<const Rule*> m_used_rules;
    std::set<RouteKey> m_reported_unknown;
    RouterStatistics m_statistics;
};

} // namespace telemetry_router
