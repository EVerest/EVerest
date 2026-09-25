// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include <framework/telemetry.hpp>
#include <utils/types.hpp>

namespace everest::telemetry {

/// \brief One telemetry element, as declared in the telemetry section of a manifest
struct ElementDeclaration {
    std::string name;
    Kind kind{Kind::Gauge};
    ValueType value_type{ValueType::Number};
    std::string description;
    std::optional<std::string> unit;
    /// Values of an enum state, declared inline or resolved from type_ref
    std::vector<std::string> enum_values;
    /// Referenced type in types/, e.g. "/evse_manager#/ChargeMode"
    std::optional<std::string> type_ref;
};

using ElementDeclarations = std::map<std::string, ElementDeclaration, std::less<>>;

/// \brief The telemetry elements of one module instance
struct ProducerDeclaration {
    std::string module_type;
    /// EVSE and connector from the module mapping in the configuration
    std::optional<Mapping> mapping;
    ElementDeclarations elements;
};

/// \brief Telemetry declarations of all active modules, by module id
using TelemetryCatalog = std::map<std::string, ProducerDeclaration, std::less<>>;

/// \brief Parse an element declaration with the keys of the manifest telemetry section
/// \returns the declaration, or the reason why it is invalid
std::variant<ElementDeclaration, std::string> parse_element_declaration(const std::string& name,
                                                                        const nlohmann::json& declaration);

/// \brief Parse a manifest telemetry section; invalid elements are skipped and reported in \p errors
ElementDeclarations parse_element_declarations(const nlohmann::json& section, std::vector<std::string>& errors);

/// \returns the values of the string enum referenced by \p ref (e.g. "/evse_manager#/StopTransactionReason")
using EnumResolver = std::function<std::optional<std::vector<std::string>>(const std::string& ref)>;

/// \returns the configured module mapping (EVSE/connector) of a module id
using MappingLookup = std::function<std::optional<Mapping>(const std::string& module_id)>;

/// \brief Resolve enum references from the type files installed in \p types_dir
EnumResolver make_types_dir_enum_resolver(const std::filesystem::path& types_dir);

/// \brief Telemetry catalog of all active modules that declare telemetry
///
/// State elements referencing an enum get its values in enum_values.
TelemetryCatalog build_telemetry_catalog(const nlohmann::json& manifests,
                                         const std::map<std::string, std::string, std::less<>>& module_names,
                                         const MappingLookup& module_mapping, const EnumResolver& resolve_enum);

/// \brief Manifest keys: kind, type, description, unit, enum, $ref; the name is the key of the enclosing object
void to_json(nlohmann::json& j, const ElementDeclaration& element);
void to_json(nlohmann::json& j, const ProducerDeclaration& producer);
/// \throws std::invalid_argument if an element is invalid
void from_json(const nlohmann::json& j, ProducerDeclaration& producer);

} // namespace everest::telemetry
